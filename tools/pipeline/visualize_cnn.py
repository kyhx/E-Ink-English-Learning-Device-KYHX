#!/usr/bin/env python3
r"""Visualize TinyMelNet activations and Grad-CAM for one real WAV.

Example:
    .\.venv-train\Scripts\python.exe tools\pipeline\visualize_cnn.py ^
        --model build\pipeline_speechocean762\model.pt ^
        --wav build\data\speechocean762\WAVE\SPEAKER9626\096260016.WAV ^
        --out docs\animations\cnn-visual\assets

Outputs:
    mel.png, conv1.png, conv2.png, conv3.png, gradcam.png, meta.json
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from features import extract_file  # noqa: E402
from model import TinyMelNet  # noqa: E402


def _save_grid(tensor, path: Path, cols: int, cell=(300, 190), cmap="magma") -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    arr = tensor[0].cpu().numpy()
    n = arr.shape[0]
    rows = (n + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(cols * cell[0] / 100, rows * cell[1] / 100), dpi=100)
    axes = np.array(axes).reshape(rows, cols)
    for i in range(rows * cols):
        ax = axes[i // cols, i % cols]
        ax.axis("off")
        if i < n:
            ax.imshow(arr[i], origin="lower", aspect="auto", cmap=cmap, interpolation="nearest")
    fig.patch.set_facecolor("#0b1020")
    fig.subplots_adjust(left=.015, right=.985, top=.985, bottom=.015, wspace=.06, hspace=.06)
    fig.savefig(path, facecolor="#0b1020")
    plt.close(fig)


def _save_mel(f: np.ndarray, path: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig = plt.figure(figsize=(9, 4.5), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.axis("off")
    ax.imshow(f, origin="lower", aspect="auto", cmap="magma", interpolation="nearest")
    fig.patch.set_facecolor("#0b1020")
    fig.savefig(path, facecolor="#0b1020")
    plt.close(fig)


def _save_cam(f: np.ndarray, cam: np.ndarray, path: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from PIL import Image

    cam_img = np.array(Image.fromarray((cam * 255).astype(np.uint8)).resize(
        (f.shape[1], f.shape[0]), Image.Resampling.BILINEAR)) / 255.0
    fig = plt.figure(figsize=(9, 4.5), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.axis("off")
    ax.imshow(f, origin="lower", aspect="auto", cmap="magma", interpolation="nearest")
    ax.imshow(cam_img, origin="lower", aspect="auto", cmap="jet", alpha=.5, interpolation="bilinear")
    fig.patch.set_facecolor("#0b1020")
    fig.savefig(path, facecolor="#0b1020")
    plt.close(fig)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", type=Path, default=Path("build/pipeline_speechocean762/model.pt"))
    ap.add_argument("--wav", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    import torch

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    model = TinyMelNet()
    model.load_state_dict(torch.load(args.model, map_location="cpu"))
    model.eval()

    f = extract_file(args.wav)
    x = torch.from_numpy(f[None, None]).float()

    feats: dict[str, torch.Tensor] = {}
    hooks = []
    for idx, name in [(2, "conv1"), (6, "conv2"), (10, "conv3")]:
        def make_hook(n):
            def hook(_m, _i, o):
                feats[n] = o.detach()
            return hook
        hooks.append(model.features[idx].register_forward_hook(make_hook(name)))

    with torch.no_grad():
        logits = model(x)
    for h in hooks:
        h.remove()
    prob = torch.softmax(logits, dim=1)[0].cpu().numpy()
    target = int(logits.argmax(1))

    # Grad-CAM on conv3, target = predicted class.
    acts, grads = [], []
    h1 = model.features[8].register_forward_hook(lambda _m, _i, o: acts.append(o))
    h2 = model.features[8].register_full_backward_hook(lambda _m, _gi, go: grads.append(go[0]))
    xg = x.clone().requires_grad_(True)
    logits_g = model(xg)
    score = logits_g[0, target]
    model.zero_grad()
    score.backward()
    A, G = acts[0].detach(), grads[0].detach()
    w = G.mean(dim=(2, 3), keepdim=True)
    cam = torch.relu((w * A).sum(dim=1)).squeeze().cpu().numpy()
    cam = cam / (cam.max() + 1e-8)
    h1.remove()
    h2.remove()

    _save_mel(f, out / "mel.png")
    _save_grid(feats["conv1"], out / "conv1.png", cols=4)
    _save_grid(feats["conv2"], out / "conv2.png", cols=4)
    _save_grid(feats["conv3"], out / "conv3.png", cols=8, cell=(230, 145))
    _save_cam(f, cam, out / "gradcam.png")

    meta = {
        "wav": str(args.wav),
        "model": str(args.model),
        "p_correct": float(prob[1]),
        "p_incorrect": float(prob[0]),
        "predicted_class": target,
        "feature_shapes": {k: list(v.shape) for k, v in feats.items()},
        "cam_shape": list(cam.shape),
        "note": "Grad-CAM highlights correlated time-frequency regions, not phoneme labels.",
    }
    (out / "meta.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(meta, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
