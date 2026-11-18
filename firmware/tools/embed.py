"""Puts the orbs' own fonts and pictures into the XL firmware as C arrays (2026-11-16): the same files the original
Info Orbs firmware embedded (github.com/brettdottech/info-orbs: fonts/, images/), copied into assets/ with their
licences. Writes src/assets.c + src/assets.h; build.sh runs it."""
import os
HERE = os.path.dirname(os.path.abspath(__file__))
ORIG = os.path.join(HERE, "..", "assets")
FILES = [  # C name, path under the original firmware
    ("font_dseg7", "fonts/DSEG7ModernBold.ttf"), ("font_dseg14", "fonts/DSEG14ModernBold.ttf"),
    ("font_roboto", "fonts/RobotoRegular.ttf"), ("font_final_frontier", "fonts/FinalFrontier.ttf"),
] + [(f"nixie_{i}", f"images/clock/nixie.no-holes/{i}.jpg") for i in range(12)] + [
    (f"wx_{n}{m}", f"images/weather/{'dark' if m == 'B' else 'light'}/{n}{m}.jpg")
    for n in ("moonCloud", "sunClouds", "sun", "moon", "snow", "rain", "clouds") for m in ("B", "W")]
h = ["/* made by tools/embed.py from assets/ (the original Info Orbs firmware's fonts and images); do not edit */", "#pragma once",
     "#include <stdint.h>", "#include <stddef.h>", '#ifdef __cplusplus', 'extern "C" {', "#endif"]
c = ["/* made by tools/embed.py; do not edit */", '#include "assets.h"']
for name, rel in FILES:
    data = open(os.path.join(ORIG, rel), "rb").read()
    h.append(f"extern const uint8_t {name}[{len(data)}];  /* {rel} */")
    rows = [",".join(str(b) for b in data[i:i + 32]) for i in range(0, len(data), 32)]
    c.append(f"const uint8_t {name}[{len(data)}] __attribute__((aligned(4))) = {{\n" + ",\n".join(rows) + "\n};")
h.append("extern const uint8_t *const nixie_jpg[12];")
h.append("extern const size_t nixie_jpg_len[12];")
c.append("const uint8_t *const nixie_jpg[12] = {" + ", ".join(f"nixie_{i}" for i in range(12)) + "};")
c.append("const size_t nixie_jpg_len[12] = {" + ", ".join(f"sizeof nixie_{i}" for i in range(12)) + "};")
# the settings page (web/index.html, 2026-11-17), compressed: the XL sends it as it is, with Content-Encoding: gzip
import gzip
for cname, fname in (("web_index_gz", "index.html"), ("web_wifi_gz", "wifi.html")):   # (wifi: the setup hotspot's page)
    page = gzip.compress(open(os.path.join(HERE, "..", "web", fname), "rb").read(), compresslevel=9, mtime=0)
    h.append(f"extern const uint8_t {cname}[{len(page)}];  /* web/{fname}, gzip */")
    h.append(f"#define {cname}_len ((size_t){len(page)})")
    rows = [",".join(str(b) for b in page[i:i + 32]) for i in range(0, len(page), 32)]
    c.append(f"const uint8_t {cname}[{len(page)}] __attribute__((aligned(4))) = {{\n" + ",\n".join(rows) + "\n};")
h += ["#ifdef __cplusplus", "}", "#endif"]
open(os.path.join(HERE, "..", "src", "assets.h"), "w").write("\n".join(h) + "\n")
open(os.path.join(HERE, "..", "src", "assets.c"), "w").write("\n".join(c) + "\n")
print("assets:", sum(os.path.getsize(os.path.join(ORIG, r)) for _, r in FILES) // 1024, "KB")
