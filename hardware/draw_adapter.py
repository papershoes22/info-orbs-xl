#!/usr/bin/env python3
"""draw_adapter.py: layout sketch of the XL adapter for the orbs (2026-11-15). NOT a soldering guide yet.

  python3 -I draw_adapter.py OUT_DIR

Writes adapter_top.png (seen from the base side, like the photos of the board's back: case end at the top,
middle of the orbs at the bottom) and adapter_side.png (cut along the length). Every label is the boards' own print:
the socket pins carry the ESP32's printed names (printed on the ESP32's underside), the XL pins the XL's own print
(../photos/07-xl-top.jpg). Socket positions and what each socket pin drives come from the orbs board's v0.2 design file
(brettdottech/info-orbs commit b6ead4e), the same board as ours ("info orbs v0.2" printed on it).
"""
import os, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, FancyBboxPatch

P = 2.54
J2 = ["3V3", "EN", "SVP", "SVN", "P34", "P35", "P32", "P33", "P25", "P26", "P27", "P14", "P12", "GND", "P13", "SD2",
      "SD3", "CMD", "5V"]                                   # left socket row, case end first (ESP32 underside print)
J3 = ["GND", "P23", "P22", "TX", "RX", "P21", "GND", "P19", "P18", "P5", "P17", "P16", "P4", "P0", "P2", "P15",
      "SD1", "SD0", "CLK"]                                  # right socket row
ORBS = {  # what each used socket pin is wired to on the v0.2 orbs board
    ("L", 6): "screen 3 CS", ("L", 7): "screen 2 CS", ("L", 8): "screen 4 CS", ("L", 9): "LFT button (5 V)",
    ("L", 10): "OK button (5 V)", ("L", 11): "RHT button (5 V)", ("L", 14): "screen 1 CS", ("L", 18): "USB 5 V",
    ("R", 0): "USB GND", ("R", 1): "screens SCL", ("R", 5): "screen 5 CS", ("R", 6): "screens GND",
    ("R", 7): "screens DC", ("R", 8): "screens RST", ("R", 10): "screens SDA"}
# XL print, from the USB end (k = 0) to the antenna end (k = 29)
XL_L = ["VB", "VS", "-", "3E", "+", "VA", "$28", "-", "$27", "$26", "RU", "22", "-", "21", "20", "19", "18", "-",
        "17", "16", "BT", "$47", "-", "$46", "$45", "$44", "$43", "-", "39", "38"]
XL_R = ["0", "1", "-", "2", "3", "4", "5", "-", "6", "7", "8", "9", "-", "10", "11", "12", "13", "-", "14", "15",
        "+", "31", "-", "32", "33", "34", "35", "-", "36", "37"]
ROW_OF_K = lambda k: 26 - k                                 # XL pin k sits in perfboard row 26 - k
LINKS = [  # (socket side, socket row, XL side, XL row, kind, note)
    ("R", 1, "R", 1, "link", "SCL -> XL 34"),
    ("R", 5, "R", 5, "link", "screen 5 CS -> XL 31"),
    ("R", 7, "R", 7, "link", "DC -> XL 15"),
    ("R", 8, "R", 8, "link", "RST -> XL 14"),
    ("R", 10, "R", 17, "wire", "SDA -> XL 7"),
    ("R", 0, "R", 4, "wire", "USB GND -> XL GND"),
    ("R", 6, "R", 4, "wire", "screens GND -> XL GND"),
    ("L", 7, "L", 7, "link", "screen 2 CS -> XL 16"),
    ("L", 8, "L", 8, "link", "screen 4 CS -> XL 17"),
    ("L", 6, "L", 3, "wire", "screen 3 CS -> XL 46"),
    ("L", 14, "L", 15, "wire", "screen 1 CS -> XL 22"),
    ("L", 18, "L", 26, "wire", "USB 5 V -> XL VB"),
    ("L", 9, "L", 10, "res", "LFT -> 10k -> XL 18"),
    ("L", 10, "L", 11, "res", "OK -> 10k -> XL 19"),
    ("L", 11, "L", 12, "res", "RHT -> 10k -> XL 20"),
]
COL = {"L": 0, "R": 9}
XCOL = {"L": 1, "R": 8}
KIND_COLOUR = {"link": "tab:blue", "wire": "tab:orange", "res": "tab:purple"}


def xy(col, row):
    return col * P, -row * P


