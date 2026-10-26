#!/usr/bin/env python3
r"""Dataset manifest, speaker-aware split, synthetic toy data, feature cache.

Manifest (`labels.csv`) required columns:
    file,label
Optional columns:
    utt_id,speaker_id,word_id

Example:
    utt_id,file,speaker_id,word_id,label
    u0001,audio/u0001.wav,S01,12,1
    u0002,audio/u0002.wav,S01,12,0

`label`: 1 = 正确/可接受, 0 = 错误. `speaker_id` is used for speaker-independent
splits; do not random-split by sample for the final experiment.
"""
from __future__ import annotations

import csv
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

SAMPLE_RATE = 16000
N_SAMPLES = 32000


@dataclass
class Sample:
    utt_id: str
    path: Path
    speaker_id: str
    word_id: int
    label: int
    split: str = ""


def _parse_label(v: str) -> int:
    s = str(v).strip().lower()
    if s in {"1", "true", "correct", "ok", "yes", "正", "正确"}:
        return 1
    if s in {"0", "false", "incorrect", "bad", "no", "负", "错误"}:
        return 0
    raise ValueError(f"unknown label: {v!r}")


def load_manifest(data_dir: str | Path) -> list[Sample]:
    data_dir = Path(data_dir)
    manifest = data_dir / "labels.csv"
    if not manifest.exists():
        raise FileNotFoundError(f"missing {manifest}")

    rows = list(csv.DictReader(manifest.open(encoding="utf-8-sig", newline="")))
    if not rows:
        raise ValueError(f"empty manifest: {manifest}")

    samples: list[Sample] = []
    for i, row in enumerate(rows):
        row = {str(k).strip().lower(): v for k, v in row.items()}
        if "file" not in row or "label" not in row:
            raise ValueError("labels.csv must contain file,label columns")
        rel = str(row["file"]).strip()
        path = (data_dir / rel).resolve()
        if not path.exists():
            path = (data_dir / "audio" / Path(rel).name).resolve()
        if not path.exists():
            print(f"[warn] missing audio: {rel}")
            continue
        utt = str(row.get("utt_id") or path.stem)
        speaker = str(row.get("speaker_id") or "unknown")
        word_raw = str(row.get("word_id") or "-1")
        try:
            word_id = int(word_raw)
        except ValueError:
            word_id = -1
        samples.append(
            Sample(
                utt_id=utt,
                path=path,
                speaker_id=speaker,
                word_id=word_id,
                label=_parse_label(row["label"]),
                split=str(row.get("split") or "").strip().lower(),
            )
        )
    if not samples:
        raise ValueError("no valid samples after manifest check")
    return samples


def generate_toy_dataset(
    out_dir: str | Path,
    n_speakers: int = 6,
    n_per_speaker: int = 50,
    seed: int = 7,
) -> Path:
    """Create a small synthetic WAV dataset so the whole pipeline runs offline.

    It is NOT a pronunciation dataset: two classes differ by a high-frequency
    formant.  It only proves audio -> log-Mel -> CNN -> TFLite works.
    """
    import soundfile as sf

    out = Path(out_dir)
    audio_dir = out / "audio"
    audio_dir.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(seed)
    rows = []
    t = np.arange(N_SAMPLES, dtype=np.float32) / SAMPLE_RATE

    for spk in range(n_speakers):
        speaker = f"S{spk + 1:02d}"
        base_f0 = 105.0 + 18.0 * spk
        for j in range(n_per_speaker):
            label = 1 if j % 2 == 0 else 0
            f0 = base_f0 * float(rng.uniform(0.9, 1.1))
            formant = 1200.0 if label == 0 else 2600.0

            y = np.zeros_like(t)
            for h in range(1, 36):
                f = f0 * h
                if f > 7600:
                    break
                amp = (1.0 / h) * (1.0 + 2.2 * np.exp(-0.5 * ((f - formant) / 260.0) ** 2))
                y += amp * np.sin(2 * np.pi * f * t + float(rng.uniform(0, 2 * np.pi)))
            # A second, fixed formant keeps the signal speech-like.
            y += 0.5 * np.sin(2 * np.pi * 700 * t)
            env = np.exp(-0.5 * ((t - float(rng.uniform(0.8, 1.2))) / 0.35) ** 2) + 0.08
            y = y * env
            y += 0.01 * rng.normal(size=y.shape)
            y[:800] = 0.0
            y[-800:] = 0.0
            peak = float(np.max(np.abs(y))) or 1.0
            y = (0.6 * y / peak).astype(np.float32)

            utt = f"{speaker.lower()}_{j:03d}"
            rel = f"audio/{utt}.wav"
            sf.write(out / rel, y, SAMPLE_RATE, subtype="PCM_16")
            rows.append(
                {
                    "utt_id": utt,
                    "file": rel,
                    "speaker_id": speaker,
                    "word_id": int(rng.integers(0, 50)),
                    "label": label,
                }
            )

    with (out / "labels.csv").open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["utt_id", "file", "speaker_id", "word_id", "label"])
        w.writeheader()
        w.writerows(rows)
    print(f"toy dataset: {len(rows)} wavs, {n_speakers} speakers -> {out}")
    return out


