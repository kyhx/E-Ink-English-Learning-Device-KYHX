#!/usr/bin/env python3
"""渲染 2.9" 单词卡 UI 预览（横向 296x128），像素级复刻 C 端布局。

页面：主菜单 / 单词卡 / 复习日历 / 同步 / 设置。

用法：
    .venv-train\\Scripts\\python.exe tools/ui29_preview.py
产物：
    docs/ui29_preview.png   五页竖向堆叠，放大 3 倍
"""
from __future__ import annotations

import calendar
import datetime
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ui29_assets as A  # noqa: E402
from font5x7_ascii import GLYPHS  # noqa: E402

# ------------------------------------------------------------------ 逻辑尺寸
W, H = 296, 128

# ------------------------------------------------------------------ 布局常量
# 这些数值直接照抄进 main/ui29/ui29_card.c，改布局时两边一起改。
MARGIN = 3
INNER = W - 2 * MARGIN          # 290
Y_SBAR = 3
Y_DIV1 = 14

# 菜单
MENU_X = 12
MENU_Y = 18
MENU_ROW_H = 26
MENU_ROWS = 4

# 单词卡
COL_L = 8
Y_WORD = 18
WORD_SCALE = 4
Y_ROW2 = 56                    # 音标 + 释义同一行
Y_BAR = 86
BAR_H = 8
Y_HINT = 106

# 通用
CJK = 24                       # 汉字字身高
IPA_ADV = A.IPA_ADVANCE        # 12


class Panel:
    def __init__(self):
        self.img = Image.new("L", (W, H), 255)
        self.d = ImageDraw.Draw(self.img)

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

    # ---------------------------------------------------------- 5x7 ASCII
    def ascii(self, x, y, text, scale=1, color=1):
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
                            self.px(cx + col, y + row, color)
                        else:
                            self.fill_rect(cx + col * scale, y + row * scale,
                                           scale, scale, color)
            cx += 5 * scale
        return cx - x

    def ascii_width(self, text, scale=1):
        return len(text) * 5 * scale

    # ---------------------------------------------------------- 24x24 汉字
    def glyph24(self, x, y, ch, table, color=1):
        rows = table.get(ch)
        if rows is None:
            return
        for j, bits in enumerate(rows):
            for i in range(24):
                if bits & (1 << (23 - i)):
                    self.px(x + i, y + j, color)

    def text24(self, x, y, text, table, color=1):
        cx = x
        for ch in text:
            self.glyph24(cx, y, ch, table, color)
            cx += CJK
        return cx - x

    # ---------------------------------------------------------- 14x24 音标
    def glyph14(self, x, y, ch, table):
        rows = table.get(ch)
        if rows is None:
            return
        for j, bits in enumerate(rows):
            for i in range(A.IPA_CELL_W):
                if bits & (1 << (A.IPA_CELL_W - 1 - i)):
                    self.px(x + i, y + j)

    def text14(self, x, y, text, table):
        cx = x
        for ch in text:
            self.glyph14(cx, y, ch, table)
            cx += IPA_ADV
        return cx - x

    def text14_width(self, text):
        return len(text) * IPA_ADV


# ------------------------------------------------------------------ 公共件
def status_bar(p, left, right):
    p.ascii(MARGIN, Y_SBAR, left)
    p.ascii(W - MARGIN - p.ascii_width(right), Y_SBAR, right)
    p.hline(MARGIN, Y_DIV1, INNER)


def footer(p, text):
    p.rect(MARGIN, Y_HINT - 8, INNER, 24)
    p.ascii(MARGIN + 6, Y_HINT, text)


# ------------------------------------------------------------------ 页面
def page_menu(p, items, sel, f24):
    status_bar(p, "E-INK WORD", "MENU")
    for i, item in enumerate(items):
        y = MENU_Y + i * MENU_ROW_H
        if i == sel:
            p.fill_rect(MARGIN, y - 2, INNER, MENU_ROW_H - 2)
            p.text24(MENU_X, y, item, f24, color=0)
            # 右侧箭头
            p.ascii(W - MARGIN - 12, y + 8, ">", 1, color=0)
        else:
            p.text24(MENU_X, y, item, f24)
            p.ascii(W - MARGIN - 12, y + 8, ">", 1)


