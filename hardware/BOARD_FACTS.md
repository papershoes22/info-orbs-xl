# Board facts: measurements, connections, the XL

Everything measured or read for the adapter (2026-11-14 to 2026-11-16), on one unit whose board is printed
"info orbs v0.2". Lengths in mm. "Caliper" readings were taken by hand on the real parts; "file" facts come from the
original KiCad design files (`board-positions/`).

## Caliper readings (as taken)

| # | What | Reading |
|---|---|---|
| 1 | The ESP32 socket's two rows, centre to centre | 22.5 (the design file says 23.00; the adapter used 23.00 = 9 perfboard holes 22.86) |
| 2 | Holes per socket row | 19 |
| 3 | The ESP32 DevKit's board | 51.52 x 25.90 |
| 4 | The ESP32's metal module, height above the DevKit's own board | 3.32 |
| 5 | The base (the cover the ESP32 points into): inside depth | 16.98 |
| 6 | The orbs board's surface below the case rim (the board is recessed) | 2.99, so 16.98 + 2.99 = **19.97 room above the board** |
| 7 | Free space past the ESP32's ends: toward the case end / toward the middle | 19.07 / 129 |
| 8 | The case USB board to the socket's end | 44.22 |
| 9 | The XL: length with its USB-C / width / parts on top / parts underneath | 77.92 / 21.04 / 5.67 / 2.42 |
| 10 | The socket's height above the board | 8.5 |
| 11 | The case USB board's top above the orbs board | 5.32 |
| 12 | The base's outside height (its posts stand 0.94 above the lip at the screws and in the middle) | 20.65 |
| 13 | The case screws | 20 long |
| 20 | The cut cable's plug body: width | 10.97 |
| 21 | ... thickness | 6.04 |
| 22 | ... tip to the end of its moulding (about 20.93 sticks out past the XL's port) | 27.58 |
| 23 | The shortest wire on hand | 150 |
| 24 | The XL's end to the near edge of its last hole (centre about 1.57) | 1.07 |

(14-19 were photos and meter checks, below.)

## Meter checks

| # | Check | Result |
|---|---|---|
| 16 | The case USB board's two middle pins against the orbs board | Not connected to anything on the orbs board: the board carries USB power only, so the adapter brings its own data wires |
| 17 | The screens' VCC against the case USB board's VCC | Joined (about 0): the screens run on the USB 5 V, as in the v0.2 design file |
| 18 | The screens' GND against the USB GND | Not joined (1): separate grounds, joined only through the ESP32 |
| 19 | Which case USB pin is D+ (with a USB-C breakout, both plug orientations) | D+ = the middle pin next to GND, D- = the middle pin next to VCC. **The board's print shows them the other way round** |

Cable used: a cut USB-C cable whose green wire is D+ and white D- (check any other cable with the meter).

## The v0.2 board (design file: brettdottech/info-orbs commit b6ead4e)

- Outline 198.82 x 28.50, 1.6 thick. The ESP32 (footprint "V1", an ESP32 DevKitC 38-pin) on the BACK, the five 7-pin
  screen headers D1-D5 and the three buttons on the FRONT, the 4-pin case USB header on the back. Positions of every
  part and pad: `board-positions/v0.2_parts.txt`. No mounting holes: the case's posts hold the board.
- Socket rows J2 and J3, 19 pins each, 23.00 apart, 2.54 pitch.
- **Power:** the case USB board's VCC (USB 5 V) goes to the socket's J2-19 "5V", to every screen's VCC and to the
  high side of all three buttons. The socket's 3V3 pin (J2-1) goes only to the breakout pad. Silkscreen: "5V Power
  Only (must be USB A)": the case USB board has no CC resistors, so a USB-C charger that waits for them gives nothing;
  use a USB-A-to-C cable.
- **Three grounds, joined only through the ESP32:** J3-1 = USB ground, J3-7 = all screens' ground, J2-14 = the
  breakout's ground.
- **Signals (ESP32 pin, socket pin):**

| Signal | ESP32 | Socket | XL (adapter) |
|---|---|---|---|
| Screens' SPI clock (SCL on the board) | IO23 | J3-2 | GP34 (SPI0 SCK) |
| Screens' SPI data (SDA on the board) | IO17 | J3-11 | GP7 (SPI0 TX) |
| Screens' DC | IO19 | J3-8 | GP15 |
| Screens' reset | IO18 | J3-9 | GP14 |
| Screen D1 select | IO13 | J2-15 | GP22 |
| Screen D2 select | IO33 | J2-8 | GP16 |
| Screen D3 select | IO32 | J2-7 | GP46 |
| Screen D4 select | IO25 | J2-9 | GP17 |
| Screen D5 select | IO21 | J3-6 | GP31 |
| Left button (high = pressed, 5 V on v0.2) | IO26 | J2-10 | GP18 (pull-down) |
| Middle (OK) button | IO27 | J2-11 | GP19 (pull-down) |
| Right button | IO14 | J2-12 | GP20 (pull-down) |
| USB 5 V | 5V | J2-19 | VB |
| USB ground / screens' ground | GND | J3-1 / J3-7 | GND (both) |

- Each screen header, pins 1-7: reset, select, DC, data, clock, GND (screens'), VCC.
- The breakout pads (U1): 5V, 3.3V, GND (breakout), gpio34, gpio35, gpio12.

## v0.3 differences (design file: `PCBFiles+Schematics/infoorbs-v0_3` in the original repo)

Same outline, the same socket in the same place, the same screen header and button positions (references renamed
tft1-5, SW1-3), plus a thin-DevKit footprint V2 and a 2-pin header J1. **The screens are powered from the ESP32's
3.3 V pin, the buttons switch 3.3 V,** and the case USB header ("USBPower1": VCC, D-, D+, GND) feeds the DevKit's 5 V
with D+/D- unconnected. All grounds are one net there (the USB ground, the screens' ground, the breakout's ground
and J1 pin 1). The adapter in this folder does not connect the socket's 3V3 pin, so it would not power v0.3
screens as drawn. Positions: `board-positions/v0.3_parts.txt`.

## The Pimoroni Pico LiPo 2 XL W

- RP2350B (80 pins), stepping **A4** (read with `picotool info --debug` on ours), 16 MB flash, 8 MB PSRAM (select on
  GP47), RM2 Wi-Fi module (CYW43439) on the underside at the antenna end.
- Board about 76.4 x 21 x 9.9 (Pimoroni's drawing); with its USB-C 77.92 long (reading 9).
- USB D+/D- only at its own USB-C port (27 ohm resistors, no pads); 5.1k CC resistors on board, so a USB-C cable into
  the XL itself works with any charger.
- 3.3 V from an AP7366 600 mA regulator fed from VSYS; VBUS -> VSYS through a diode. Pin 40 VBUS ("VB"), 39 VSYS,
  36 3V3 out.
- On QFN-80 RP2350 parts, GPIO 0-29 are 5 V tolerant while IOVDD (3.3 V) is present; with the chip unpowered the limit
  is 3.63 V (RP2350 datasheet). GPIO 40-47 (ADC pins) are not 5 V tolerant.
- The XL's last 3 holes on each side (at the antenna end) hang past the adapter's edge on purpose: unused, and the
  antenna stays clear of copper.

## Heights and fit with the adapter

- Room above the orbs board: 19.97 with the original base, 22.97 with the 3 mm deeper one (the deeper base's floor
  sits 22.99 above the board in the model).
- The adapter's stack: socket 8.5 + perfboard + the XL on its headers puts the XL's top at about 20.6-21.1 (estimate
  from the parts' sizes), so about 1.9 under the new floor (`check_fit.py`: 1.91).
- `check_fit.py` (with the XL, the plug and the perfboard placed from these numbers): XL 1.91 clear of the floor, the
  plug 2.95, the perfboard 9.39; sideways: XL about 1.3 from the end posts, perfboard 2.25, plug 0.53.

## Not measured

The current the five screens and the XL draw; the 3.3 V regulator's headroom for v0.3 screens; the socket's exact
height on another unit.
