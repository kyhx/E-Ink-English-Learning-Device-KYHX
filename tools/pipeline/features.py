#!/usr/bin/env python3
r"""Audio -> log-Mel 40x64 feature extractor.

This file is the single source of truth for the PC-side frontend.  The C
implementation in `main/dsp` must mirror these constants exactly:

    16 kHz mono, 2.0 s (32000 samples)
    n_fft=512, win_length=400 (25 ms), hop_length=160 (10 ms)
    n_mels=40, fmin=20, fmax=7600, power=2
    log(max(mel, 1e-6)), per-utterance mean/variance (CMVN)
    adaptive average pool over time -> 64 frames
    int8 quantization uses the TFLite input scale/zero from model_params.h

Run a quick check:
    .\.venv-train\Scripts\python.exe tools\pipeline\features.py --selftest
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

SAMPLE_RATE = 16000
DURATION_SEC = 2.0
N_SAMPLES = int(SAMPLE_RATE * DURATION_SEC)  # 32000
N_FFT = 512
WIN_LENGTH = 400       # 25 ms
HOP_LENGTH = 160       # 10 ms
N_MELS = 40
N_FRAMES = 64
FMIN = 20.0
FMAX = 7600.0

# Keep the same defaults as the firmware/feature spec.
VAD_FRAME_MS = 20
VAD_HOP_MS = 10
VAD_MARGIN_MS = 100

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def load_audio(path: str | Path, target_sr: int = SAMPLE_RATE) -> np.ndarray:
    """Read a WAV/FLAC/OGG file, downmix to mono, resample to 16 kHz."""
    import librosa
    import soundfile as sf

    y, sr = sf.read(str(path), dtype="float32", always_2d=True)
    y = y.mean(axis=1)  # mono
    if sr != target_sr:
        y = librosa.resample(y, orig_sr=sr, target_sr=target_sr, res_type="soxr_hq")
    return np.asarray(y, dtype=np.float32)


def trim_silence(
    y: np.ndarray,
    sr: int = SAMPLE_RATE,
    frame_ms: int = VAD_FRAME_MS,
    hop_ms: int = VAD_HOP_MS,
    margin_ms: int = VAD_MARGIN_MS,
) -> np.ndarray:
    """Energy-based VAD trim with a small margin.  Never returns empty."""
    frame = max(1, int(sr * frame_ms / 1000))
    hop = max(1, int(sr * hop_ms / 1000))
    if len(y) < frame:
        return y
    n = 1 + (len(y) - frame) // hop
    energy = np.empty(n, dtype=np.float32)
    for i in range(n):
        seg = y[i * hop:i * hop + frame]
        energy[i] = float(np.mean(seg * seg))

    # Relative threshold: ignore quiet noise but keep most speech.
    floor = float(np.median(energy))
    thresh = max(floor * 4.0, float(energy.max()) * 0.02, 1e-8)
    active = np.where(energy >= thresh)[0]
    if active.size == 0:
        return y

    start = max(0, active[0] * hop - int(sr * margin_ms / 1000))
    end = min(len(y), active[-1] * hop + frame + int(sr * margin_ms / 1000))
    if end - start < int(0.25 * sr):
        return y
    return y[start:end]


def pad_or_crop(y: np.ndarray, n: int = N_SAMPLES) -> np.ndarray:
    """Center-pad or center-crop to exactly n samples."""
    if len(y) >= n:
        start = (len(y) - n) // 2
        return y[start:start + n]
    left = (n - len(y)) // 2
    right = n - len(y) - left
    return np.pad(y, (left, right), mode="constant")


def _adaptive_pool_time(mel: np.ndarray, frames: int = N_FRAMES) -> np.ndarray:
    """Average-pool [n_mels, T] to [n_mels, frames] with stable boundaries."""
    n_mels, t = mel.shape
    if t == frames:
        return mel
    if t < frames:
        # Interpolate along time for very short clips.
        idx = np.linspace(0, t - 1, frames)
        lo = np.floor(idx).astype(int)
        hi = np.minimum(lo + 1, t - 1)
        w = (idx - lo).astype(np.float32)
        return mel[:, lo] * (1 - w) + mel[:, hi] * w

    bounds = np.linspace(0, t, frames + 1).astype(int)
    out = np.empty((n_mels, frames), dtype=np.float32)
    for i in range(frames):
        a, b = bounds[i], bounds[i + 1]
        if b <= a:
            b = min(t, a + 1)
        out[:, i] = mel[:, a:b].mean(axis=1)
    return out


def logmel_from_waveform(
    y: np.ndarray,
    sr: int = SAMPLE_RATE,
    *,
    do_vad: bool = True,
    do_cmvn: bool = True,
) -> np.ndarray:
    """Return a float32 [40, 64] log-Mel feature."""
    import librosa

    y = np.asarray(y, dtype=np.float32).reshape(-1)
    if do_vad:
        y = trim_silence(y, sr)
    y = pad_or_crop(y, int(sr * DURATION_SEC))

    # Remove DC; keep the waveform bounded.
    y = y - float(np.mean(y))
    peak = float(np.max(np.abs(y)))
    if peak > 1.0:
        y = y / peak

    mel = librosa.feature.melspectrogram(
        y=y,
        sr=sr,
        n_fft=N_FFT,
        win_length=WIN_LENGTH,
        hop_length=HOP_LENGTH,
        window="hamming",
        center=True,
        power=2.0,
        n_mels=N_MELS,
        fmin=FMIN,
        fmax=FMAX,
    )
    logmel = np.log(np.maximum(mel, 1e-6)).astype(np.float32)
    logmel = _adaptive_pool_time(logmel, N_FRAMES)

    if do_cmvn:
        logmel = (logmel - float(logmel.mean())) / (float(logmel.std()) + 1e-5)
        logmel = np.clip(logmel, -5.0, 5.0)
    return logmel.astype(np.float32)


def extract_file(path: str | Path, *, do_vad: bool = True) -> np.ndarray:
    y = load_audio(path)
    return logmel_from_waveform(y, SAMPLE_RATE, do_vad=do_vad)


def quantize_int8(x: np.ndarray, scale: float, zero_point: int) -> np.ndarray:
    """Quantize float features the same way the firmware input tensor does."""
    q = np.round(np.asarray(x, dtype=np.float32) / float(scale) + int(zero_point))
    return np.clip(q, -128, 127).astype(np.int8)


def dequantize_int8(q: np.ndarray, scale: float, zero_point: int) -> np.ndarray:
    return (np.asarray(q, dtype=np.float32) - int(zero_point)) * float(scale)


def _selftest() -> int:
    """Generate a tone, extract features, and print shape/range."""
    import tempfile

    import soundfile as sf

    sr = SAMPLE_RATE
    t = np.arange(sr) / sr
    y = (0.2 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)
    p = Path(tempfile.mktemp(suffix=".wav"))
    sf.write(p, y, sr, subtype="PCM_16")
    f = extract_file(p)
    print(f"feature shape: {f.shape}, dtype: {f.dtype}")
    print(f"range: [{f.min():.3f}, {f.max():.3f}], finite={bool(np.isfinite(f).all())}")
    p.unlink(missing_ok=True)
    ok = f.shape == (N_MELS, N_FRAMES) and np.isfinite(f).all()
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("wav", nargs="?")
    args = ap.parse_args()
    if args.selftest or not args.wav:
        raise SystemExit(_selftest())
    f = extract_file(args.wav)
    print(f"shape={f.shape} min={f.min():.3f} max={f.max():.3f}")
