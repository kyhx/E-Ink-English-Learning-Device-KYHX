#!/usr/bin/env python3
"""生成 2.9" 单词卡的资源头文件。

产物：
  main/ui29/ui29_words.h   —— 50 词的 word / ipa / meaning 字符串表
  main/ui29/ui29_font16.h  —— 汉字 16x16 点阵子集（只含用到的字）
  main/ui29/ui29_font12.h  —— IPA / ASCII 10x14 点阵子集（基线对齐）

布局预览见 tools/ui29_preview.py。

用法：
    .venv-train\\Scripts\\python.exe tools/ui29_assets.py
"""
from __future__ import annotations

import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
WORDS = os.path.join(ROOT, "data", "words.csv")
OUT_DIR = os.path.join(ROOT, "main", "ui29")

CJK_TTF = "C:/Windows/Fonts/simhei.ttf"
IPA_TTF = "C:/Windows/Fonts/segoeui.ttf"

W, H = 128, 296

IPA_CELL_W = 12
IPA_CELL_H = 20
IPA_ADVANCE = 10

# 界面上固定出现的汉字（标题/提示/状态）。改文案时同步改这里，
# 生成器会把用到的字全部打进字库，C 端只按码位查表。
UI_LABELS = [
    "单元", "单词学习", "就绪", "录音中", "已保存", "评分待接入",
    "按录音键跟读", "按播放键听发音", "复习完成", "今日", "连续",
    "同步", "无线同步", "无网络", "错误", "请重试", "还剩秒",
    "按同步键退出", "按任意键继续", "答对", "答错", "正在录音",
]


def load_words():
    rows = []
    with open(WORDS, encoding="utf-8") as f:
        header = f.readline()
        assert header.startswith("word_id,word,meaning,ipa"), header
        for line in f:
            line = line.strip()
            if not line:
                continue
            wid, word, meaning, ipa = line.split(",")[:4]
            rows.append((word, ipa, meaning))
    return rows


def render_glyph(font, ch, cell_w, cell_h, dx=0, dy=0, anchor=None):
    """把单个字符渲染进 cell_w x cell_h 的 1bpp 位图（返回行位图列表）。"""
    img = Image.new("L", (cell_w, cell_h), 255)
    d = ImageDraw.Draw(img)
    if anchor:
        d.text((dx, dy), ch, font=font, fill=0, anchor=anchor)
    else:
        d.text((dx, dy), ch, font=font, fill=0)
    rows = []
    for y in range(cell_h):
        bits = 0
        for x in range(cell_w):
            if img.getpixel((x, y)) < 128:
                bits |= 1 << (cell_w - 1 - x)
        rows.append(bits)
    return rows


def build_cjk(chars):
    font = ImageFont.truetype(CJK_TTF, 16)
    table = {}
    for ch in sorted(set(chars)):
        table[ch] = render_glyph(font, ch, 16, 16, dx=0, dy=1)
    return table


IPA_BASELINE = 28      # 大画布里的基线位置
IPA_CROP_TOP = 11      # 从第 11 行开始裁，基线落在裁后第 17 行，上方留 11 px 给变音符


def build_ipa(chars):
    """IPA/ASCII 统一字身、基线对齐。

    先画在 32 px 高的大画布上（基线在 26），再裁出 [12, 12+IPA_CELL_H)，
    这样 ˈ ː 之类的上标/长音符不会被切掉。
    """
    font = ImageFont.truetype(IPA_TTF, 14)
    table = {}
    for ch in sorted(set(chars)):
        big = render_glyph(font, ch, IPA_CELL_W, 36,
                           dx=1, dy=IPA_BASELINE, anchor="ls")
        table[ch] = big[IPA_CROP_TOP:IPA_CROP_TOP + IPA_CELL_H]
    return table


def write_file(name, lines):
    path = os.path.join(OUT_DIR, name)
    text = "\n".join(lines) + "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return path


