#!/usr/bin/env python3
r"""ONNX -> TFLite float/int8 conversion, parity check, and C export.

The converter deliberately keeps the TFLite input in NCHW [1,1,40,64] with
`onnx2tf -kat mel`.  C code must fill the input in the same order.

Outputs:
    model.onnx
    tflite_float/model_float32.tflite
    tflite_int8/model_full_integer_quant.tflite
    model_data.cc
    model_params.h
    golden.npz
    parity.json
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np


def _softmax(v: np.ndarray) -> np.ndarray:
    v = np.asarray(v, dtype=np.float32).reshape(-1)
    e = np.exp(v - np.max(v))
    return e / max(1e-12, e.sum())


def _onnx2tf_exe() -> str:
    exe = shutil.which("onnx2tf")
    if exe:
        return exe
    candidate = Path(sys.executable).with_name("onnx2tf.exe")
    if candidate.exists():
        return str(candidate)
    raise FileNotFoundError("onnx2tf not found in the current venv")


def export_onnx(model, path: str | Path) -> Path:
    import torch

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    model.eval()
    dummy = torch.zeros(1, 1, 40, 64)
    torch.onnx.export(
        model,
        dummy,
        str(path),
        input_names=["mel"],
        output_names=["logits"],
        opset_version=18,
        do_constant_folding=True,
        external_data=False,
    )
    return path


def _run(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess:
    print("$", " ".join(cmd))
    proc = subprocess.run(
        cmd,
        cwd=str(cwd),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        print("--- onnx2tf stdout tail ---")
        print("\n".join(proc.stdout.splitlines()[-30:]))
        print("--- onnx2tf stderr tail ---")
        print("\n".join(proc.stderr.splitlines()[-30:]))
    return proc


def run_onnx2tf(
    onnx_path: str | Path,
    out_dir: str | Path,
    *,
    int8: bool,
    calib_path: str | Path | None = None,
) -> Path:
    exe = _onnx2tf_exe()
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    cmd = [exe, "-i", str(onnx_path), "-o", str(out), "-kat", "mel", "-n"]
    if int8:
        if calib_path is None:
            raise ValueError("int8 conversion needs a calibration .npy")
        cmd += ["-oiqt", "-cind", "mel", str(calib_path), "0.0", "1.0"]
    proc = _run(cmd, out.parent)
    if proc.returncode != 0:
        raise RuntimeError("onnx2tf failed")
    return out


def find_tflite(out_dir: str | Path, *, int8: bool) -> Path:
    out = Path(out_dir)
    files = sorted(out.rglob("*.tflite"))
    if not files:
        raise FileNotFoundError(f"no .tflite under {out}")
    if int8:
        for p in files:
            if "full_integer_quant" in p.name:
                return p
        for p in files:
            if "integer_quant" in p.name and "int16" not in p.name:
                return p
    else:
        for p in files:
            if "float32" in p.name:
                return p
    return files[0]


class TFLiteRunner:
    def __init__(self, path: str | Path) -> None:
        from ai_edge_litert.interpreter import Interpreter

        self.path = Path(path)
        self.interp = Interpreter(model_path=str(self.path))
        self.interp.allocate_tensors()
        self.inp = self.interp.get_input_details()[0]
        self.out = self.interp.get_output_details()[0]
        self.input_shape = [int(v) for v in self.inp["shape"]]
        self.output_shape = [int(v) for v in self.out["shape"]]

    def details(self) -> dict:
        return {
            "path": str(self.path),
            "input_shape": self.input_shape,
            "input_dtype": str(self.inp["dtype"]),
            "input_quant": [float(v) for v in self.inp["quantization"]],
            "output_shape": self.output_shape,
            "output_dtype": str(self.out["dtype"]),
            "output_quant": [float(v) for v in self.out["quantization"]],
        }

    def predict_logits(self, x: np.ndarray) -> np.ndarray:
        """x: [N,1,40,64] float32. Returns [N,2] float logits."""
        x = np.asarray(x, dtype=np.float32)
        outs = []
        for i in range(len(x)):
            arr = x[i:i + 1]
            # -kat mel keeps NCHW; handle a possible NHWC model gracefully.
            if len(self.input_shape) == 4 and self.input_shape[-1] == 1:
                arr = np.transpose(arr, (0, 2, 3, 1))
            arr = np.asarray(arr, dtype=np.float32).reshape(self.input_shape)

            if self.inp["dtype"] == np.int8:
                scale, zero = self.inp["quantization"]
                q = np.clip(np.round(arr / scale + zero), -128, 127).astype(np.int8)
                self.interp.set_tensor(self.inp["index"], q)
            else:
                self.interp.set_tensor(self.inp["index"], arr.astype(self.inp["dtype"]))

            self.interp.invoke()
            y = self.interp.get_tensor(self.out["index"])
            if self.out["dtype"] == np.int8:
                scale, zero = self.out["quantization"]
                y = (y.astype(np.float32) - zero) * scale
            outs.append(np.asarray(y, dtype=np.float32).reshape(-1))
        return np.stack(outs, axis=0)


def torch_logits(model, x: np.ndarray) -> np.ndarray:
    import torch

    model.eval()
    outs = []
    with torch.no_grad():
        for i in range(0, len(x), 64):
            xb = torch.from_numpy(x[i:i + 64])
            outs.append(model(xb).cpu().numpy())
    return np.concatenate(outs, axis=0)


def onnx_logits(onnx_path: str | Path, x: np.ndarray) -> np.ndarray:
    import onnxruntime as ort

    sess = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    # The exported ONNX graph has a fixed batch of 1; loop for parity.
    outs = []
    for i in range(len(x)):
        outs.append(sess.run(None, {"mel": x[i:i + 1]})[0])
    return np.concatenate(outs, axis=0)


def parity_check(
    model,
    onnx_path: str | Path,
    float_tflite: str | Path,
    int8_tflite: str | Path,
    x: np.ndarray,
    y: np.ndarray,
) -> dict:
    ref = torch_logits(model, x)
    ref_prob = np.stack([_softmax(v) for v in ref], axis=0)
    ref_pred = ref.argmax(1)
    out = {"n": int(len(x)), "ref_accuracy": float(np.mean(ref_pred == y))}

    try:
        on = onnx_logits(onnx_path, x)
        out["onnx"] = {
            "max_abs_logit": float(np.max(np.abs(on - ref))),
            "mean_abs_logit": float(np.mean(np.abs(on - ref))),
            "label_agreement": float(np.mean(on.argmax(1) == ref_pred)),
            "accuracy": float(np.mean(on.argmax(1) == y)),
        }
    except Exception as exc:  # noqa: BLE001
        out["onnx"] = {"error": str(exc)}

    for name, path in (("float_tflite", float_tflite), ("int8_tflite", int8_tflite)):
        try:
            r = TFLiteRunner(path)
            pred = r.predict_logits(x)
            prob = np.stack([_softmax(v) for v in pred], axis=0)
            out[name] = {
                "max_abs_logit": float(np.max(np.abs(pred - ref))),
                "mean_abs_logit": float(np.mean(np.abs(pred - ref))),
                "max_abs_prob": float(np.max(np.abs(prob - ref_prob))),
                # agreement with the PyTorch predictions, not with ground truth
                "label_agreement": float(np.mean(pred.argmax(1) == ref_pred)),
                "accuracy": float(np.mean(pred.argmax(1) == y)),
                "details": r.details(),
            }
        except Exception as exc:  # noqa: BLE001
            out[name] = {"error": str(exc)}
    return out


def export_c_array(tflite_path: str | Path, out_cc: str | Path, symbol: str = "g_model") -> Path:
    data = Path(tflite_path).read_bytes()
    out = Path(out_cc)
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "// Auto-generated by tools/pipeline/convert.py. Do not edit by hand.",
        "#include <stdint.h>",
        '#include "model_params.h"',
        "",
        f"alignas(16) const unsigned char {symbol}[] = {{",
    ]
    for i in range(0, len(data), 12):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
        lines.append(f"    {chunk},")
    lines += [
        "};",
        f"const unsigned int {symbol}_len = sizeof({symbol});",
        "",
    ]
    out.write_text("\n".join(lines), encoding="utf-8")
    return out


def export_model_params(runner: TFLiteRunner, out_h: str | Path) -> Path:
    d = runner.details()
    in_scale, in_zero = d["input_quant"]
    out_scale, out_zero = d["output_quant"]
    text = f"""// Auto-generated by tools/pipeline/convert.py. Do not edit by hand.
