#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""
Generate the test picture of drmdevice (DEVELOPER/testpicture_data.h)

The picture is drawn here from scratch, it contains no third party work. The
picture and this script are dedicated to the public domain (CC0 1.0).

The picture is drawn in Y'CbCr (BT.709, limited range), so the PLUGE patches
can be below black and the colour bars have exact values. It is encoded as a
single H.264 I-frame with ffmpeg (libx264) and written as a C array.

usage: testpicture.py <output header> [preview png]
       e.g. DEVELOPER/testpicture.py DEVELOPER/testpicture_data.h

Requirements: python3, numpy, ffmpeg with libx264
"""

import subprocess
import sys

import numpy as np

W, H = 1920, 1080
CELL = 120                      # grid cell, 16 x 9 cells
CX, CY, R = W // 2, H // 2, 480 # centre circle

# BT.709 Y'CbCr (limited range) of R'G'B' in 0..1
def ycbcr(r, g, b):
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    cb = (b - y) / 1.8556
    cr = (r - y) / 1.5748
    return (16 + 219 * y, 128 + 224 * cb, 128 + 224 * cr)

Y = np.zeros((H, W))
CB = np.zeros((H, W))
CR = np.zeros((H, W))

def fill(mask, colour):
    y, cb, cr = colour
    Y[mask] = y
    CB[mask] = cb
    CR[mask] = cr

def rect(x0, y0, x1, y1, colour):
    m = np.zeros((H, W), bool)
    m[y0:y1, x0:x1] = True
    fill(m, colour)

GREY = ycbcr(0.5, 0.5, 0.5)
WHITE = ycbcr(1, 1, 1)
BLACK = ycbcr(0, 0, 0)

yy, xx = np.mgrid[0:H, 0:W]

# background with a white grid and a white border (overscan)
fill(np.ones((H, W), bool), GREY)
grid = np.zeros((H, W), bool)
for k in range(1, W // CELL):
    grid[:, k * CELL - 1:k * CELL + 1] = True
for k in range(1, H // CELL):
    grid[k * CELL - 1:k * CELL + 1, :] = True
grid[:, :2] = grid[:, -2:] = True
grid[:2, :] = grid[-2:, :] = True
fill(grid, WHITE)

# corner circles (geometry)
for cx, cy in ((180, 180), (W - 180, 180), (180, H - 180), (W - 180, H - 180)):
    d = np.hypot(xx - cx, yy - cy)
    fill(d < 100, BLACK)
    fill((d >= 96) & (d < 100), WHITE)

# centre circle (aspect ratio), its inner area is black
dist = np.hypot(xx - CX, yy - CY)
inside = dist < R
fill(inside, BLACK)

# area of the test signals, completely inside the circle
X0, X1 = 560, 1360

# 75% colour bars (EBU): white, yellow, cyan, green, magenta, red, blue, black
bars = [(1, 1, 1), (1, 1, 0), (0, 1, 1), (0, 1, 0), (1, 0, 1), (1, 0, 0), (0, 0, 1), (0, 0, 0)]
for i, (r, g, b) in enumerate(bars):
    rect(X0 + i * 100, 300, X0 + (i + 1) * 100, 420, ycbcr(0.75 * r, 0.75 * g, 0.75 * b))

# grey scale, 10 steps from black to white
for i in range(10):
    v = i / 9
    rect(X0 + i * 80, 420, X0 + (i + 1) * 80, 480, ycbcr(v, v, v))

# PLUGE on black: -2%, 0, +2%, +4% and a peak white patch
pluge = [16 - 4.38, 16, 16 + 4.38, 16 + 8.76]
for i, y in enumerate(pluge):
    rect(600 + i * 60, 492, 600 + (i + 1) * 60, 588, (y, 128, 128))
rect(1120, 492, 1320, 588, WHITE)

# frequency gratings (black/white lines with a period of 16, 8, 6, 4, 3 and 2 pixels)
periods = [16, 8, 6, 4, 3, 2]
width = (X1 - X0) // len(periods)
for i, p in enumerate(periods):
    x0 = X0 + i * width
    x1 = X1 if i == len(periods) - 1 else x0 + width
    lines = ((xx - x0) % p) < p / 2
    m = np.zeros((H, W), bool)
    m[600:720, x0:x1] = True
    fill(m & lines, WHITE)
    fill(m & ~lines, BLACK)

# 100% colour bars: red, green, blue, cyan, magenta, yellow
primaries = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 1, 1), (1, 0, 1), (1, 1, 0)]
for i, c in enumerate(primaries):
    x0 = X0 + i * width
    x1 = X1 if i == len(primaries) - 1 else x0 + width
    rect(x0, 720, x1, 780, ycbcr(*c))

# centre cross
rect(CX - 1, 510, CX + 1, 570, WHITE)
rect(CX - 30, CY - 1, CX + 30, CY + 1, WHITE)

# circle outline
fill((dist >= R - 4) & (dist < R), WHITE)

# labels in an own 5x7 pixel font
FONT = {
    'D': ["1110 ", "1  1 ", "1   1", "1   1", "1   1", "1  1 ", "1110 "],
    'R': ["1111 ", "1   1", "1   1", "1111 ", "1 1  ", "1  1 ", "1   1"],
    'M': ["1   1", "11 11", "1 1 1", "1 1 1", "1   1", "1   1", "1   1"],
    'E': ["11111", "1    ", "1    ", "1111 ", "1    ", "1    ", "11111"],
    'V': ["1   1", "1   1", "1   1", "1   1", "1   1", " 1 1 ", "  1  "],
    'I': [" 111 ", "  1  ", "  1  ", "  1  ", "  1  ", "  1  ", " 111 "],
    'C': [" 111 ", "1   1", "1    ", "1    ", "1    ", "1   1", " 111 "],
    'X': ["1   1", "1   1", " 1 1 ", "  1  ", " 1 1 ", "1   1", "1   1"],
    '0': [" 111 ", "1   1", "1  11", "1 1 1", "11  1", "1   1", " 111 "],
    '1': ["  1  ", " 11  ", "  1  ", "  1  ", "  1  ", "  1  ", " 111 "],
    '2': [" 111 ", "1   1", "    1", "   1 ", "  1  ", " 1   ", "11111"],
    '8': [" 111 ", "1   1", "1   1", " 111 ", "1   1", "1   1", " 111 "],
    '9': [" 111 ", "1   1", "1   1", " 1111", "    1", "   1 ", " 11  "],
}

def text(s, cy, scale, colour):
    w = len(s) * 6 * scale - scale
    x = CX - w // 2
    y0 = cy - 7 * scale // 2
    for ch in s:
        for row, bits in enumerate(FONT[ch]):
            for col, bit in enumerate(bits):
                if bit == '1':
                    rect(x + col * scale, y0 + row * scale, x + (col + 1) * scale, y0 + (row + 1) * scale, colour)
        x += 6 * scale

text("DRMDEVICE", 210, 8, WHITE)
text("1920X1080", 870, 8, WHITE)

# 4:2:0: average the chroma of 2x2 pixels
y8 = np.clip(np.rint(Y), 1, 254).astype(np.uint8)
cb8 = np.clip(np.rint(CB.reshape(H // 2, 2, W // 2, 2).mean(axis=(1, 3))), 1, 254).astype(np.uint8)
cr8 = np.clip(np.rint(CR.reshape(H // 2, 2, W // 2, 2).mean(axis=(1, 3))), 1, 254).astype(np.uint8)
raw = y8.tobytes() + cb8.tobytes() + cr8.tobytes()

colour = ["-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv"]
h264 = subprocess.run(
    ["ffmpeg", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", f"{W}x{H}", "-r", "50",
     *colour, "-i", "-",
     "-c:v", "libx264", "-preset", "veryslow", "-tune", "stillimage", "-profile:v", "high", "-level", "4.0",
     "-crf", "14", "-x264-params", "keyint=1:aud=1:bframes=0:sar=1/1", *colour,
     "-frames:v", "1", "-f", "h264", "-"],
    input=raw, stdout=subprocess.PIPE, check=True).stdout

with open(sys.argv[1], "w") as f:
    f.write("// SPDX-License-Identifier: CC0-1.0\n\n")
    f.write("/**\n * @file testpicture_data.h\n * Test picture of drmdevice (generated by DEVELOPER/testpicture.py, don't edit)\n")
    f.write(" *\n * A single H.264 I-frame (1920x1080, BT.709), dedicated to the public domain (CC0 1.0).\n */\n\n")
    f.write("#ifndef __TESTPICTURE_DATA_H\n#define __TESTPICTURE_DATA_H\n\n#include <cstdint>\n\n")
    f.write("/** the H.264 elementary stream of the test picture (Annex B) */\n")
    f.write("static const uint8_t s_testPictureH264[] = {\n")
    for i in range(0, len(h264), 16):
        f.write("\t" + ", ".join(f"0x{b:02x}" for b in h264[i:i + 16]) + ",\n")
    f.write("};\n\n#endif\n")

print(f"{sys.argv[1]}: {len(h264)} bytes of H.264")

if len(sys.argv) > 2:
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "h264", "-i", "-", "-frames:v", "1", sys.argv[2]],
                   input=h264, check=True)
