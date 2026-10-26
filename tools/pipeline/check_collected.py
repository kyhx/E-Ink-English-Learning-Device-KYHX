#!/usr/bin/env python3
r"""Validate a collected dataset before training.

Usage:
    .\.venv-train\Scripts\python.exe tools\pipeline\check_collected.py ^
        --data-dir data\collected\v1.0

Checks:
- labels.csv columns and labels
- WAV exists, readable, 16 kHz mono, about 2 s
- quiet / clipped / duplicate takes
- per-speaker and per-label counts
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import sys
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", type=Path, required=True)
    ap.add_argument("--min-peak", type=float, default=0.01)
    ap.add_argument("--min-duration", type=float, default=1.5)
    ap.add_argument("--max-duration", type=float, default=2.5)
    args = ap.parse_args()

    import soundfile as sf

    root = args.data_dir.resolve()
    manifest = root / "labels.csv"
    if not manifest.exists():
        print(f"ERROR: missing {manifest}")
        return 1
    rows = list(csv.DictReader(manifest.open(encoding="utf-8-sig", newline="")))
    if not rows:
        print("ERROR: empty labels.csv")
        return 1

    errors: list[str] = []
    warnings: list[str] = []
    seen_hashes: dict[str, str] = {}
    spk = Counter()
    labels = Counter()
    words = Counter()
    per_spk_label = defaultdict(Counter)

    required = {"utt_id", "file", "speaker_id", "word_id", "label"}
    missing_cols = required - set(rows[0].keys())
    if missing_cols:
        print(f"ERROR: missing columns {sorted(missing_cols)}")
        return 1

    for r in rows:
        utt = r.get("utt_id", "")
        rel = r.get("file", "")
        p = root / rel
        if not p.exists():
            errors.append(f"{utt}: missing file {rel}")
            continue
        try:
            info = sf.info(str(p))
            y, sr = sf.read(str(p), dtype="float32", always_2d=True)
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{utt}: unreadable {exc}")
            continue
        y = y.mean(axis=1)
        dur = len(y) / sr
        peak = float(np.max(np.abs(y))) if len(y) else 0.0

        if info.samplerate != 16000:
            warnings.append(f"{utt}: sample rate {info.samplerate}, expected 16000")
        if info.channels != 1:
            warnings.append(f"{utt}: {info.channels} channels, expected mono")
        if not (args.min_duration <= dur <= args.max_duration):
            warnings.append(f"{utt}: duration {dur:.2f}s, expected ~2s")
        if peak < args.min_peak:
            warnings.append(f"{utt}: very quiet peak={peak:.4f}")
        if peak >= 0.999:
            warnings.append(f"{utt}: possibly clipped peak={peak:.4f}")

        h = hashlib.sha1(y.tobytes()).hexdigest()[:16]
        if h in seen_hashes:
            warnings.append(f"{utt}: duplicate audio of {seen_hashes[h]}")
        else:
            seen_hashes[h] = utt

        label = str(r.get("label", "")).strip()
        if label not in {"0", "1"}:
            errors.append(f"{utt}: label must be 0/1, got {label!r}")
        else:
            labels[label] += 1
            per_spk_label[r.get("speaker_id", "")][label] += 1
        spk[r.get("speaker_id", "")] += 1
        words[r.get("word_id", "")] += 1

    print(f"rows: {len(rows)}")
    print(f"speakers: {len(spk)}  labels: {dict(labels)}")
    for s in sorted(spk):
        print(f"  {s}: {spk[s]}  labels={dict(per_spk_label[s])}")
    print(f"words covered: {len(words)}  min/max per word: "
          f"{min(words.values()) if words else 0}/{max(words.values()) if words else 0}")

    if warnings:
        print(f"\nWARNINGS ({len(warnings)}):")
        for w in warnings[:40]:
            print(" -", w)
        if len(warnings) > 40:
            print(f" ... {len(warnings) - 40} more")
    if errors:
        print(f"\nERRORS ({len(errors)}):")
        for e in errors[:40]:
            print(" -", e)
        if len(errors) > 40:
            print(f" ... {len(errors) - 40} more")
        return 1
    print("\nPASS: manifest and WAVs are readable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
