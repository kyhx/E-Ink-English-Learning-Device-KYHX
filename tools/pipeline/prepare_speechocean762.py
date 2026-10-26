#!/usr/bin/env python3
r"""Build a pipeline `labels.csv` from speechocean762.

speechocean762 is sentence-level (5000 read sentences, 125/125 speaker-disjoint
train/test).  We use sentence `accuracy` as a proxy binary label:

    accuracy >= 8  -> 1 (good)
    accuracy <= 7  -> 0 (needs improvement)

This is NOT the same task as a 2-second single-word card.  It is a public-data
pipeline test: real non-native speech, real human scores, official speaker split.

Download:
    https://www.openslr.org/101/
Extract so that this exists:
    <corpus>/resource/scores.json
    <corpus>/train/utt2spk
    <corpus>/test/utt2spk
    <corpus>/WAVE/SPEAKER0001/000010011.WAV

Example:
    .\.venv-train\Scripts\python.exe tools\pipeline\prepare_speechocean762.py ^
        --corpus build\data\speechocean762 ^
        --out build\data\speechocean762_pipeline ^
        --limit-per-class 500
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import random
from collections import Counter
from pathlib import Path


def _read_utt2spk(path: Path) -> dict[str, str]:
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        utt, spk = line.split(maxsplit=1)
        out[utt] = spk
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--limit-per-class", type=int, default=500,
                    help="per split/class; 0 = all")
    ap.add_argument("--score-key", default="accuracy")
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    root = args.corpus.resolve()
    scores = json.loads((root / "resource" / "scores.json").read_text(encoding="utf-8"))
    rng = random.Random(args.seed)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    rows = []
    for split in ("train", "test"):
        utt2spk = _read_utt2spk(root / split / "utt2spk")
        pos, neg = [], []
        for utt, spk in utt2spk.items():
            rec = scores.get(utt)
            if not rec:
                continue
            wav = root / "WAVE" / f"SPEAKER{spk}" / f"{utt}.WAV"
            if not wav.exists():
                wav = root / "WAVE" / f"SPEAKER{spk}" / f"{utt}.wav"
            if not wav.exists():
                print(f"[warn] missing wav: {utt}")
                continue
            label = 1 if float(rec[args.score_key]) >= 8 else 0
            (pos if label == 1 else neg).append((utt, spk, wav, label))

        for name, items in (("pos", pos), ("neg", neg)):
            if args.limit_per_class and len(items) > args.limit_per_class:
                items = rng.sample(items, args.limit_per_class)
            for utt, spk, wav, label in items:
                rows.append({
                    "utt_id": utt,
                    "file": os.path.relpath(wav, out).replace("\\", "/"),
                    "speaker_id": spk,
                    "word_id": 0,
                    "label": label,
                    "split": split,
                })
            print(f"{split}/{name}: {len(items)}")

    rows.sort(key=lambda r: (r["split"], r["speaker_id"], r["utt_id"]))
    with (out / "labels.csv").open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(
            f, fieldnames=["utt_id", "file", "speaker_id", "word_id", "label", "split"]
        )
        w.writeheader()
        w.writerows(rows)

    by_split = Counter(r["split"] for r in rows)
    by_label = Counter((r["split"], r["label"]) for r in rows)
    spk = {s: len({r["speaker_id"] for r in rows if r["split"] == s})
           for s in ("train", "test")}
    print(f"wrote {out / 'labels.csv'}: {len(rows)} rows")
    print(f"split={dict(by_split)}  labels={dict(by_label)}  speakers={spk}")
    if by_split.get("train", 0) == 0 or by_split.get("test", 0) == 0:
        raise SystemExit("need both train and test rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
