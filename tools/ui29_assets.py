#!/usr/bin/env python3
"""生成 2.9" 单词卡的资源头文件。

产物：
  main/ui29/ui29_words.h   —— 50 词的 word / ipa / meaning 字符串表
  main/ui29/ui29_font24.h  —— 汉字 24x24 点阵子集（菜单/释义）
  main/ui29/ui29_font14.h  —— IPA / ASCII 14x24 点阵子集（基线对齐）

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

# 汉字字身
CJK_CELL = 24
CJK_SIZE = 24
CJK_DY = 1

# IPA 字身：14x24，基线对齐
IPA_CELL_W = 14
IPA_CELL_H = 24
IPA_ADVANCE = 12
IPA_SIZE = 16
IPA_BASELINE = 34      # 44 px 高的大画布里的基线
IPA_CANVAS_H = 44
IPA_CROP_TOP = 12

# 界面上固定出现的汉字（菜单/标题/提示/状态）。
# 改文案时同步改这里，生成器会把用到的字全部打进字库。
UI_LABELS = [
    "主菜单", "单词", "复习日历", "同步", "设置", "蓝牙", "无线网络",
    "返回", "开启", "关闭", "未启用", "本月", "今日",
    "就绪", "录音中", "已保存", "评分待接入", "按录音键跟读",
    "正在录音", "错误", "请重试", "无语音", "保存失败",
    "日一二三四五六", "年", "月", "无记录", "已复习",
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


def build_cjk24(chars):
    font = ImageFont.truetype(CJK_TTF, CJK_SIZE)
    table = {}
    for ch in sorted(set(chars)):
        table[ch] = render_glyph(font, ch, CJK_CELL, CJK_CELL, dx=0, dy=CJK_DY)
    return table


def build_ipa14(chars):
    """IPA/ASCII 14x24，基线对齐；先画在大画布上再裁，避免变音符被切。"""
    font = ImageFont.truetype(IPA_TTF, IPA_SIZE)
    table = {}
    for ch in sorted(set(chars)):
        big = render_glyph(font, ch, IPA_CELL_W, IPA_CANVAS_H,
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


def emit_font24(table):
    lines = [
        "/* 由 tools/ui29_assets.py 生成，不要手改。",
        " * 汉字 24x24 点阵子集（SimHei 24px，阈值 128）。",
        " * 每个字模 24 个 uint32：一行一个，bit23 = 最左像素。",
        " * 点阵由本机系统字体生成，仅用于本机显示与论文附图。",
        " * SPDX-License-Identifier: MIT",
        " */",
        "#ifndef UI29_FONT24_H",
        "#define UI29_FONT24_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint16_t cp;           /* Unicode 码位 */",
        "    uint32_t bmp[24];      /* 24 行，bit23 = 最左 */",
        "} ui29_glyph24_t;",
        "",
        "static const ui29_glyph24_t k_font24[] = {",
    ]
    for ch in sorted(table):
        body = ",".join("0x%06X" % r for r in table[ch])
        lines.append("    {0x%04X, {%s}}," % (ord(ch), body))
    lines += [
        "};",
        "",
        "#define UI29_FONT24_COUNT ((int)(sizeof(k_font24) / sizeof(k_font24[0])))",
        "",
        "#endif /* UI29_FONT24_H */",
    ]
    return write_file("ui29_font24.h", lines)


def emit_font14(table):
    lines = [
        "/* 由 tools/ui29_assets.py 生成，不要手改。",
        " * IPA / ASCII 14x24 点阵子集（Segoe UI 16px，基线对齐，阈值 128）。",
        " * 每个字模 24 个 uint16：一行一个，bit13 = 最左；步进 12 px。",
        " * SPDX-License-Identifier: MIT",
        " */",
        "#ifndef UI29_FONT14_H",
        "#define UI29_FONT14_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint16_t cp;",
        "    uint16_t bmp[24];",
        "} ui29_glyph14_t;",
        "",
        "static const ui29_glyph14_t k_font14[] = {",
    ]
    for ch in sorted(table):
        body = ",".join("0x%04X" % r for r in table[ch])
        lines.append("    {0x%04X, {%s}}," % (ord(ch), body))
    lines += [
        "};",
        "",
        "#define UI29_FONT14_COUNT ((int)(sizeof(k_font14) / sizeof(k_font14[0])))",
        "",
        "#endif /* UI29_FONT14_H */",
    ]
    return write_file("ui29_font14.h", lines)


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

    f24 = build_cjk24([c for c in cjk if ord(c) > 0x7F])
    f14 = build_ipa14([c for c in ipa_chars if ord(c) >= 0x20])

    os.makedirs(OUT_DIR, exist_ok=True)
    print("words :", emit_words(words), len(words), "words")
    print("font24:", emit_font24(f24), len(f24), "glyphs")
    print("font14:", emit_font14(f14), len(f14), "glyphs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
