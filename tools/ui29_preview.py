#!/usr/bin/env python3
"""渲染 2.9" 单词卡 UI 预览（128x296），像素级复刻 C 端布局。

用途：烧录前先看清排版、字号、留白，避免反复刷屏。

用法：
    .venv-train\\Scripts\\python.exe tools/ui29_preview.py
产物：
    docs/ui29_preview.png   五个页面横向拼接，放大 3 倍
"""
from __future__ import annotations

import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ui29_assets as A  # noqa: E402
from font5x7_ascii import GLYPHS  # noqa: E402
from ui29_assets import H, W  # noqa: E402

# ------------------------------------------------------------------ 布局常量
# 这些数值直接照抄进 main/ui29/ui29_card.c，改布局时两边一起改。
MARGIN = 4
INNER = W - 2 * MARGIN          # 120
Y_SBAR = 2                      # 状态栏文字顶
Y_DIV1 = 12                     # 状态栏下分隔线
Y_WORD = 24                     # 单词顶（scale 4 -> 32 px 高）
WORD_SCALE = 4
Y_IPA = 60                      # 音标顶（20 px 框，基线对齐）
Y_DIV2 = 84
Y_MEAN = 92                     # 释义顶（16 px，行距 20）
MEAN_LINE_H = 20
Y_BOX = 148                     # 状态框
BOX_H = 92
Y_BAR = 254                     # 进度条
BAR_H = 6
Y_HINT1 = 268
Y_HINT2 = 278


class Panel:
    """1bpp 画布，坐标系与 C 端 epd1680_fb_* 一致。"""

    def __init__(self):
        self.img = Image.new("L", (W, H), 255)
        self.d = ImageDraw.Draw(self.img)

    # ---------------------------------------------------------- 基本图元
    def px(self, x, y, color=1):
        if 0 <= x < W and 0 <= y < H:
            self.img.putpixel((x, y), 0 if color else 255)

    def hline(self, x, y, w, color=1):
        for i in range(w):
            self.px(x + i, y, color)

    def vline(self, x, y, h, color=1):
        for i in range(h):
            self.px(x, y + i, color)

    def rect(self, x, y, w, h, color=1):
        self.hline(x, y, w, color)
        self.hline(x, y + h - 1, w, color)
        self.vline(x, y, h, color)
        self.vline(x + w - 1, y, h, color)

    def fill_rect(self, x, y, w, h, color=1):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=0 if color else 255)

    def dash_hline(self, x, y, w, on=3, off=2):
        i = 0
        while i < w:
            self.hline(x + i, y, min(on, w - i))
            i += on + off

    # ---------------------------------------------------------- 5x7 ASCII
    def ascii(self, x, y, text, scale=1):
        cx = x
        for ch in text:
            idx = ord(ch) - 0x20
            if not (0 <= idx < len(GLYPHS)):
                idx = ord("?") - 0x20
            glyph = GLYPHS[idx]
            for col in range(5):
                bits = glyph[col]
                for row in range(8):
                    if bits & (1 << row):
                        if scale == 1:
                            self.px(cx + col, y + row)
                        else:
                            self.fill_rect(cx + col * scale, y + row * scale,
                                           scale, scale)
            cx += 5 * scale
        return cx - x

    def ascii_width(self, text, scale=1):
        return len(text) * 5 * scale

    # ---------------------------------------------------------- 16x16 汉字
    def glyph16(self, x, y, ch, table):
        rows = table.get(ch)
        if rows is None:
            return
        for j, bits in enumerate(rows):
            for i in range(16):
                if bits & (1 << (15 - i)):
                    self.px(x + i, y + j)

    def text16(self, x, y, text, table):
        cx = x
        for ch in text:
            self.glyph16(cx, y, ch, table)
            cx += 16
        return cx - x

    # ---------------------------------------------------------- 10x14 IPA
    def glyph12(self, x, y, ch, table):
        rows = table.get(ch)
        if rows is None:
            return
        for j, bits in enumerate(rows):
            for i in range(A.IPA_CELL_W):
                if bits & (1 << (A.IPA_CELL_W - 1 - i)):
                    self.px(x + i, y + j)

    def text12(self, x, y, text, table):
        cx = x
        for ch in text:
            self.glyph12(cx, y, ch, table)
            cx += A.IPA_ADVANCE
        return cx - x

    def text12_width(self, text):
        return len(text) * A.IPA_ADVANCE


# ------------------------------------------------------------------ 页面
def status_bar(p: Panel, left: str, right: str):
    p.ascii(MARGIN, Y_SBAR, left)
    p.ascii(W - MARGIN - p.ascii_width(right), Y_SBAR, right)
    p.hline(MARGIN, Y_DIV1, INNER)


