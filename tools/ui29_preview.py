#!/usr/bin/env python3
"""渲染 2.9" 单词卡 UI 预览（横向 296x128），像素级复刻 C 端布局。

物理屏是 128x296 竖屏；横向使用 = 内容旋转 90° 后写进同一帧缓冲，
C 端由 ui29_card.c 的坐标映射完成，这里直接按横向逻辑坐标绘制。

用法：
    .venv-train\\Scripts\\python.exe tools/ui29_preview.py
产物：
    docs/ui29_preview.png   五个页面竖向堆叠，放大 3 倍
"""
from __future__ import annotations

import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ui29_assets as A  # noqa: E402
from font5x7_ascii import GLYPHS  # noqa: E402

# ------------------------------------------------------------------ 逻辑尺寸
W, H = 296, 128          # 横向：宽 296，高 128

# ------------------------------------------------------------------ 布局常量
# 这些数值直接照抄进 main/ui29/ui29_card.c，改布局时两边一起改。
# 卡片页竖向占用 4..122 px，右栏三行 18/42/66，进度条 84，键位条 98..122。
MARGIN = 4
INNER = W - 2 * MARGIN          # 288

Y_SBAR = 3
Y_DIV1 = 14

COL_L = 8                       # 左栏（单词 + 音标）起点
COL_R = 176                     # 右栏（释义 + 状态）起点
COL_R_W = W - MARGIN - COL_R    # 116 px -> 7 个汉字

Y_WORD = 17
WORD_SCALE = 4
Y_IPA = 53

Y_MEAN = 17
MEAN_LINE_H = 20
Y_BIG = 41
Y_SMALL = 65

Y_BAR = 83
BAR_H = 8
Y_HINT = 104


class Panel:
    """1bpp 画布，坐标系与 C 端横向逻辑坐标一致。"""

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

    # ---------------------------------------------------------- 12x20 IPA
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


def page_card(p, idx, total, word, ipa, meaning, big, small, f16, f12):
    status_bar(p, "UNIT 01", "%02d/%02d" % (idx + 1, total))

    # 左栏：单词 + 音标
    p.ascii(COL_L, Y_WORD, word, WORD_SCALE)
    p.text12(COL_L, Y_IPA, ipa, f12)

    # 右栏：释义 + 状态
    lines = [meaning[i:i + 7] for i in range(0, len(meaning), 7)][:1]
    for i, line in enumerate(lines):
        p.text16(COL_R, Y_MEAN + i * MEAN_LINE_H, line, f16)
    p.text16(COL_R, Y_BIG, big, f16)
    if small:
        p.text16(COL_R, Y_SMALL, small, f16)

    # 底部：进度 + 提示
    p.rect(MARGIN, Y_BAR, INNER, BAR_H)
    fill_w = int((INNER - 4) * (idx + 1) / total)
    if fill_w > 0:
        p.fill_rect(MARGIN + 2, Y_BAR + 2, fill_w, BAR_H - 4)
    # 底部键位条：画成带边框的条，避免 128 高里出现空带
    p.rect(MARGIN, Y_HINT - 6, INNER, 26)
    p.ascii(MARGIN + 6, Y_HINT, "PREV NEXT PLAY    REC SYNC HOLD")


def page_home(p, f16, stats):
    status_bar(p, "E-INK WORD", "HOME")
    p.text16(MARGIN + 4, 28, "单词学习", f16)
    p.ascii(MARGIN + 4, 56, "DAILY", 2)

    p.rect(150, 24, 138, 72)
    rows = [("TOTAL", stats["total"]), ("TODAY", stats["today"]),
            ("STREAK", stats["streak"])]
    y = 32
    for label, value in rows:
        p.ascii(160, y, label)
        txt = str(value)
        p.ascii(278 - p.ascii_width(txt), y, txt)
        p.dash_hline(158, y + 12, 122)
        y += 20

    p.ascii(MARGIN, Y_HINT, "PLAY=START  SYNC=WIFI  HOLD NEXT=UNIT")


def page_sync(p, f16, ssid, url):
    status_bar(p, "SYNC", "WIFI")
    p.text16(MARGIN + 4, 26, "无线同步", f16)
    p.rect(MARGIN + 4, 52, 200, 44)
    p.ascii(MARGIN + 12, 58, "SSID")
    p.ascii(MARGIN + 12, 70, ssid)
    p.ascii(MARGIN + 110, 58, "URL")
    p.ascii(MARGIN + 110, 70, url)
    p.text16(220, 28, "未启用", f16)
    p.ascii(MARGIN, Y_HINT, "SYNC = STOP AP")


def main():
    words = A.load_words()
    cjk = set()
    for label in A.UI_LABELS:
        cjk.update(label)
    for _, _, meaning in words:
        cjk.update(meaning)
    cjk.update("未启用")          # 同步页占位
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
    sheet = Image.new("L", (W * scale,
                            (H * len(pages) + gap * (len(pages) - 1)) * scale),
                      255)
    for i, img in enumerate(pages):
        sheet.paste(img.resize((W * scale, H * scale), Image.NEAREST),
                    (0, i * (H + gap) * scale))
    out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "docs", "ui29_preview.png")
    sheet.save(out)
    print("preview:", out, sheet.size)


if __name__ == "__main__":
    sys.exit(main())
