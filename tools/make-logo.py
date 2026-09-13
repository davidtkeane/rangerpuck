#!/usr/bin/env python3
"""Convert a 2-tone PNG into a 1-bit bitmap header for the puck.
1-bit means the icon can be TINTED with the state colour at draw time,
and costs a few hundred bytes instead of tens of kilobytes.
    ../.venv/bin/python tools/make-logo.py <image.png> [width]
"""
import sys, os
from PIL import Image
SRC = sys.argv[1] if len(sys.argv) > 1 else "/Users/ranger/scripts/Github_David/HollywoodSaver/images/ranger.png"
W   = int(sys.argv[2]) if len(sys.argv) > 2 else 72
OUT = os.path.join(os.path.dirname(__file__), "..", "RangerPuck", "ranger_logo.h")
im = Image.open(SRC).convert("L")
H = round(W * im.height / im.width)
im = im.resize((W, H), Image.LANCZOS); px = im.load()
v = sorted(im.get_flattened_data() if hasattr(im,'get_flattened_data') else im.getdata())
thr = (v[len(v)//20] + v[-len(v)//20]) // 2
rb = (W + 7)//8; data = bytearray()
for y in range(H):
    for bx in range(rb):
        b = 0
        for bit in range(8):
            x = bx*8+bit
            if x < W and px[x,y] > thr: b |= 0x80 >> bit
        data.append(b)
with open(OUT,"w") as f:
    f.write(f"#pragma once\n#define LOGO_W {W}\n#define LOGO_H {H}\n"
            f"const unsigned char RANGER_LOGO[] PROGMEM = {{\n")
    for i in range(0,len(data),12):
        f.write("  "+", ".join(f"0x{b:02X}" for b in data[i:i+12])+",\n")
    f.write("};\n")
print(f"{W}x{H}, {len(data)} bytes -> {OUT}")
