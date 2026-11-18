#!/bin/sh
# The orbs' sets drawn on the PC by the XL's own code: tools/preview_sets.sh OUT.png (each scene a row of 5 orbs,
# masked to the round glass).
set -e
cd "$(dirname "$0")/.."
OUT=${1:?OUT.png}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
OFR=third_party/OpenFontRender
for f in $(find $OFR -name '*.c'); do cc -c -O1 -w -I$OFR -DFT2_BUILD_LIBRARY "$f" -o "$T/$(echo $f | tr '/.' '__').o"; done
for f in $(find $OFR -name '*.cpp'); do c++ -c -O1 -w -std=c++17 -include cstdint -I$OFR -DFT2_BUILD_LIBRARY "$f" -o "$T/$(echo $f | tr '/.' '__').o"; done
cc -c -O1 -w third_party/tjpgd_bodmer/tjpgd.c -o "$T/tjpgd.o"
cc -c -O1 src/assets.c -o "$T/assets.o"
c++ -O1 -std=c++17 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -include cstdint -I$OFR -Ithird_party/ArduinoJson -o "$T/preview" \
	tools/preview_sets.cpp src/screen.cpp src/tft.cpp src/clock_widget.cpp src/weather.cpp src/sets_new.cpp "$T"/*.o -lm
# (FreeType's cache is never freed at exit - the XL never exits - so leak reports are off; the run prints the
# heap in use over 2000 simulated seconds instead)
ASAN_OPTIONS=detect_leaks=0 "$T/preview" "$T"
python3 -I - "$T" "$OUT" <<'PY'
import sys, glob, os
from PIL import Image, ImageDraw
d, out = sys.argv[1], sys.argv[2]
files = sorted(glob.glob(f"{d}/*.rgb"))
mask = Image.new("L", (240, 240), 0); ImageDraw.Draw(mask).ellipse((0, 0, 239, 239), fill=255)
sheet = Image.new("RGB", (5 * 250 + 10, len(files) * 270 + 10), (40, 40, 40))
dr = ImageDraw.Draw(sheet)
for r, f in enumerate(files):
    im = Image.frombytes("RGB", (1200, 240), open(f, "rb").read())
    for o in range(5):
        orb = im.crop((o * 240, 0, o * 240 + 240, 240)); bg = Image.new("RGB", (240, 240)); bg.paste(orb, (0, 0), mask)
        sheet.paste(bg, (10 + o * 250, 30 + r * 270))
    dr.text((12, 12 + r * 270), os.path.basename(f)[:-4], fill=(200, 200, 200))
sheet.save(out); print(out)
PY
