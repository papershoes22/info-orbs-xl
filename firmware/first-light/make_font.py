"""Bitmap fonts for the orbs' screens (2026-11-16): Barlow (SIL OFL, from Google Fonts),
anti-aliased, 4 bits per pixel. Pass the folder with Barlow's .ttf files as BARLOW_DIR.

Run:  python3 -I tools/make_font.py      (writes src/fonts.h + src/fonts.c; build.sh runs it)
Each glyph: its bitmap's width/height, where it sits from the pen (x) and from the line's top (y), and its advance.
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "..", "src", "fonts.h")
OUT_C = os.path.join(HERE, "..", "src", "fonts.c")
BARLOW = os.environ.get("BARLOW_DIR", "fonts")
FONTS = [  # name, file, pixels, which characters (None = all of printable ASCII)
    ("font_name", os.path.join(BARLOW, "BarlowSemiCondensed-SemiBold.ttf"), 100, "D12345"),
    ("font_lab", os.path.join(BARLOW, "Barlow-SemiBold.ttf"), 20, None),
    ("font_info", os.path.join(BARLOW, "Barlow-Medium.ttf"), 16, None),
]


def one(name, ttf, px, chars):
    f = ImageFont.truetype(ttf, px)
    asc, desc = f.getmetrics()
    data, glyphs = [], []
    for c in range(32, 127):
        ch = chr(c)
        if chars is not None and ch not in chars:
            glyphs.append((0, 0, 0, 0, 0, 0))
            continue
        l, t, r, b = f.getbbox(ch)            # from the pen at the line's top (anchor "la")
        w, h = max(0, r - l), max(0, b - t)
        adv = int(round(f.getlength(ch)))
        assert w < 256 and h < 256 and -128 <= l < 128 and -128 <= t < 128 and adv < 256, (name, ch)
        off = len(data)
        if w and h:
            im = Image.new("L", (w, h), 0)
            ImageDraw.Draw(im).text((-l, -t), ch, font=f, fill=255)
            px_ = list(im.tobytes())
            for row in range(h):
                line = px_[row * w:(row + 1) * w]
                if w % 2:
                    line = line + [0]
                for i in range(0, len(line), 2):
                    data.append((line[i] >> 4) << 4 | (line[i + 1] >> 4))
        glyphs.append((off, w, h, l, t, adv))
    s = [f"static const uint8_t {name}_bits[] = {{"]
    for i in range(0, len(data), 24):
        s.append("  " + ",".join(str(v) for v in data[i:i + 24]) + ",")
    s.append("};")
    s.append(f"static const glyph_t {name}_glyphs[95] = {{")
    for g in glyphs:
        s.append("  {%d,%d,%d,%d,%d,%d}," % g)
    s.append("};")
    s.append(f"const font_t {name} = {{ {name}_bits, {name}_glyphs, {asc + desc}, {asc} }};")
    return "\n".join(s)


def main():
    head = ["/* made by tools/make_font.py (Barlow, the Info Orbs' font); do not edit */", "#pragma once",
            "#include <stdint.h>",
            "typedef struct { uint32_t off; uint8_t w, h; int8_t x, y; uint8_t adv; } glyph_t;",
            "typedef struct { const uint8_t *bits; const glyph_t *glyphs; uint8_t line_h, ascent; } font_t;"]
    head += [f"extern const font_t {n};" for n, *_ in FONTS]
    body = ["/* made by tools/make_font.py; do not edit */", '#include "fonts.h"']
    for n, ttf, px, chars in FONTS:
        body.append(one(n, ttf, px, chars))
    with open(OUT_H, "w") as fo:
        fo.write("\n".join(head) + "\n")
    with open(OUT_C, "w") as fo:
        fo.write("\n".join(body) + "\n")
    print(OUT_H, OUT_C, os.path.getsize(OUT_C))


if __name__ == "__main__":
    main()