def top(out):
    fig, ax = plt.subplots(figsize=(13, 17), dpi=110)
    # perfboard
    ax.add_patch(Rectangle((-0.5 * P, -26.5 * P), 10 * P, 27 * P, fc="#e9dcc0", ec="#8a7a55", lw=1.2, zorder=0))
    for c in range(10):
        for r in range(27):
            x, y = xy(c, r); ax.plot(x, y, "o", ms=3, color="#b49a5e", zorder=1)
    # XL outline: antenna end 1.37 past pin k=29 (row -3), USB end 1.37 past k=0 (row 26), receptacle +1.5
    xl_top = 3 * P + 1.37; xl_bot = -(26 * P + 1.37)
    ax.add_patch(Rectangle((0.91, xl_bot), 21.04, xl_top - xl_bot, fc="#26262699", ec="black", lw=1.5, zorder=2))
    ax.add_patch(Rectangle((0.91 + 21.04 / 2 - 4.5, xl_bot - 1.5), 9.0, 7.5, fc="#c0c0c0", ec="black", zorder=3))
    ax.text(11.43, xl_bot + 2.2, "XL USB-C", ha="center", fontsize=8, zorder=4)
    ax.add_patch(Rectangle((2.0, xl_top - 17.0), 18.9, 17.0, fc="none", ec="#39b54a", lw=1.5, ls="--", zorder=3))
    ax.annotate("XL's Wi-Fi module (on its underside):\nantenna end hangs past the perfboard;\nrows 0-3 between the XL rows: nothing\npokes through the top, tape under it",
                xy=(2.0, xl_top - 8), xytext=(-29, 7), fontsize=8, color="#2e8b3a", zorder=4,
                arrowprops=dict(arrowstyle="->", color="#2e8b3a"))
    # socket pins and labels
    for r in range(19):
        for side, names in (("L", J2), ("R", J3)):
            x, y = xy(COL[side], r)
            used = (side, r) in ORBS
            ax.plot(x, y, "s", ms=7, color="black" if used else "#777777", zorder=5)
            lab = names[r] + (f"  {ORBS[(side, r)]}" if used else "")
            if side == "L":
                ax.text(x - 1.6, y, lab, ha="right", va="center", fontsize=8, fontweight="bold" if used else "normal")
            else:
                ax.text(x + 1.6, y, lab, ha="left", va="center", fontsize=8, fontweight="bold" if used else "normal")
    # XL pins (rows -3..26) with XL print
    for k in range(30):
        r = ROW_OF_K(k)
        for side, names in (("L", XL_L), ("R", XL_R)):
            x, y = xy(XCOL[side], r)
            on_board = r >= 0
            ax.plot(x, y, "o", ms=6, mfc="#d4a017" if on_board else "none", mec="#d4a017", zorder=6)
            ax.text(x + (0.9 if side == "L" else -0.9), y, names[k], ha="left" if side == "L" else "right",
                    va="center", fontsize=6.5, color="white", zorder=7)
    # connections
    for s_side, s_row, x_side, x_row, kind, note in LINKS:
        x1, y1 = xy(COL[s_side], s_row); x2, y2 = xy(XCOL[x_side], x_row)
        if kind == "link":
            ax.plot([x1, x2], [y1, y2], color=KIND_COLOUR[kind], lw=3, zorder=8)
        else:
            mid = 3 * P if s_side == "L" else 6 * P
            ax.plot([x1, mid, mid, x2], [y1, y1, y2, y2], color=KIND_COLOUR[kind], lw=2,
                    ls="-" if kind == "wire" else (0, (1, 1)), zorder=8)
    for g in (10, 11, 12):
        x1, y1 = xy(1, g); ax.plot([x1, 4 * P, 4 * P], [y1, y1 - 0.4, -14 * P], color="tab:purple", lw=1, ls=":", zorder=8)
    ax.text(4 * P + 0.5, -13.3 * P, "15k from each\nbutton pin to\nXL GND (row 14)", fontsize=7, color="tab:purple")
    # notes
    ax.text(-0.5 * P, 2.6 * P + 7, "XL ADAPTER: LAYOUT SKETCH (not the soldering guide)\n"
            "seen from the base side, like the photos of the board's back: case end at the top",
            fontsize=12, fontweight="bold", va="bottom")
    ax.text(-0.5 * P, -29.2 * P,
            "squares = pins going into the orbs' socket (ESP32 names; bold = used, with what they drive on a v0.2 board)\n"
            "circles = XL pins (XL's own print). Blue = joined to the pin right next to it.  Orange = short wire.\n"
            "Purple = through a 10k resistor (button lines are 5 V on a v0.2 board; 10k + 15k makes them 3.0 V for the XL).\n"
            "NOT joined: right row 6 (screens GND) sits next to XL '+' (3.3 V); left row 5 sits next to XL $47 (memory chip).\n"
            "XL 3.3 V is not used: on a v0.2 board the screens and buttons run off USB 5 V, nothing needs 3.3 V.\n"
            "All joints on the perfboard's underside. Middle of the orbs (case USB board, about 30 mm further) is below.",
            fontsize=9, va="top")
    ax.set_xlim(-30, 52); ax.set_ylim(-80, 22); ax.set_aspect("equal"); ax.axis("off")
    fig.savefig(os.path.join(out, "adapter_top.png"), bbox_inches="tight")


