"""Copies TFT_eSPI's smooth-drawing functions, unchanged, from the copy the orbs' original firmware was built with
(TFT_eSPI 2.5.43 by Bodmer, FreeBSD/MIT licences: third_party/TFT_eSPI-license.txt; give its folder as TFT_ESPI_DIR) into
third_party/tft_espi_smooth.inc, so the XL draws second ticks, arcs and circles pixel for pixel as the ESP32 did.
Run once: python3 -I tools/extract_tft.py (the output is kept in the project)."""
import os, re
SRC = os.path.join(os.environ.get("TFT_ESPI_DIR", "TFT_eSPI"), "TFT_eSPI.cpp")
HDR = os.path.join(os.environ.get("TFT_ESPI_DIR", "TFT_eSPI"), "TFT_eSPI.h")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "third_party", "tft_espi_smooth.inc")
WANT = ["uint16_t TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t color, uint8_t alpha, uint32_t bg_color)",
        "void TFT_eSPI::drawSmoothArc(", "inline uint8_t TFT_eSPI::sqrt_fraction(", "void TFT_eSPI::drawArc(",
        "void TFT_eSPI::drawSmoothCircle(", "void TFT_eSPI::fillSmoothCircle(", "void TFT_eSPI::drawSmoothRoundRect(",
        "void TFT_eSPI::fillSmoothRoundRect(", "void TFT_eSPI::drawSpot(", "void TFT_eSPI::drawWideLine(",
        "void TFT_eSPI::drawWedgeLine(", "inline float TFT_eSPI::wedgeLineDistance(",
        "uint16_t TFT_eSPI::alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc)\n"]
src = open(SRC).read()
out = ["/* Copied unchanged by tools/extract_tft.py from TFT_eSPI (Bodmer), the version the orbs' original firmware used.",
       "   Licence: see TFT_eSPI's license.txt (FreeBSD + MIT parts). Do not edit: re-run the tool. */"]
m = re.search(r"constexpr float PixelAlphaGain.*?deg2rad\s*=\s*[^;]*;", src, re.S)
out.append(m.group(0))
for w in WANT:
    i = src.find(w)
    assert i >= 0, w
    j = src.index("{", i)
    depth = 0
    for k in range(j, len(src)):
        if src[k] == "{": depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                break
    out.append(src[i:k + 1])
# the header's fastBlend template
h = open(HDR).read()
i = h.find("template <typename A, typename F, typename B> static inline uint16_t\nfastBlend")
assert i >= 0, "fastBlend"
j = h.index("{", i); depth = 0
for k in range(j, len(h)):
    if h[k] == "{": depth += 1
    elif h[k] == "}":
        depth -= 1
        if depth == 0: break
out.insert(2, h[i:k + 1])
open(OUT, "w").write("\n\n".join(out) + "\n")
print(OUT, sum(len(x) for x in out), "bytes,", len(WANT), "functions + fastBlend")
