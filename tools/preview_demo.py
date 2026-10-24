#!/usr/bin/env python3
"""离线模拟 main.c 的绘图逻辑，渲染出「示例程序实际显示效果」PNG。

用途：在没有硬件的情况下检查 5x7 字体排版、灰阶条纹与局刷区域是否正确。

用法： python tools/preview_demo.py
"""
import os
import re
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_H = os.path.normpath(os.path.join(
    HERE, "..", "components", "epd_ssd1619a", "epd_font_5x7.h"))
OUT = os.path.normpath(os.path.join(HERE, "..", "docs", "demo_preview.png"))

W, H = 400, 300
ROW = W // 8


def load_font():
    src = open(FONT_H, encoding="ascii").read()
    body = src[src.index("font5x7[") :]
    rows = re.findall(r"\{([^{}]*)\}", body)
    table = []
    for r in rows:
        vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", r)]
        if len(vals) == 5:
            table.append(vals)
    return table


FONT = load_font()


class Frame:
    def __init__(self):
        self.buf = bytearray(ROW * H)

    def clear(self, color):
        self.buf = bytearray([0xFF if color else 0x00] * (ROW * H))

    def pixel(self, x, y, color):
        if 0 <= x < W and 0 <= y < H:
            m = 0x80 >> (x & 7)
            i = y * ROW + (x >> 3)
            if color:
                self.buf[i] |= m
            else:
                self.buf[i] &= (~m) & 0xFF

    def hline(self, x, y, w, color):
        for i in range(w):
            self.pixel(x + i, y, color)

    def vline(self, x, y, h, color):
        for i in range(h):
            self.pixel(x, y + i, color)

    def rect(self, x, y, w, h, color):
        self.hline(x, y, w, color)
        self.hline(x, y + h - 1, w, color)
        self.vline(x, y, h, color)
        self.vline(x + w - 1, y, h, color)

    def text(self, x, y, s, color):
        cx = x
        for ch in s:
            g = FONT[ord(ch) - 32]
            for col in range(5):
                for row in range(8):
                    if g[col] & (1 << row):
                        self.pixel(cx + col, y + row, color)
            cx += 6
        return cx

    def to_image(self):
        img = Image.new("1", (W, H), 0)
        px = img.load()
        for y in range(H):
            for x in range(W):
                bit = (self.buf[y * ROW + (x >> 3)] >> (7 - (x & 7))) & 1
                px[x, y] = 255 if bit else 0
        return img


def draw_gray_bars(f):
    f.clear(0)
    for level in range(8):
        x0 = 10 + level * 48
        phase = level
        for y in range(20, 160):
            for x in range(x0, x0 + 40):
                if ((x + y) % (phase + 1)) == 0:
                    f.pixel(x, y, 1)
        f.rect(x0, 20, 40, 140, 1)
    f.text(10, 170, "SSD1619A 4.2in 400x300", 1)
    f.text(10, 185, "ESP32-S3 / ESP-IDF", 1)


def main():
    f = Frame()
    draw_gray_bars(f)
    # 局刷区域（右上角 120x40 棋盘格）
    for y in range(40):
        for x in range(120):
            if ((x + y) & 3) == 0:
                f.pixel(400 - 120 - 8 + x, 10 + y, 1)
    f.to_image().resize((W * 2, H * 2), Image.NEAREST).save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
