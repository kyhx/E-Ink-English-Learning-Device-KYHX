#!/usr/bin/env python3
"""Verify the Python 3.12 training/conversion environment.

Run with the project venv:
    .\.venv-train\Scripts\python.exe tools\setup\verify_python.py
Exit code 0 = all required imports/checks pass.
"""
from __future__ import annotations

import importlib
import importlib.metadata as md
import json
import sys
from pathlib import Path

REQUIRED = {
    "torch": "torch",
    "tensorflow": "tensorflow",
    "onnx": "onnx",
    "onnxruntime": "onnxruntime",
    "numpy": "numpy",
    "scipy": "scipy",
    "pandas": "pandas",
    "sklearn": "scikit-learn",
    "matplotlib": "matplotlib",
    "librosa": "librosa",
    "soundfile": "soundfile",
    "yaml": "PyYAML",
    "tqdm": "tqdm",
    "pytest": "pytest",
    "PIL": "Pillow",
    "serial": "pyserial",
    "onnx2tf": "onnx2tf",
    "tf_keras": "tf_keras",
    "fontTools": "fonttools",
}

OPTIONAL = {
    "ai_edge_litert": "ai-edge-litert",
}


def version_of(dist: str) -> str:
    try:
        return md.version(dist)
    except md.PackageNotFoundError:
        return "?"


def main() -> int:
    print(f"Python: {sys.version}")
    print(f"Executable: {sys.executable}")
    print(f"Prefix: {sys.prefix}")
    print()

    failed: list[str] = []
    versions: dict[str, str] = {}

    for module, dist in REQUIRED.items():
        try:
            mod = importlib.import_module(module)
            ver = getattr(mod, "__version__", None) or version_of(dist)
            versions[dist] = ver
            print(f"[ok]   {module:<13} {ver}")
        except Exception as exc:  # noqa: BLE001 - report, don't crash
            failed.append(module)
            print(f"[FAIL] {module:<13} {type(exc).__name__}: {exc}")

    for module, dist in OPTIONAL.items():
        try:
            mod = importlib.import_module(module)
            ver = getattr(mod, "__version__", None) or version_of(dist)
            print(f"[ok]   {module:<13} {ver} (optional)")
        except Exception as exc:  # noqa: BLE001
            print(f"[warn] {module:<13} {type(exc).__name__}: {exc} (optional)")

    print("\n== TFLite interpreter ==")
    interpreter = None
    try:
        from ai_edge_litert.interpreter import Interpreter as LiteInterpreter
        interpreter = LiteInterpreter
        print("[ok]   ai_edge_litert.interpreter.Interpreter")
    except Exception as exc:  # noqa: BLE001
        print(f"[warn] ai_edge_litert interpreter: {exc}")
    if interpreter is None:
        try:
            import tensorflow as tf
            interpreter = tf.lite.Interpreter
            print("[ok]   tensorflow.lite.Interpreter")
        except Exception as exc:  # noqa: BLE001
            print(f"[FAIL] no TFLite interpreter: {exc}")
            failed.append("tflite-interpreter")

    print("\n== fonts ==")
    font_candidates = [
        Path(r"C:\Windows\Fonts\NotoSansSC-VF.ttf"),
        Path(r"C:\Windows\Fonts\msyh.ttc"),
        Path(r"C:\Windows\Fonts\Deng.ttf"),
    ]
    try:
        import matplotlib
        font_candidates.insert(
            0, Path(matplotlib.get_data_path()) / "fonts" / "ttf" / "DejaVuSans.ttf"
        )
    except Exception:
        pass
    for p in font_candidates:
        print(f"[{'ok' if p.exists() else '--'}]   {p}")

    print("\n== serial ports ==")
    try:
        from serial.tools import list_ports
        ports = list(list_ports.comports())
        if ports:
            for p in ports:
                print(f"[ok]   {p.device}  {p.description}")
        else:
            print("[warn] no serial port found (device unplugged?)")
    except Exception as exc:  # noqa: BLE001
        print(f"[warn] pyserial list_ports failed: {exc}")

    print("\n== summary ==")
    print(json.dumps({"versions": versions, "failed": failed}, indent=2))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