def extract_features(
    samples: list[Sample],
    cache_path: str | Path | None = None,
    *,
    do_vad: bool = True,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Extract [N,40,64] features and return x, y, word_id, speaker_id arrays."""
    from features import extract_file

    cache = Path(cache_path) if cache_path else None
    fingerprint = hashlib.sha1(
        "\n".join(f"{s.path}|{s.label}|{s.speaker_id}" for s in samples).encode("utf-8")
    ).hexdigest()[:16]
    if cache and cache.exists():
        z = np.load(cache, allow_pickle=True)
        cached_fp = str(z["fingerprint"]) if "fingerprint" in z else ""
        if len(z["y"]) == len(samples) and cached_fp == fingerprint:
            print(f"features cache: {cache} ({z['x'].shape})")
            return z["x"], z["y"], z["word_id"], z["speaker"]
        print("[warn] feature cache is stale (manifest changed); recomputing")

    xs = []
    for i, s in enumerate(samples):
        try:
            xs.append(extract_file(s.path, do_vad=do_vad))
        except Exception as exc:  # noqa: BLE001
            raise RuntimeError(f"feature extraction failed for {s.path}: {exc}") from exc
        if (i + 1) % 25 == 0 or i + 1 == len(samples):
            print(f"features: {i + 1}/{len(samples)}")

    x = np.stack(xs).astype(np.float32)[:, None, :, :]  # [N,1,40,64]
    y = np.asarray([s.label for s in samples], dtype=np.int64)
    word = np.asarray([s.word_id for s in samples], dtype=np.int64)
    speaker = np.asarray([s.speaker_id for s in samples], dtype=object)

    if cache:
        cache.parent.mkdir(parents=True, exist_ok=True)
        np.savez_compressed(
            cache, x=x, y=y, word_id=word, speaker=speaker,
            fingerprint=np.array(fingerprint),
        )
        print(f"features saved: {cache}")
    return x, y, word, speaker


def split_by_speaker(
    speaker: np.ndarray,
    *,
    val_speakers: list[str] | None = None,
    pre_split: list[str] | None = None,
    val_ratio: float = 0.25,
    seed: int = 7,
) -> tuple[np.ndarray, np.ndarray, dict]:
    """Return train/val indices; prefer a predefined split, then held-out speakers."""
    if pre_split and any(str(s).strip().lower() in {"train", "val", "test"} for s in pre_split):
        labels = np.asarray([str(s).strip().lower() for s in pre_split])
        train_idx = np.where(labels == "train")[0]
        val_idx = np.where(np.isin(labels, ["val", "test"]))[0]
        if len(train_idx) and len(val_idx):
            val_spk = sorted({str(speaker[i]) for i in val_idx})
            train_spk = sorted({str(speaker[i]) for i in train_idx})
            overlap = sorted(set(val_spk) & set(train_spk))
            if overlap:
                print(f"[warn] predefined split has speaker overlap: {overlap[:5]}")
            info = {
                "mode": "predefined",
                "val_speakers": val_spk,
                "speaker_overlap": overlap,
            }
            return train_idx, val_idx, info

    speakers = sorted({str(s) for s in speaker})
    if len(speakers) < 2:
        rng = np.random.default_rng(seed)
        n = len(speaker)
        idx = rng.permutation(n)
        n_val = max(1, int(round(n * val_ratio)))
        val_idx, train_idx = np.sort(idx[:n_val]), np.sort(idx[n_val:])
        info = {"mode": "random_fallback", "warning": "only one speaker", "val_speakers": []}
        print("[warn] only one speaker: random split is NOT speaker-independent")
        return train_idx, val_idx, info

    if val_speakers:
        chosen = [s for s in val_speakers if s in speakers]
        if not chosen:
            raise ValueError(f"val speakers not in data: {val_speakers}")
    else:
        rng = np.random.default_rng(seed)
        n_val = max(1, int(round(len(speakers) * val_ratio)))
        chosen = sorted(rng.choice(speakers, size=n_val, replace=False).tolist())

    val_mask = np.isin(speaker, chosen)
    train_idx = np.where(~val_mask)[0]
    val_idx = np.where(val_mask)[0]
    if len(train_idx) == 0 or len(val_idx) == 0:
        raise ValueError("empty train/val split")
    info = {"mode": "speaker_independent", "val_speakers": chosen}
    return train_idx, val_idx, info


class FeatureDataset:
    """Torch Dataset with light feature-level augmentation for training."""

    def __init__(self, x: np.ndarray, y: np.ndarray, *, augment: bool = False, seed: int = 7):
        import torch
        from torch.utils.data import Dataset

        class _Dataset(Dataset):
            def __init__(self) -> None:
                self.x = torch.from_numpy(x)
                self.y = torch.from_numpy(y)
                self.augment = augment
                self.rng = np.random.default_rng(seed)

            def __len__(self) -> int:
                return len(self.y)

            def __getitem__(self, i: int):
                z = self.x[i].clone()
                if self.augment:
                    # Gain (adds a constant in log-Mel), time shift, small noise.
                    z = z + float(self.rng.uniform(-0.8, 0.8))
                    shift = int(self.rng.integers(-4, 5))
                    if shift:
                        z = torch.roll(z, shifts=shift, dims=2)
                    if self.rng.random() < 0.5:
                        z = z + torch.from_numpy(
                            self.rng.normal(0, 0.05, size=tuple(z.shape)).astype(np.float32)
                        )
                return z, self.y[i]

        self.dataset = _Dataset()


def class_counts(y: np.ndarray) -> dict[int, int]:
    vals, counts = np.unique(y, return_counts=True)
    return {int(v): int(c) for v, c in zip(vals, counts)}