def emit_words(words):
    lines = [
        "/* 由 tools/ui29_assets.py 生成，不要手改。",
        " * 数据来源：data/words.csv（50 词，含 IPA）。",
        " * SPDX-License-Identifier: MIT",
        " */",
        "#ifndef UI29_WORDS_H",
        "#define UI29_WORDS_H",
        "",
        "typedef struct {",
        "    const char *word;",
        "    const char *ipa;",
        "    const char *meaning;",
        "} ui29_word_t;",
        "",
        "static const ui29_word_t k_words29[] = {",
    ]
    for word, ipa, meaning in words:
        lines.append('    {"%s", "%s", "%s"},' % (word, ipa, meaning))
    lines += [
        "};",
        "",
        "#define UI29_WORD_COUNT ((int)(sizeof(k_words29) / sizeof(k_words29[0])))",
        "",
        "#endif /* UI29_WORDS_H */",
    ]
    return write_file("ui29_words.h", lines)


def emit_font16(table):
    lines = [
        "/* 由 tools/ui29_assets.py 生成，不要手改。",
        " * 汉字 16x16 点阵子集（SimHei 16px，阈值 128）。",
        " * 每个字模 32 字节：16 行，每行 2 字节，高位在左。",
        " * 点阵由本机系统字体生成，仅用于本机显示与论文附图。",
        " * SPDX-License-Identifier: MIT",
        " */",
        "#ifndef UI29_FONT16_H",
        "#define UI29_FONT16_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint16_t cp;        /* Unicode 码位 */",
        "    uint8_t  bmp[32];   /* 16 行 x 2 字节，MSB 在左 */",
        "} ui29_glyph16_t;",
        "",
        "static const ui29_glyph16_t k_font16[] = {",
    ]
    for ch in sorted(table):
        hexes = []
        for r in table[ch]:
            hexes.append("0x%02X,0x%02X" % (r >> 8, r & 0xFF))
        lines.append("    {0x%04X, {%s}}," % (ord(ch), ",".join(hexes)))
    lines += [
        "};",
        "",
        "#define UI29_FONT16_COUNT ((int)(sizeof(k_font16) / sizeof(k_font16[0])))",
        "",
        "#endif /* UI29_FONT16_H */",
    ]
    return write_file("ui29_font16.h", lines)


def emit_font12(table):
    lines = [
        "/* 由 tools/ui29_assets.py 生成，不要手改。",
        " * IPA / ASCII 12x20 点阵子集（Segoe UI 14px，基线对齐，阈值 128）。",
        " * 每个字模 40 字节：20 行，每行 2 字节（低 12 位有效），高位在左；步进 10 px。",
        " * SPDX-License-Identifier: MIT",
        " */",
        "#ifndef UI29_FONT12_H",
        "#define UI29_FONT12_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint16_t cp;",
        "    uint16_t bmp[20];",
        "} ui29_glyph12_t;",
        "",
        "static const ui29_glyph12_t k_font12[] = {",
    ]
    for ch in sorted(table):
        body = ",".join("0x%04X" % r for r in table[ch])
        lines.append("    {0x%04X, {%s}}," % (ord(ch), body))
    lines += [
        "};",
        "",
        "#define UI29_FONT12_COUNT ((int)(sizeof(k_font12) / sizeof(k_font12[0])))",
        "",
        "#endif /* UI29_FONT12_H */",
    ]
    return write_file("ui29_font12.h", lines)


def ui_cjk_chars(words):
    chars = set()
    for label in UI_LABELS:
        chars.update(label)
    for _, _, meaning in words:
        chars.update(meaning)
    return chars


def main():
    words = load_words()
    cjk = ui_cjk_chars(words)
    ipa_chars = set()
    for _, ipa, _ in words:
        ipa_chars.update(ipa)

    f16 = build_cjk([c for c in cjk if ord(c) > 0x7F])
    f12 = build_ipa([c for c in ipa_chars if ord(c) >= 0x20])

    os.makedirs(OUT_DIR, exist_ok=True)
    p1 = emit_words(words)
    p2 = emit_font16(f16)
    p3 = emit_font12(f12)

    print("words :", p1, len(words), "words")
    print("font16:", p2, len(f16), "glyphs")
    print("font12:", p3, len(f12), "glyphs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
