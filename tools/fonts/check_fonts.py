#!/usr/bin/env python3
r"""Check that the chosen TTF/TTC files cover the IPA and Chinese glyphs we need.

Run with the training venv:
    .\.venv-train\Scripts\python.exe tools\fonts\check_fonts.py
"""
from __future__ import annotations

import sys
from pathlib import Path

from fontTools.ttLib import TTFont

# Individual IPA code points used by the planned word bank / phonetics.
IPA = "ˈˌːəɪʊæɑʌɔɜɚɝɛɒœøɹɾʔθðʃʒŋɡʧʤɐɞɘɵɤɯɨʉɯ"
# A representative Chinese sample; the real coverage check will load words.csv.
CJK = "苹果想认为学习英语发音正确错误跟读复习进度同步电池录音推理提示保存设置开始结束"

FONTS = {
    "IPA (DejaVuSans)": Path(
        r"C:\venv-eink\Lib\site-packages\matplotlib\mpl-data\fonts\ttf\DejaVuSans.ttf"
    ),
    "CJK (Noto Sans SC)": Path(r"C:\Windows\Fonts\NotoSansSC-VF.ttf"),
}


def cmap_for(path: Path) -> set[int]:
    # fontNumber=0 is correct for .ttc; harmless for .ttf.
    font = TTFont(str(path), fontNumber=0, lazy=True)
    cmap = font.getBestCmap() or {}
    return set(cmap.keys())


def main() -> int:
    failed = False
    for label, path in FONTS.items():
        print(f"\n== {label} ==")
        print(path)
        if not path.exists():
            print("[FAIL] file not found")
            failed = True
            continue
        cmap = cmap_for(path)
        text = IPA if "IPA" in label else CJK
        missing = sorted({ch for ch in text if ord(ch) not in cmap})
        print(f"glyphs requested: {len(set(text))}, missing: {len(missing)}")
        if missing:
            print("missing:", " ".join(missing))
            failed = True
        else:
            print("[ok] all requested glyphs present")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
