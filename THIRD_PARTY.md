# Other people's work in this repository

The whole repository is under the GNU AGPL v3 ([LICENSE](LICENSE)) because the firmware reuses code from the
original Info Orbs firmware. Each item below keeps its own licence; its licence file sits next to it.

| What | Where | From | Licence |
|---|---|---|---|
| The original Info Orbs firmware: the Clock, Weather and Matrix widgets and the screen manager (adapted to draw into PSRAM), its colour helpers, the GC9A01 start-up sequence it used | `firmware/src/screen.*`, `clock_widget.*`, `weather.*`, `matrix_widget.h`, `compat.h`, `lcd.c`, `third_party/orbs_utils.inc` | [brettdottech/info-orbs](https://github.com/brettdottech/info-orbs) | AGPL-3.0 (`firmware/third_party/info-orbs-LICENSE.txt`) |
| The Matrix rain animation | `firmware/src/DigitalRainAnimation.hpp` | the original Info Orbs firmware (its file says public domain) | public domain |
| The Nixie tube and weather icon pictures | `firmware/assets/images/` | the original Info Orbs firmware | AGPL-3.0, as part of that repository |
| DSEG7 Modern Bold, DSEG14 Modern Bold | `firmware/assets/fonts/` | keshikan (via the original firmware) | SIL Open Font License 1.1 (`DSEG-LICENSE.txt`) |
| Roboto Regular | `firmware/assets/fonts/` | Google (via the original firmware) | Apache 2.0 (`Roboto-LICENSE.txt`) |
| Barlow (glyphs rendered into the first-light test's `fonts.c`) | `firmware/first-light/fonts.c` | Jeremy Tribby, via Google Fonts | SIL Open Font License 1.1 |
| Final Frontier | `firmware/assets/fonts/` | Allen R. Walden (via the original firmware) | freeware: its own file says "UNPROTECTED - Please distribute freely" |
| TFT_eSPI's smooth drawing functions (arcs, circles, wide lines, alpha blending), copied unchanged | `firmware/third_party/tft_espi_smooth.inc` | [Bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) 2.5.43 | FreeBSD and MIT parts (`TFT_eSPI-license.txt`) |
| Adafruit GFX 5x7 font | `firmware/third_party/glcdfont.c` | Adafruit GFX library | BSD (Adafruit) |
| OpenFontRender with FreeType 2.4.12 | `firmware/third_party/OpenFontRender/` | [takkaO/OpenFontRender](https://github.com/takkaO/OpenFontRender) (via the original firmware) | FreeType License (`LICENSE`, `FTL.TXT`; added here: the original repository carried no licence file for it); some non-FreeType files MIT. Credit: Portions of this software are copyright © 1996-2013 The FreeType Project (www.freetype.org). All rights reserved. |
| ArduinoJson 7.4.3 | `firmware/third_party/ArduinoJson/` | Benoît Blanchon | MIT (`LICENSE.txt`) |
| JPEGDEC | `firmware/third_party/JPEGDEC/` | Larry Bank, BitBank Software | Apache 2.0 (`LICENSE`) |
| TJpgDec (Bodmer's TJpg_Decoder copy) | `firmware/third_party/tjpgd_bodmer/` | ChaN, Bodmer | ChaN's TJpgDec licence (`license.txt`) |
| QR Code generator | `firmware/third_party/qrcodegen/` | Project Nayuki | MIT (`license.txt`) |
| Mbed TLS 3.6.6 | `firmware/third_party/mbedtls/` | Trusted Firmware / Arm | Apache-2.0 or GPL-2.0-or-later (`LICENSE`) |
| `pico_sdk_import.cmake`; lwIP settings in `src/lwipopts.h` | `firmware/` | Raspberry Pi Pico SDK and pico-examples | BSD 3-Clause |
| "Info Orbs enclosure" base, and the 3 mm deeper remix of it | `hardware/source/bottom_20mmscrews.stl`, `hardware/base_xl_3mm.stl`, `hardware/print_base_xl_3mm.stl`, `hardware/orbs_base_xl_petg.3mf` | [Skeetz1230 on Printables](https://www.printables.com/model/1016115-info-orbs-enclosure) | CC BY 4.0 (see `hardware/source/README.md`) |
| Part positions of the original boards | `hardware/board-positions/*.txt` | read from [brettdottech/info-orbs](https://github.com/brettdottech/info-orbs)' KiCad files | facts taken from AGPL-3.0 files |

## Not in this repository, but in every firmware file built from it

The Raspberry Pi Pico SDK (BSD 3-Clause) and, through it, TinyUSB (MIT), lwIP (BSD) and the CYW43 Wi-Fi driver. The
CYW43 driver has its own licence (in the SDK's `lib/cyw43-driver`): it may be used only with Raspberry Pi's own chips,
which the XL's RP2350 is. Anyone handing out a built `.uf2` should read that licence next to the AGPL.