def side(out):
    fig, ax = plt.subplots(figsize=(16, 6.5), dpi=110)
    # s = mm along the socket from pin 1 (case end); H = mm above the orbs board's back
    ax.add_patch(Rectangle((-24, -1.6), 135, 1.6, fc="#2b2b2b"))                      # orbs board
    ax.text(100, -1.2, "orbs board (back side)", color="white", fontsize=8, va="center")
    ax.add_patch(Rectangle((-1.27, 0), 19 * P, 8.5, fc="#444444"))                    # socket
    ax.text(22, 4, "socket 8.5 (reading 10)", color="white", ha="center", fontsize=8)
    ax.add_patch(Rectangle((-0.5 * P, 9.2), 27 * P, 1.6, fc="#e9dcc0", ec="#8a7a55"))  # perfboard (on its joints)
    ax.text(50, 10.0, "perfboard", fontsize=8, va="center")
    ax.add_patch(Rectangle((0, 10.8), 26 * P, 2.5, fc="#111111"))                      # XL header plastic
    xl0 = -(3 * P + 1.37); xl1 = 26 * P + 1.37
    ax.add_patch(Rectangle((xl0, 13.3), xl1 - xl0, 1.6, fc="#262626"))                # XL board
    ax.add_patch(Rectangle((xl0, 13.3 - 2.42), 17.0, 2.42, fc="#39b54a"))              # RM2 underneath
    ax.add_patch(Rectangle((xl0 + 1, 14.9), 7, 5.67, fc="#efe7d2", ec="#999"))         # LiPo socket (tallest)
    ax.add_patch(Rectangle((xl1 - 7.5, 14.9), 9.0, 3.2, fc="#c0c0c0"))                 # USB-C
    ax.add_patch(Rectangle((xl1 + 1.5 - 6.5, 14.9 + 1.6 - 1.2), 17, 2.4, fc="#5b8fd6", alpha=0.85))  # plug
    ax.text(xl1 + 5, 19.2, "USB-C plug in the XL\n(plug board or cut cable)", fontsize=8, ha="center")
    ax.plot([xl1 + 9, 96, 98], [16.1, 16.1, 5.6], color="tab:red", lw=1.5)
    ax.text(84, 16.6, "D+ / D- wires", fontsize=8, color="tab:red")
    ax.add_patch(Rectangle((95.8 - 2, 0), 9.6, 5.32, fc="#2e8b57"))                    # case USB board
    ax.text(98.8, 2.5, "case USB\nboard 5.32", color="white", fontsize=7, ha="center", va="center")
    ax.add_patch(Rectangle((74.9, 1.74), 10, 11.39 - 1.74, fc="#7fb06988", ec="#4d7a3a"))   # base middle posts
    ax.text(79.9, 6.5, "base\nmiddle\nposts", fontsize=7, ha="center")
    ax.add_patch(Rectangle((-20.4, 2.99), 7.8, 20.0, fc="#7fb06955", ec="#4d7a3a"))   # screw column region
    ax.text(-16.5, 12, "base\nscrew\ncolumn", fontsize=7, ha="center")
    for H, txt, c in ((19.99, "the original base's inside floor (19.99 above the board)", "0.5"),
                      (22.99, "new base's inside floor (+3.0 = 22.99)", "tab:green")):
        ax.axhline(H, color=c, lw=1.3, ls="--"); ax.text(-23, H + 0.3, txt, fontsize=8, color=c)
    ax.text(30, 21.2, "XL top (LiPo socket) about 20.6-21.1", fontsize=8)
    ax.set_xlim(-25, 112); ax.set_ylim(-3, 25); ax.set_aspect("equal")
    ax.set_xlabel("mm along the orbs (case end on the left, middle of the orbs on the right)")
    ax.set_ylabel("mm above the board")
    ax.set_title("XL adapter: side view (cut along the length)", fontsize=12)
    fig.savefig(os.path.join(out, "adapter_side.png"), bbox_inches="tight")


if __name__ == "__main__":
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    top(out); side(out); print(out)