def page_card(p, idx, total, word, ipa, meaning, f24, f14):
    status_bar(p, "UNIT 01", "%02d/%02d" % (idx + 1, total))
    p.ascii(COL_L, Y_WORD, word, WORD_SCALE)

    # 音标 + 释义同一行
    p.text14(COL_L, Y_ROW2, ipa, f14)
    mw = len(meaning) * CJK
    mx = COL_L + p.text14_width(ipa) + 14
    if mx + mw > W - MARGIN:
        mx = W - MARGIN - mw
    p.text24(mx, Y_ROW2, meaning, f24)

    p.rect(MARGIN, Y_BAR, INNER, BAR_H)
    fill = int((INNER - 4) * (idx + 1) / total)
    if fill:
        p.fill_rect(MARGIN + 2, Y_BAR + 2, fill, BAR_H - 4)
    footer(p, "PREV NEXT PLAY   REC SYNC HOLD")


def page_calendar(p, today, reviewed, f24):
    status_bar(p, "CALENDAR", "%d-%02d  SYNC=BACK" % (today.year, today.month))

    # 星期表头：24px 汉字，7 列等宽
    wd = "日一二三四五六"
    cell_w = (INNER) // 7          # 41
    for i, ch in enumerate(wd):
        p.text24(MARGIN + i * cell_w + (cell_w - CJK) // 2, 15, ch, f24)

    first_wd = (datetime.date(today.year, today.month, 1).weekday() + 1) % 7
    ndays = calendar.monthrange(today.year, today.month)[1]
    y0 = 41
    row_h = 14

    for d in range(1, ndays + 1):
        slot = first_wd + d - 1
        r, c = divmod(slot, 7)
        if r >= 6:
            break
        cx = MARGIN + c * cell_w
        cy = y0 + r * row_h
        txt = str(d)
        tw = p.ascii_width(txt)
        x = cx + (cell_w - tw) // 2
        if d in reviewed:
            # 已复习：黑底白字
            p.fill_rect(cx + 8, cy, cell_w - 16, 11)
            p.ascii(x, cy + 2, txt, 1, color=0)
        else:
            p.ascii(x, cy + 2, txt, 1)
        if d == today.day:
            p.rect(cx + 6, cy - 1, cell_w - 12, 13)



def page_sync(p, ssid, url, f24):
    status_bar(p, "SYNC", "WIFI")
    p.text24(MARGIN, 18, "无线同步", f24)
    p.ascii(MARGIN, 50, "SSID")
    p.ascii(MARGIN + 44, 50, ssid)
    p.ascii(MARGIN, 64, "URL")
    p.ascii(MARGIN + 44, 64, url)
    p.text24(MARGIN, 80, "未启用", f24)
    p.ascii(MARGIN, Y_HINT + 4, "SYNC = 返回主菜单".replace("返回主菜单", "BACK"))


def page_settings(p, f24, sel):
    status_bar(p, "SETTINGS", "MENU")
    rows = [("无线网络", "关闭"), ("蓝牙", "关闭")]
    for i, (label, state) in enumerate(rows):
        y = 18 + i * 38
        if i == sel:
            p.rect(MARGIN, y - 2, INNER, 30)
        p.text24(MARGIN + 10, y, label, f24)
        sw = len(state) * CJK
        p.text24(W - MARGIN - 10 - sw, y, state, f24)
    p.ascii(MARGIN, Y_HINT + 4, "PLAY = TOGGLE   SYNC = BACK")


def main():
    words = A.load_words()
    cjk = A.ui_cjk_chars(words)
    cjk.update("年月日一二三四五六")
    ipa_chars = set()
    for _, ipa, _ in words:
        ipa_chars.update(ipa)

    f24 = A.build_cjk24([c for c in cjk if ord(c) > 0x7F])
    f14 = A.build_ipa14([c for c in ipa_chars if ord(c) >= 0x20])

    pages = []
    menu_items = ["单词", "复习日历", "同步", "设置"]

    p = Panel()
    page_menu(p, menu_items, 0, f24)
    pages.append(p.img)

    # 菜单选中第 2 项（演示高亮）
    p = Panel()
    page_menu(p, menu_items, 1, f24)
    pages.append(p.img)

    p = Panel()
    page_card(p, 0, len(words), words[0][0], words[0][1], words[0][2], f24, f14)
    pages.append(p.img)

    p = Panel()
    w = words[32]      # iron 铁 / 20 warm 温暖的
    page_card(p, 32, len(words), w[0], w[1], w[2], f24, f14)
    pages.append(p.img)

    today = datetime.date(2026, 11, 5)
    p = Panel()
    page_calendar(p, today, {1, 2, 3, 4, 5}, f24)
    pages.append(p.img)

    p = Panel()
    page_sync(p, "EPD-Word-01", "192.168.4.1", f24)
    pages.append(p.img)

    p = Panel()
    page_settings(p, f24, 0)
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
