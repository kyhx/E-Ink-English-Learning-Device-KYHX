#!/usr/bin/env python3
"""把 PNG 图片转换成 SSD1619A 4.2" 单色墨水屏的 1bpp 帧文件（15000 字节）。

帧格式（与本工程驱动一致）：
  - 400 x 300 像素，1 bit/px
  - 每行 50 字节，整帧 15000 字节，行优先
  - 高位在左（MSB first）
  - 位值 1 = 白色，0 = 黑色

用法：
    python tools/png_to_raw.py input.png output.raw [--invert] [--scale contain|cover|stretch]
                                             [--threshold 128] [--flip-y] [--mirror-x]
默认缩放方式为 contain（等比缩放并居中留白，白底）。
"""
import argparse
import os
import sys

from PIL import Image

W, H = 400, 300
ROW = W // 8
TOTAL = ROW * H


def load_and_fit(path, mode, scale_mode, invert, threshold):
    img = Image.open(path).convert("L")
    if scale_mode == "stretch":
        img = img.resize((W, H), Image.LANCZOS)
    else:
        ratio = min(W / img.width, H / img.height) if scale_mode == "contain" \
            else max(W / img.width, H / img.height)
        nw, nh = max(1, round(img.width * ratio)), max(1, round(img.height * ratio))
        img = img.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new("L", (W, H), 255 if not invert else 0)
        canvas.paste(img, ((W - nw) // 2, (H - nh) // 2))
        img = canvas

    # 阈值二值化：暗 -> 黑(0)，亮 -> 白(1)
    out = bytearray(TOTAL)
    for y in range(H):
        base = y * ROW
        for x in range(W):
            v = img.getpixel((x, y))
            bit = 1 if v > threshold else 0
            if invert:
                bit ^= 1
            if bit:
                out[base + (x >> 3)] |= (0x80 >> (x & 7))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--invert", action="store_true", help="黑白反转")
    ap.add_argument("--scale", default="contain",
                    choices=["contain", "cover", "stretch"])
    ap.add_argument("--threshold", type=int, default=128)
    ap.add_argument("--flip-y", action="store_true", help="上下翻转")
    ap.add_argument("--mirror-x", action="store_true", help="左右镜像")
    a = ap.parse_args()

    data = load_and_fit(a.input, None, a.scale, a.invert, a.threshold)

    if a.flip_y:
        rows = [data[y * ROW:(y + 1) * ROW] for y in range(H)]
        rows.reverse()
        data = b"".join(rows)
    if a.mirror_x:
        rows = []
        for y in range(H):
            r = data[y * ROW:(y + 1) * ROW]
            nr = bytearray(ROW)
            for b in range(ROW):
                v = r[b]
                nr[ROW - 1 - b] = int("{:08b}".format(v)[::-1], 2)
            rows.append(bytes(nr))
        data = b"".join(rows)

    with open(a.output, "wb") as f:
        f.write(data)
    print("wrote %s (%d bytes)" % (a.output, len(data)))


if __name__ == "__main__":
    main()
