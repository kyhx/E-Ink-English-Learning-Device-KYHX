#!/usr/bin/env python3
r"""One-command model/data pipeline for the E-Ink word device.

Examples:
    # 1) synthetic data: proves audio -> log-Mel -> CNN -> TFLite int8
    .\.venv-train\Scripts\python.exe tools\pipeline\run.py --toy --out build/pipeline_toy

    # 2) real data
    .\.venv-train\Scripts\python.exe tools\pipeline\run.py ^
        --data-dir data/dataset/v1.0 --out build/pipeline_v1 ^
        --val-speakers S05,S06 --epochs 40 --augment

Real manifest (`labels.csv`):
    utt_id,file,speaker_id,word_id,label
    u0001,audio/u0001.wav,S01,12,1

Outputs in --out:
    features.npz, split.json, model.pt, history.json, metrics.json
    model.onnx, model_float32.tflite, model_int8.tflite
    model_data.cc, model_params.h, golden.npz, parity.json
    report.md
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# Make sibling modules importable when this file is run directly.
HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))


def _split_arg(s: str | None) -> list[str] | None:
    if not s:
        return None
    return [x.strip() for x in s.split(",") if x.strip()]


def _write_report(
    out: Path,
    *,
    data_dir: Path,
    toy: bool,
    samples,
    split_info: dict,
    train_n: int,
    val_n: int,
    x_shape,
    metrics: dict,
    parity: dict | None,
    artifacts: dict,
    passed: bool,
    notes: list[str],
) -> None:
    lines = [
        "# Model pipeline report",
        "",
        f"- data: `{data_dir}`" + ("（合成玩具数据，仅验证流程）" if toy else ""),
        f"- samples: {len(samples)}  (train {train_n}, val {val_n})",
        f"- split: {split_info.get('mode')}  val_speakers={split_info.get('val_speakers')}",
        f"- feature shape: {tuple(x_shape)}  (N, 1, 40, 64)",
        f"- model params: {metrics.get('n_params')}",
        "",
        "## Metrics",
        "",
        f"- accuracy: **{metrics.get('accuracy'):.4f}**",
        f"- precision: {metrics.get('precision'):.4f}",
        f"- recall: {metrics.get('recall'):.4f}",
        f"- F1: {metrics.get('f1'):.4f}",
        f"- ROC-AUC: {metrics.get('roc_auc')}",
        f"- confusion matrix: {metrics.get('confusion_matrix')}",
        f"- best epoch: {metrics.get('best_epoch')}",
        "",
    ]
    if parity:
        lines += ["## Deployment parity", ""]
        for key in ("onnx", "float_tflite", "int8_tflite"):
            d = parity.get(key, {})
            if "error" in d:
                lines.append(f"- {key}: ERROR {d['error']}")
            else:
                lines.append(
                    f"- {key}: max_logit={d.get('max_abs_logit'):.4g}, "
                    f"max_prob={d.get('max_abs_prob', 0):.4g}, "
                    f"label_agreement={d.get('label_agreement'):.4f}, "
                    f"acc={d.get('accuracy', 0):.4f}"
                )
        lines.append("")
    lines += ["## Artifacts", ""]
    for k, v in artifacts.items():
        lines.append(f"- {k}: `{v}`")
    lines += ["", "## Notes", ""]
    lines += [f"- {n}" for n in notes]
    lines += ["", f"**Result: {'PASS' if passed else 'CHECK'}**", ""]
    (out / "report.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description="log-Mel CNN training + TFLite deployment pipeline")
    ap.add_argument("--data-dir", type=Path, default=None, help="directory containing labels.csv")
    ap.add_argument("--toy", action="store_true", help="generate synthetic WAV data and run")
    ap.add_argument("--toy-speakers", type=int, default=6)
    ap.add_argument("--toy-per-speaker", type=int, default=50)
    ap.add_argument("--out", type=Path, default=Path("build/pipeline"))
    ap.add_argument("--epochs", type=int, default=25)
    ap.add_argument("--batch-size", type=int, default=16)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--val-speakers", type=str, default=None, help="comma-separated speaker ids")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--augment", action="store_true")
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--no-convert", action="store_true")
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args()

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    from dataset import (
        class_counts,
        extract_features,
        generate_toy_dataset,
        load_manifest,
        split_by_speaker,
    )
    from train import train_model

    if args.toy:
        data_dir = out / "dataset"
        generate_toy_dataset(
            data_dir,
            n_speakers=args.toy_speakers,
            n_per_speaker=args.toy_per_speaker,
            seed=args.seed,
        )
        toy = True
    elif args.data_dir:
        data_dir = args.data_dir.resolve()
        toy = False
    else:
        raise SystemExit("use --toy or --data-dir")

    samples = load_manifest(data_dir)
    print(f"data: {data_dir}  samples={len(samples)}  labels={class_counts(np.array([s.label for s in samples]))}")

    cache = None if args.no_cache else out / "features.npz"
    x, y, word, speaker = extract_features(samples, cache)
    print(f"features: {x.shape}  labels={class_counts(y)}")

    train_idx, val_idx, split_info = split_by_speaker(
        speaker,
        val_speakers=_split_arg(args.val_speakers),
        pre_split=[s.split for s in samples],
        seed=args.seed,
    )
    # Save the split by utterance id so it can be reproduced.
    split_payload = {
        **split_info,
        "train": [samples[i].utt_id for i in train_idx],
        "val": [samples[i].utt_id for i in val_idx],
    }
    (out / "split.json").write_text(json.dumps(split_payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"split: {split_info['mode']}  train={len(train_idx)} val={len(val_idx)}  val_speakers={split_info.get('val_speakers')}")

    model, metrics, history = train_model(
        x[train_idx], y[train_idx], x[val_idx], y[val_idx],
        out_dir=out,
        epochs=args.epochs,
        batch_size=args.batch_size,
        lr=args.lr,
        augment=args.augment,
        seed=args.seed,
    )

    # Save validation predictions for error analysis.
    from train import predict_proba

    proba = predict_proba(model, x[val_idx])
    pred = proba.argmax(1)
    with (out / "val_predictions.csv").open("w", encoding="utf-8", newline="") as f:
        f.write("utt_id,speaker,word_id,label,p_correct,pred\n")
        for k, i in enumerate(val_idx):
            f.write(
                f"{samples[i].utt_id},{samples[i].speaker_id},{samples[i].word_id},"
                f"{samples[i].label},{proba[k, 1]:.6f},{pred[k]}\n"
            )

    artifacts = {
        "torch": str(out / "model.pt"),
        "metrics": str(out / "metrics.json"),
        "split": str(out / "split.json"),
        "val_predictions": str(out / "val_predictions.csv"),
    }
    parity = None
    if not args.no_convert:
        from convert import convert_all

        print("\n== convert ONNX / TFLite ==")
        conv = convert_all(
            model,
            x[train_idx], x[val_idx], y[val_idx],
            out,
            speaker_val=speaker[val_idx],
        )
        parity = conv["parity"]
        artifacts.update({
            "onnx": conv["onnx"],
            "float_tflite": conv["float_tflite"],
            "int8_tflite": conv["int8_tflite"],
            "model_data.cc": conv["model_data_cc"],
            "model_params.h": conv["model_params_h"],
            "golden.npz": conv["golden_npz"],
            "parity": str(out / "parity.json"),
        })
        print(json.dumps(parity, indent=2, ensure_ascii=False))

    # Pass criteria are intentionally simple; real data may require tuning.
    passed = metrics["accuracy"] >= 0.60
    notes = [
        "toy data is synthetic and only validates the pipeline" if toy else "real data mode",
        f"split mode: {split_info['mode']}",
        "mel-only baseline; word-conditioned model / reference mel is a later experiment",
    ]
    if parity:
        f = parity.get("float_tflite", {})
        q = parity.get("int8_tflite", {})
        if "error" in f or "error" in q:
            passed = False
            notes.append("TFLite conversion or parity failed; inspect parity.json")
        else:
            if f.get("max_abs_logit", 1.0) > 1e-3:
                passed = False
                notes.append("float TFLite differs from PyTorch by >1e-3")
            if q.get("label_agreement", 0.0) < 0.90:
                passed = False
                notes.append("int8 label agreement < 0.90")
            if q.get("max_abs_prob", 1.0) > 0.15:
                passed = False
                notes.append("int8 probability diff > 0.15")
    else:
        notes.append("conversion skipped (--no-convert)")

    _write_report(
        out,
        data_dir=data_dir,
        toy=toy,
        samples=samples,
        split_info=split_info,
        train_n=len(train_idx),
        val_n=len(val_idx),
        x_shape=x.shape,
        metrics=metrics,
        parity=parity,
        artifacts=artifacts,
        passed=passed,
        notes=notes,
    )
    print(f"\nreport: {out / 'report.md'}")
    print(f"result: {'PASS' if passed else 'CHECK'}")
    if args.strict and not passed:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