#pragma once
#include <stdint.h>

#define MODEL_INPUT_H 40
#define MODEL_INPUT_W 64
#define MODEL_INPUT_C 1
#define MODEL_INPUT_NCHW 1
#define MODEL_INPUT_SCALE {in_scale:.12g}f
#define MODEL_INPUT_ZERO {int(in_zero)}
#define MODEL_OUTPUT_SCALE {out_scale:.12g}f
#define MODEL_OUTPUT_ZERO {int(out_zero)}
#define MODEL_INPUT_SHAPE "{{1,1,40,64}}"
#define MODEL_OUTPUT_SHAPE "{{1,2}}"
#define MODEL_NUM_CLASSES 2
#define MODEL_CLASS_0 "incorrect"
#define MODEL_CLASS_1 "correct"
"""
    out = Path(out_h)
    out.write_text(text, encoding="utf-8")
    return out


def export_golden(
    path: str | Path,
    x: np.ndarray,
    y: np.ndarray,
    ref_logits: np.ndarray,
    int8_logits: np.ndarray | None,
    speaker: np.ndarray | None = None,
) -> Path:
    payload = {
        "x": np.asarray(x, dtype=np.float32),
        "y": np.asarray(y, dtype=np.int64),
        "ref_logits": np.asarray(ref_logits, dtype=np.float32),
        "ref_probs": np.stack([_softmax(v) for v in ref_logits], axis=0).astype(np.float32),
    }
    if int8_logits is not None:
        payload["int8_logits"] = np.asarray(int8_logits, dtype=np.float32)
        payload["int8_probs"] = np.stack([_softmax(v) for v in int8_logits], axis=0).astype(np.float32)
    if speaker is not None:
        payload["speaker"] = np.asarray(speaker)
    out = Path(path)
    np.savez_compressed(out, **payload)
    return out


def convert_all(
    model,
    x_calib: np.ndarray,
    x_val: np.ndarray,
    y_val: np.ndarray,
    out_dir: str | Path,
    *,
    speaker_val: np.ndarray | None = None,
) -> dict:
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    calib = np.asarray(x_calib[:200], dtype=np.float32)
    calib_path = out / "calib_nchw.npy"
    np.save(calib_path, calib)

    onnx_path = export_onnx(model, out / "model.onnx")
    float_dir = run_onnx2tf(onnx_path, out / "tflite_float", int8=False)
    int8_dir = run_onnx2tf(
        onnx_path, out / "tflite_int8", int8=True, calib_path=calib_path
    )
    float_tflite = find_tflite(float_dir, int8=False)
    int8_tflite = find_tflite(int8_dir, int8=True)

    # Stable copies for scripts/docs.
    shutil.copy2(float_tflite, out / "model_float32.tflite")
    shutil.copy2(int8_tflite, out / "model_int8.tflite")

    parity = parity_check(model, onnx_path, float_tflite, int8_tflite, x_val, y_val)
    (out / "parity.json").write_text(json.dumps(parity, indent=2), encoding="utf-8")

    int8_runner = TFLiteRunner(int8_tflite)
    export_c_array(out / "model_int8.tflite", out / "model_data.cc")
    export_model_params(int8_runner, out / "model_params.h")

    ref = torch_logits(model, x_val)
    int8_logits = int8_runner.predict_logits(x_val)
    export_golden(out / "golden.npz", x_val, y_val, ref, int8_logits, speaker_val)

    return {
        "onnx": str(onnx_path),
        "float_tflite": str(out / "model_float32.tflite"),
        "int8_tflite": str(out / "model_int8.tflite"),
        "model_data_cc": str(out / "model_data.cc"),
        "model_params_h": str(out / "model_params.h"),
        "golden_npz": str(out / "golden.npz"),
        "parity": parity,
        "int8_details": int8_runner.details(),
        "calib": str(calib_path),
    }
