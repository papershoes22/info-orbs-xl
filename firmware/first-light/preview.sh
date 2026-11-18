#!/bin/sh
# Draws the first-light pictures on the PC (the XL's own drawing code) into one PNG: tools/preview.sh OUT.png
# Rows: before any button; the middle button held (3rd press); after it's let go. Each picture masked to the round
# screen (what the glass shows).
set -e
cd "$(dirname "$0")/.."
OUT=${1:?OUT.png}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cc -O1 -Wall -Wextra -fsanitize=address,undefined -o "$T/preview" tools/preview.c src/cards.c src/gfx.c src/fonts.c -lm
"$T/preview" "$T"
python3 -I - "$T" "$OUT" <<'PY'
import sys
from PIL import Image, ImageDraw
d, out = sys.argv[1], sys.argv[2]
sheet = Image.new("RGB", (5 * 250 + 10, 3 * 250 + 10), (40, 40, 40))
mask = Image.new("L", (240, 240), 0)
ImageDraw.Draw(mask).ellipse((0, 0, 239, 239), fill=255)
for r, row in enumerate("abc"):
    for o in range(5):
        im = Image.frombytes("RGB", (240, 240), open(f"{d}/{row}{o}.rgb", "rb").read())
        bg = Image.new("RGB", (240, 240), (0, 0, 0))
        bg.paste(im, (0, 0), mask)
        sheet.paste(bg, (10 + o * 250, 10 + r * 250))
sheet.save(out)
print(out)
PY