def word_block(p: Panel, word: str, ipa: str, f12):
    w = p.ascii_width(word, WORD_SCALE)
    p.ascii((W - w) // 2, Y_WORD, word, WORD_SCALE)

    iw = p.text12_width(ipa)
    p.text12((W - iw) // 2, Y_IPA, ipa, f12)
    p.hline(MARGIN, Y_DIV2, INNER)


def meaning_block(p: Panel, meaning: str, f16):
    lines = [meaning[i:i + 7] for i in range(0, len(meaning), 7)][:2]
    y = Y_MEAN
    for line in lines:
        p.text16((W - len(line) * 16) // 2, y, line, f16)
        y += MEAN_LINE_H


def status_box(p: Panel, big: str, small: str, f16):
    p.rect(MARGIN, Y_BOX, INNER, BOX_H)
    p.text16((W - len(big) * 16) // 2, Y_BOX + 26, big, f16)
    if small:
        p.text16((W - len(small) * 16) // 2, Y_BOX + 56, small, f16)


def progress(p: Panel, idx: int, total: int):
    p.rect(MARGIN, Y_BAR, INNER, BAR_H)
    fill_w = int((INNER - 4) * (idx + 1) / total)
    if fill_w > 0:
        p.fill_rect(MARGIN + 2, Y_BAR + 2, fill_w, BAR_H - 4)


def footer(p: Panel):
    p.ascii(MARGIN, Y_HINT1, "PREV NEXT PLAY")
    p.ascii(MARGIN, Y_HINT2, "REC  SYNC HOLD")


def page_card(p, idx, total, word, ipa, meaning, big, small, f16, f12):
    status_bar(p, "UNIT 01", f"{idx + 1:02d}/{total}")
    word_block(p, word, ipa, f12)
    meaning_block(p, meaning, f16)
    status_box(p, big, small, f16)
    progress(p, idx, total)
    footer(p)


def page_home(p, f16, stats):
    status_bar(p, "E-INK WORD", "HOME")
    p.ascii((W - p.ascii_width("DAILY", 3)) // 2, 40, "DAILY", 3)
    p.text16((W - 4 * 16) // 2, 84, "单词学习", f16)
    p.rect(MARGIN, 120, INNER, 112)
    rows = [("TOTAL", stats["total"]), ("TODAY", stats["today"]),
            ("STREAK", stats["streak"])]
    y = 136
    for label, value in rows:
        p.ascii(MARGIN + 10, y, label)
        txt = str(value)
        p.ascii(W - MARGIN - 10 - p.ascii_width(txt), y, txt)
        p.dash_hline(MARGIN + 8, y + 14, INNER - 16)
        y += 26
    p.ascii(MARGIN, 250, "PLAY = START")
    p.ascii(MARGIN, 262, "SYNC = WIFI")
    p.ascii(MARGIN, 274, "HOLD NEXT = UNIT")


def page_sync(p, f16, ssid, url):
    status_bar(p, "SYNC", "WIFI")
    p.text16((W - 4 * 16) // 2, 40, "无线同步", f16)
    p.rect(MARGIN, 80, INNER, 92)
    p.ascii(MARGIN + 8, 92, "SSID")
    p.ascii(MARGIN + 8, 104, ssid)
    p.ascii(MARGIN + 8, 124, "URL")
    p.ascii(MARGIN + 8, 136, url)
    p.text16((W - 6 * 16) // 2, 200, "按同步键退出", f16)
    p.ascii(MARGIN, 250, "SYNC = STOP AP")


def main():
    words = A.load_words()
    cjk = set()
    for label in A.UI_LABELS:
        cjk.update(label)
    for _, _, meaning in words:
        cjk.update(meaning)
    cjk.update("还剩秒")  # 卡片上动态文案
    ipa_chars = set()
    for _, ipa, _ in words:
        ipa_chars.update(ipa)

    f16 = A.build_cjk([c for c in cjk if ord(c) > 0x7F])
    f12 = A.build_ipa([c for c in ipa_chars if ord(c) >= 0x20])

    pages = []

    p = Panel()
    page_card(p, 0, len(words), words[0][0], words[0][1], words[0][2],
              "就绪", "按录音键跟读", f16, f12)
    pages.append(p.img)

    p = Panel()
    w = words[16]
    page_card(p, 16, len(words), w[0], w[1], w[2], "录音中", "正在录音", f16, f12)
    pages.append(p.img)

    p = Panel()
    w = words[10]
    page_card(p, 10, len(words), w[0], w[1], w[2], "已保存", "评分待接入", f16, f12)
    pages.append(p.img)

    p = Panel()
    page_home(p, f16, {"total": len(words), "today": 0, "streak": 0})
    pages.append(p.img)

    p = Panel()
    page_sync(p, f16, "EPD-Word-01", "192.168.4.1")
    pages.append(p.img)

    scale = 3
    gap = 12
    sheet = Image.new("L", ((W * len(pages) + gap * (len(pages) - 1)) * scale,
                            H * scale), 255)
    for i, img in enumerate(pages):
        sheet.paste(img.resize((W * scale, H * scale), Image.NEAREST),
                    (i * (W + gap) * scale, 0))
    out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "docs", "ui29_preview.png")
    sheet.save(out)
    print("preview:", out, sheet.size)


if __name__ == "__main__":
    sys.exit(main())
