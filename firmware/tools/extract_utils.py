"""Copies the orbs' original colour helpers, unchanged, from the original Info Orbs firmware's firmware/src/core/utils/Utils.cpp
(github.com/brettdottech/info-orbs, AGPL-3.0: third_party/info-orbs-LICENSE.txt; give the repo folder as INFO_ORBS_DIR) into third_party/orbs_utils.inc. Run once: python3 -I tools/extract_utils.py"""
import os, re
SRC = os.path.join(os.environ.get("INFO_ORBS_DIR", "info-orbs"), "firmware/src/core/utils/Utils.cpp")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "third_party", "orbs_utils.inc")
WANT = ["uint16_t Utils::rgb888ToRgb565(", "uint16_t Utils::rgb565dim(", "void Utils::rgb565dimBitmap(", "uint32_t Utils::rgb565ToRgb888(",
        "uint16_t Utils::grayscaleToTargetColor(", "void Utils::colorizeImageData("]
src = open(SRC).read()
out = ["/* Copied unchanged by tools/extract_utils.py from the orbs' original firmware (Utils.cpp). Do not edit. */"]
m = re.search(r"struct GrayscaleToTargetColorCache \{.*?\};\s*GrayscaleToTargetColorCache grayscaleToTargetColorCache;", src, re.S)
assert m, "cache struct"
out.append(m.group(0))
for w in WANT:
    i = src.find(w)
    assert i >= 0, w
    j = src.index("{", i); depth = 0
    for k in range(j, len(src)):
        if src[k] == "{": depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0: break
    out.append(src[i:k + 1])
open(OUT, "w").write("\n\n".join(out) + "\n")
print(OUT, len(WANT), "functions")
