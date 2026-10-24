#!/usr/bin/env python3
"""生成 ESP32-S3 + 墨水屏驱动板 V1.5 (SSD1619A 4.2" 单色) 接线图 PNG。

用法： python tools/gen_wiring.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "docs", "wiring.png"))

W, H = 1500, 980
BG = "#FFFFFF"
PANEL = "#F2F5F9"
LINE = "#2F3E4E"
TXT = "#1B2733"
GREY = "#7A8899"
RED = "#C0392B"
GREEN = "#1E8E5A"

CJK = "C:/Windows/Fonts/msyh.ttc"
MONO = "C:/Windows/Fonts/consola.ttf"


def font(path, size, index=0):
    try:
        return ImageFont.truetype(path, size, index=index)
    except Exception:
        return ImageFont.truetype("C:/Windows/Fonts/arial.ttf", size)


f_title = font(CJK, 40)
f_sub = font(CJK, 22)
f_h = font(CJK, 26)
f_lbl = font(CJK, 21)
f_sil = font(MONO, 21)
f_note = font(CJK, 19)
f_small = font(CJK, 18)


def center(d, xy, text, fnt, fill):
    d.text(xy, text, font=fnt, fill=fill, anchor="mm")


def main():
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    center(d, (W // 2, 46), "ESP32-S3  与  墨水屏驱动板 V1.5（4.2\" 单色 SSD1619A）",
           f_title, TXT)
    center(d, (W // 2, 88), "接线图 / Pinout ·  8 针 SPI 驱动板，CS2 为三色屏专用，单色屏悬空",
           f_sub, GREY)

    # ---- 左侧：驱动板 ----
    lx0, lx1 = 90, 430
    ly0, ly1 = 150, 830
    d.rounded_rectangle([lx0, ly0, lx1, ly1], 14, fill=PANEL, outline=LINE, width=3)
    center(d, ((lx0 + lx1) // 2, ly0 + 34), "墨水屏驱动板 V1.5", f_h, TXT)
    center(d, ((lx0 + lx1) // 2, ly0 + 68), "逻辑顺序（非物理顺序）", f_small, GREY)

    left = ["CS1", "DC", "RST", "BUSY", "SCK", "SDA", "3V3", "GND", "CS2"]
    ldesc = ["片选（低有效）", "数据/命令", "复位（低有效）", "忙信号", "SPI 时钟",
             "SPI 数据 MOSI", "3.3V 供电", "地", "三色屏片选 2（单色屏不接）"]

    lx = lx1 - 62          # 左侧引脚出线端 x
    ly0p = ly0 + 110
    lyp = 44              # 行距

    for i, name in enumerate(left):
        y = lyp0 = ly0p + i * lyp
        d.ellipse([lx - 7, y - 7, lx + 7, y + 7], fill=LINE)
        # 丝印文字
        d.text((lx0 + 26, y - 14), name, font=f_sil, fill=TXT)
        d.text((lx0 + 26, y + 10), ldesc[i], font=f_small, fill=GREY)

    # ---- 右侧：ESP32-S3 ----
    rx0, rx1 = 1070, 1410
    d.rounded_rectangle([rx0, ly0, rx1, ly1], 14, fill=PANEL, outline=LINE, width=3)
    center(d, ((rx0 + rx1) // 2, ly0 + 34), "ESP32-S3 开发板", f_h, TXT)
    center(d, ((rx0 + rx1) // 2, ly0 + 68), "GPIO 编号（任意可换）", f_small, GREY)

    right = [("GPIO10", "CS"), ("GPIO9", "DC"), ("GPIO8", "RST"),
             ("GPIO7", "BUSY"), ("GPIO12", "SCK"),
             ("GPIO11", "MOSI"), ("3V3", "3.3V"), ("GND", "GND"), ("—", "NC")]
    rx = rx0 + 62

    for i, (g, use) in enumerate(right):
        y = ly0p + i * lyp
        d.ellipse([rx - 7, y - 7, rx + 7, y + 7], fill=LINE)
        d.text((rx + 22, y - 13), g + "  ·  " + use, font=f_sil, fill=TXT)

    # ---- 连线 ----
    wire = [("CS1", "GPIO10", "CS"), ("DC", "GPIO9", "DC"), ("RST", "GPIO8", "RST"),
            ("BUSY", "GPIO7", "BUSY"), ("SCK", "GPIO12", "SCK"),
            ("SDA", "GPIO11", "MOSI"), ("3V3", "3V3", "3V3"), ("GND", "GND", "GND")]

    colors = {"CS": "#2E86C1", "DC": "#8E44AD", "RST": "#C0392B", "BUSY": "#E67E22",
              "SCK": "#16A085", "MOSI": "#27AE60", "3V3": "#D35400", "GND": "#2C3E50"}

    for i, (a, b, tag) in enumerate(wire):
        y = ly0p + i * lyp
        c = colors[tag]
        d.line([lx + 8, y, rx - 8, y], fill=c, width=4)
        mid = (lx + rx) // 2
        d.rectangle([mid - 42, y - 13, mid + 42, y + 13], fill=BG, outline=c, width=2)
        center(d, (mid, y), tag, f_small, c)

    # CS2 悬空提示：只画短 stub，不要连到 MCU
    y9 = ly0p + 8 * lyp
    d.line([lx + 8, y9, lx + 110, y9], fill="#C8CDD4", width=3)
    center(d, (lx + 165, y9), "不连接", f_small, GREY)

    # ---- 底部说明 ----
    d.rounded_rectangle([90, 862, 1410, 946], 10, fill="#FFF8E6", outline="#E0B84C", width=2)
    d.text((112, 878), "注意：", font=f_lbl, fill="#8A6D1F")
    d.text((178, 878), "① 驱动板与 ESP32-S3 必须共地；供电用 3.3V，切勿接 5V。", font=f_note, fill=TXT)
    d.text((178, 908), "② SSD1619A 的 BUSY 为「高电平表示忙」；若你的驱动板带反相，"
                       "把代码中 busy_high_is_busy 改为 0。", font=f_note, fill=TXT)

    img.save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
