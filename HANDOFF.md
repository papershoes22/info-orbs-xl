# Handoff: a circuit board to replace the adapter

For whoever picks this up next (a person, or a person and their AI assistant). This file says what exists, what was
decided and why, what a replacement board has to match, and what nobody knows yet. Read it with
[hardware/BOARD_FACTS.md](hardware/BOARD_FACTS.md) (every measurement) and [hardware/README.md](hardware/README.md)
(the adapter, its lessons and its trade-offs).

## Where things stand (2026-11-18)

- One pair of Info Orbs (board printed "info orbs v0.2") runs on a Pimoroni Pico LiPo 2 XL W through a hand-soldered
  perfboard adapter in the ESP32's old socket, with a 3 mm deeper printed base. It has been running since
  2026-11-16 (with many firmware updates since) on the firmware in `firmware/`.
- The adapter takes about two hours of careful soldering and a meter. The aim now is a **custom circuit board** so
  nobody has to hand-wire one: ideally a **full replacement main board** for the orbs, with the XL on it.
- The project may later be published to a wider audience with its own board. See "Going independent" at the end.

## What a full replacement board has to match

All positions below are in `hardware/board-positions/v0.2_parts.txt` (and `v0.3_parts.txt`), read from the original
KiCad files: mm, origin at the outline's top-left corner in KiCad's view, x right, y down. Front = the screens' side.

- **Outline 198.82 x 28.50, 1.6 thick.** No mounting holes: the case's posts and lip hold the board. The original
  case: Skeetz1230's "Info Orbs enclosure" (CC BY 4.0, `hardware/source/` has its base; the other parts are on
  Printables) or brett.tech's own case.
- **Five screen headers**, 1 x 7 pins, 2.54 pitch, on the front, all at y 14.25, first pins at x 13.80, 52.79,
  91.78, 130.78 and 169.77 (about 39 apart), pins running +x. Pin order on each: reset, select, DC, data (MOSI), clock,
  GND, VCC. The round GC9A01 screens (240 x 240) stand at right angles to the board on these.
- **Three buttons** (6 mm tactile, `SW_PUSH_6mm_H5mm`) on the front near the bottom edge: left at x 137.55, middle
  151.68, right 165.80 (first pad; pads 6.50 apart in x and 4.50 in y). They line up with the case's three button caps.
- **The USB port:** today a small "V1297" USB-C board stands on the back with its 4 pins at x 115.30-122.92, y 11.00,
  its top 5.32 above the board, and its port looks out through a notch in the base. Get the notch's exact position
  from the case files or with calipers before placing a USB-C connector.
- **Room on the back (inside the base):** 19.97 from the board to the original base's floor, 22.97 with the deeper
  base. The original base can come back if the XL lies low enough.

## The XL and its quirks

- **USB data only at the XL's own USB-C port.** The XL has no D+/D- pins or pads (27 ohm resistors straight to its
  connector). So a carrier board can't route USB data to it through header pins. Ways round it:
  1. Put the XL where **its own USB-C port is the case's USB port** (move the base's notch to match; `make_base.py`
     can be extended). Then nothing else carries USB, the XL's 5.1k CC resistors make any USB-C cable and charger work,
     and the XL's VBUS pin ("VB", pin 40) can feed the screens' 5 V. Probably the simplest board.
  2. A board-mounted USB-C receptacle at the notch, with a short USB-C plug lead into the XL's port (what the adapter
     does today with a cut cable).
  3. Put the RP2350B, flash, PSRAM and a Wi-Fi module on the board itself (no XL). Most work by far: RF, power,
     certification questions. Not advised for a first board.
- **Its Wi-Fi module (RM2)** sits on the XL's underside at the antenna end, 2.42 tall. Keep copper and ground pour
  away from under the antenna, and leave room for the module if the XL faces the board.
- **3.3 V for everything else comes from the XL's 600 mA regulator** (pin 36). How much the five screens draw has
  **not been measured**; measure before powering screens from it.
- **The owner wanted the XL removable**, like the original ESP32 in its socket. The adapter couldn't do it (no
  height); a full board with the XL lying flat probably can (machine-pin sockets, or castellated pads plus a socket
  footprint). Worth asking before deciding.
- **Avoid:** GP47 (the PSRAM's chip select), GP30 (the XL's user button, "BT"), and remember GP40-47 are not 5 V
  tolerant.

## Keep the firmware's pins and nothing in the firmware changes

| Signal | XL pin |
|---|---|
| Screens' SPI clock / data (SPI0) | GP34 / GP7 |
| Screens' DC / reset (shared by all five) | GP15 / GP14 |
| Screen select D1 ... D5 | GP22, GP16, GP46, GP17, GP31 |
| Buttons left / middle / right (active high, the chip's pull-downs on) | GP18 / GP19 / GP20 |

Defined in `firmware/src/lcd.c` (screens) and `firmware/src/main.cpp` (buttons). If a board needs other pins, change
them there; nothing else depends on them. The screen link runs at 42 MHz by default (up to 125 MHz was tried), so
keep the SPI lines short and give them a solid ground return.

**Do these better than the adapter could:**
- Buttons switching **3.3 V** (or a 10k/15k divider from 5 V), not 5 V straight into the pins.
- **One ground.** v0.2 had three grounds joined only through the ESP32.
- **CC resistors** (5.1k to GND on CC1 and CC2) on any USB-C receptacle the board has, so a USB-C charger powers it.
  The original needed a USB-A-to-C cable.

## A smaller step, if a full board is too much

A **plug-in board for the existing ESP32 socket**: the perfboard adapter as a real circuit board (two 19-pin rows
23.00 apart, the XL on top, the 15 links as traces, button dividers, a pad for the USB data lead). Owners keep their
screens and board and swap one part. Height is the catch: the XL soldered flat on such a board just fits under the
original base by arithmetic (about 17.6 of 19.97), and with ordinary header plastic it's about 0.1 too tall; a
socketed XL needs the deeper base. These numbers are a guess until drawn.

## Checking a first board

- `firmware/first-light/`: the first test firmware (each screen shows its own name, so a swapped select line shows at
  once; colour bars; the buttons). It isn't built by `CMakeLists.txt` any more: add an executable for it.
- `python3 -I firmware/tools/orbs_xl.py hello` (USB data and Wi-Fi strength: `rssi`), `buttons` (every press).
- Measure the current at 5 V with all five screens white at full brightness.
- The meter checks in `hardware/xl_build_guide.pdf` part E show which pins should and shouldn't be joined.

## Open questions

- Board version on the units to be converted (v0.2 or v0.3)? A full replacement board makes this matter less.
- Are the screens soldered into the board or on sockets? A full board means moving them.
- Which USB arrangement (above)?
- XL socketed or soldered?
- Battery: the XL has a LiPo charger and connector. Nothing here uses it yet.

## Going independent

The board can be new work if it is designed from the measurements here rather than copied from the original KiCad
files (the positions have to match to fit the case and screens; that's a fact, not a copy). The firmware would stay
AGPL v3 either way unless the parts taken from the original firmware are rewritten: the Clock, Weather and Matrix
widgets, the screen manager, the colour helpers, and the original's Nixie and weather pictures (see
`THIRD_PARTY.md`). "Info Orbs" is brett.tech's name for their product, so a wider release would want its own name.
