#!/usr/bin/env python3
r"""End-to-end smoke test for the model toolchain.

PyTorch -> ONNX -> TFLite (float32 + full int8) -> LiteRT interpreter.

Run with the training venv:
    .\.venv-train\Scripts\python.exe tools\train\smoke_convert.py

The model is intentionally tiny (1x40x64 mel -> 2 classes) but uses the same
building blocks as the planned student model: Conv2d + BatchNorm + ReLU +
MaxPool + GlobalAvgPool + Linear.  If this passes, the conversion pipeline is
ready for the real model.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

# Windows PowerShell often defaults to GBK; torch.onnx prints emoji.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

MEL_H, MEL_W = 40, 64
N_CLASSES = 2


class TinyMelNet(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 8, 3, padding=1),
            nn.BatchNorm2d(8),
            nn.ReLU(),
            nn.MaxPool2d(2),
            nn.Conv2d(8, 16, 3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(),
            nn.MaxPool2d(2),
        )
        self.head = nn.Sequential(
            nn.AdaptiveAvgPool2d(1),
            nn.Flatten(),
            nn.Linear(16, N_CLASSES),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.head(self.features(x))


def run(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess:
    print("$", " ".join(cmd))
    proc = subprocess.run(
        cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace"
    )
    if proc.returncode != 0:
        print("--- stdout tail ---")
        print("\n".join(proc.stdout.splitlines()[-40:]))
        print("--- stderr tail ---")
        print("\n".join(proc.stderr.splitlines()[-40:]))
    return proc


def tflite_details(path: Path) -> dict:
    from ai_edge_litert.interpreter import Interpreter

    interp = Interpreter(model_path=str(path))
    interp.allocate_tensors()
    inp = interp.get_input_details()[0]
    out = interp.get_output_details()[0]
    return {
        "path": str(path),
        "input_shape": [int(x) for x in inp["shape"]],
        "input_dtype": str(inp["dtype"]),
        "input_quant": [float(x) for x in inp["quantization"]],
        "output_shape": [int(x) for x in out["shape"]],
        "output_dtype": str(out["dtype"]),
        "output_quant": [float(x) for x in out["quantization"]],
    }


def infer_tflite(path: Path, x_nchw: np.ndarray) -> np.ndarray:
    from ai_edge_litert.interpreter import Interpreter

    interp = Interpreter(model_path=str(path))
    interp.allocate_tensors()
    inp = interp.get_input_details()[0]
    out = interp.get_output_details()[0]

    arr = x_nchw.astype(np.float32)
    if len(inp["shape"]) == 4 and int(inp["shape"][-1]) == 1:
        arr = np.transpose(arr, (0, 2, 3, 1))  # NCHW -> NHWC
    arr = np.asarray(arr, dtype=np.float32).reshape(inp["shape"])

    if inp["dtype"] == np.int8:
        scale, zero = inp["quantization"]
        q = np.clip(np.round(arr / scale + zero), -128, 127).astype(np.int8)
        interp.set_tensor(inp["index"], q)
    else:
        interp.set_tensor(inp["index"], arr.astype(inp["dtype"]))

    interp.invoke()
    y = interp.get_tensor(out["index"])
    if out["dtype"] == np.int8:
        scale, zero = out["quantization"]
        y = (y.astype(np.float32) - zero) * scale
    return np.asarray(y, dtype=np.float32).reshape(-1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir", type=Path, default=None)
    ap.add_argument("--no-int8", action="store_true", help="skip int8 conversion")
    args = ap.parse_args()

    work = args.workdir or Path(tempfile.mkdtemp(prefix="eink-conv-"))
    work.mkdir(parents=True, exist_ok=True)
    print(f"workdir: {work}")

    torch.manual_seed(7)
    np.random.seed(7)
    model = TinyMelNet().eval()
    x = torch.randn(1, 1, MEL_H, MEL_W)
    with torch.no_grad():
        y_torch = model(x).numpy().reshape(-1)

    onnx_path = work / "tiny_melnet.onnx"
    torch.onnx.export(
        model,
        x,
        str(onnx_path),
        input_names=["mel"],
        output_names=["logits"],
        opset_version=18,
        do_constant_folding=True,
    )
    print(f"ONNX: {onnx_path} ({onnx_path.stat().st_size} bytes)")

    import onnxruntime as ort

    sess = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    y_onnx = sess.run(None, {"mel": x.numpy()})[0].reshape(-1)
    onnx_diff = float(np.max(np.abs(y_onnx - y_torch)))
    print(f"ONNX vs PyTorch max abs diff: {onnx_diff:.3e}")
    if onnx_diff > 1e-4:
        print("[FAIL] ONNX export does not match PyTorch")
        return 1

    onnx2tf = shutil.which("onnx2tf")
    if onnx2tf is None:
        candidate = Path(sys.executable).with_name("onnx2tf.exe")
        onnx2tf = str(candidate) if candidate.exists() else None
    if onnx2tf is None:
        print("[FAIL] onnx2tf executable not found in the venv")
        return 1

    float_dir = work / "tflite_float"
    proc = run(
        [
            onnx2tf,
            "-i",
            str(onnx_path),
            "-o",
            str(float_dir),
            # Keep the NCHW input layout: 40x64 mel is not an RGB image and
            # onnx2tf's automatic NHWC layout rewrite breaks GlobalAvgPool.
            "-kat",
            "mel",
            "-n",
        ],
        cwd=work,
    )
    if proc.returncode != 0:
        print("[FAIL] float TFLite conversion failed")
        return 1

    floats = sorted(float_dir.rglob("*float32*.tflite"))
    if not floats:
        floats = sorted(float_dir.rglob("*.tflite"))
    if not floats:
        print(f"[FAIL] no float TFLite generated under {float_dir}")
        return 1
    float_tflite = floats[0]
    print(f"float TFLite: {float_tflite} ({float_tflite.stat().st_size} bytes)")
    print(json.dumps(tflite_details(float_tflite), indent=2))
    y_float = infer_tflite(float_tflite, x.numpy())
    float_diff = float(np.max(np.abs(y_float - y_torch)))
    print(f"float TFLite vs PyTorch max abs diff: {float_diff:.3e}")
    if float_diff > 5e-3:
        print("[FAIL] float TFLite output mismatch")
        return 1

    if args.no_int8:
        print("[ok] float path ready (int8 skipped)")
        return 0

    # Calibration data stays NCHW because we pass -kat mel above.
    calib = np.random.rand(16, 1, MEL_H, MEL_W).astype(np.float32)
    calib_path = work / "calib_nchw.npy"
    np.save(calib_path, calib)

    int8_dir = work / "tflite_int8"
    proc = run(
        [
            onnx2tf,
            "-i",
            str(onnx_path),
            "-o",
            str(int8_dir),
            "-kat",
            "mel",
            "-oiqt",
            "-cind",
            "mel",
            str(calib_path),
            "0.0",
            "1.0",
            "-n",
        ],
        cwd=work,
    )
    if proc.returncode != 0:
        print("[FAIL] int8 TFLite conversion failed")
        return 1

    int8s = sorted(int8_dir.rglob("*integer_quant*.tflite"))
    if not int8s:
        int8s = sorted(int8_dir.rglob("*.tflite"))
    if not int8s:
        print(f"[FAIL] no int8 TFLite generated under {int8_dir}")
        return 1
    # Prefer full integer quant (int8 in/out) if multiple files exist.
    int8_tflite = next((p for p in int8s if "full" in p.name), int8s[0])
    print(f"int8 TFLite: {int8_tflite} ({int8_tflite.stat().st_size} bytes)")
    print(json.dumps(tflite_details(int8_tflite), indent=2))
    y_int8 = infer_tflite(int8_tflite, x.numpy())
    int8_diff = float(np.max(np.abs(y_int8 - y_torch)))
    print(f"int8 TFLite vs PyTorch max abs diff (logits): {int8_diff:.3e}")
    print(f"argmax torch={int(np.argmax(y_torch))} int8={int(np.argmax(y_int8))}")

    def softmax(v: np.ndarray) -> np.ndarray:
        e = np.exp(v - np.max(v))
        return e / e.sum()

    prob_diff = float(np.max(np.abs(softmax(y_int8) - softmax(y_torch))))
    print(f"int8 vs PyTorch max abs diff (softmax): {prob_diff:.3e}")
    if int(np.argmax(y_torch)) != int(np.argmax(y_int8)):
        print("[FAIL] int8 label mismatch")
        return 1
    if prob_diff > 0.10:
        print("[WARN] int8 probability differs by more than 0.10; check calibration/quantization")
        return 1

    print("\n[PASS] PyTorch -> ONNX -> TFLite float32 + int8 -> LiteRT")
    print(f"artifacts kept in: {work}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
