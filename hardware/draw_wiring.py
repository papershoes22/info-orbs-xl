#!/usr/bin/env python3
"""draw_wiring.py: the XL adapter's soldering drawings (2026-11-15). Replaces the layout sketch (draw_adapter.py).

  python3 -I draw_wiring.py OUT_DIR

Writes:
  wiring_1_top.png    the adapter board from the TOP (component side): where the 4 strips and the XL go, the 4 pins
                      pulled out of the XL strips, the marker dot. Case end at the top, like the photos.
  wiring_2_under.png  the adapter board from the UNDERSIDE (solder side, turned over left to right, so the columns
                      are mirrored): every link and wire, its name, colour, route and both ends.
  wiring_3_side.png   which way the strips go in: plastic on top, long ends down; the XL on top.
  wiring_4_usb.png    the case USB board (orbs board's back): which pin gets green (D+) and white (D-).
Labels are the boards' own print: socket pins = the ESP32's names (printed on its underside), XL pins = the XL's print
(../photos/07-xl-top.jpg). What each socket pin drives is the orbs board's v0.2 design file (brettdottech/info-orbs b6ead4e).
"""
import os, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, FancyArrowPatch

P = 2.54
J2 = ["3V3", "EN", "SVP", "SVN", "P34", "P35", "P32", "P33", "P25", "P26", "P27", "P14", "P12", "GND", "P13", "SD2",
      "SD3", "CMD", "5V"]
J3 = ["GND", "P23", "P22", "TX", "RX", "P21", "GND", "P19", "P18", "P5", "P17", "P16", "P4", "P0", "P2", "P15",
      "SD1", "SD0", "CLK"]
XL_L = ["VB", "VS", "-", "3E", "+", "VA", "$28", "-", "$27", "$26", "RU", "22", "-", "21", "20", "19", "18", "-",
        "17", "16", "BT", "$47", "-", "$46", "$45", "$44", "$43", "-", "39", "38"]
XL_R = ["0", "1", "-", "2", "3", "4", "5", "-", "6", "7", "8", "9", "-", "10", "11", "12", "13", "-", "14", "15",
        "+", "31", "-", "32", "33", "34", "35", "-", "36", "37"]
xl_label = lambda col, row: (XL_L if col == 1 else XL_R)[26 - row]
PULLED = [(1, 6), (8, 0), (8, 6), (8, 10)]            # XL strip pins pulled out before fitting
USED = {  # socket pin (col, row): what it drives on the orbs board (v0.2)
    (0, 6): "screen D3 CS", (0, 7): "screen D2 CS", (0, 8): "screen D4 CS", (0, 9): "LFT button",
    (0, 10): "OK button", (0, 11): "RHT button", (0, 14): "screen D1 CS", (0, 18): "USB 5 V",
    (9, 0): "USB ground", (9, 1): "screens SCL", (9, 5): "screen D5 CS", (9, 6): "screens ground",
    (9, 7): "screens DC", (9, 8): "screens RST", (9, 10): "screens SDA"}
# connections: name, colour, kind, route as (col, row) points from the socket pin to the XL pin
LINKS = [
    ("R1", "tab:blue", "bare", [(9, 1), (8, 1)]),
    ("R2", "tab:blue", "bare", [(9, 5), (8, 5)]),
    ("R3", "tab:blue", "bare", [(9, 7), (8, 7)]),
    ("R4", "tab:blue", "bare", [(9, 8), (8, 8)]),
    ("R5", "#d4a017", "wire", [(9, 10), (7, 10), (7, 17), (8, 17)]),
    ("R6", "black", "wire", [(9, 0), (7, 0), (7, 4), (8, 4)]),
    ("R7", "black", "wire", [(9, 6), (7, 6), (7, 4), (8, 4)]),
    ("L1", "tab:blue", "bare", [(0, 7), (1, 7)]),
    ("L2", "tab:blue", "bare", [(0, 8), (1, 8)]),
    ("L3", "#d4a017", "wire", [(0, 6), (2, 6), (2, 3), (1, 3)]),
    ("L4", "tab:cyan", "short", [(0, 14), (1, 15)]),
    ("L5", "tab:cyan", "short", [(0, 9), (1, 10)]),
    ("L6", "tab:cyan", "short", [(0, 10), (1, 11)]),
    ("L7", "tab:cyan", "short", [(0, 11), (1, 12)]),
    ("L8", "tab:red", "wire", [(0, 18), (0, 26), (1, 26)]),
]


def board(ax, mirror):
    X = (lambda c: (9 - c) * P) if mirror else (lambda c: c * P)
    ax.add_patch(Rectangle((-0.5 * P, -26.5 * P), 10 * P, 27 * P, fc="#e9dcc0", ec="#8a7a55", lw=1.4, zorder=0))
    for c in range(10):
        for r in range(27):
            ax.add_patch(Circle((X(c), -r * P), 0.5, fc="white", ec="#b49a5e", lw=0.6, zorder=1))
    dx = X(0) + (-1.9 if not mirror else 1.9)
    ax.add_patch(Circle((dx, 1.9), 0.75, fc="black", zorder=2))
    ax.text(dx + (-1.2 if not mirror else 1.2), 1.9, "marker dot\n(column 0, row 0)", fontsize=7.5, va="center",
            ha="right" if not mirror else "left")
    for r in range(0, 27, 2):
        ax.text(X(9) + (1.6 if not mirror else -1.6) * 0 + (6.5 if not mirror else -6.5) * 0, -r * P, "", fontsize=1)
    return X


def row_numbers(ax, X, side_x):
    for r in range(27):
        ax.text(side_x, -r * P, f"r{r}", fontsize=6.5, color="#6a5a35", va="center", ha="center")


def top(out):
    fig, ax = plt.subplots(figsize=(13, 19), dpi=110)
    X = board(ax, mirror=False)
    xl_top, xl_bot = 3 * P + 1.57, -(26 * P + 1.57)
    ax.add_patch(Rectangle((0.91, xl_bot), 21.04, xl_top - xl_bot, fc="#26262640", ec="black", lw=1.6, ls="--", zorder=3))
    ax.add_patch(Rectangle((11.43 - 4.5, xl_bot - 1.5), 9.0, 7.4, fc="#c0c0c0", ec="black", zorder=4))
    ax.text(11.43, xl_bot + 2.0, "XL's USB-C", ha="center", fontsize=8, zorder=5)
    ax.text(11.43, xl_top - 4, "XL goes on top, its print facing up,\nUSB-C at THIS end (row 26) ->\n"
            "its 'VB' corner over column 1", ha="center", fontsize=8, zorder=5)
    ax.text(11.43, -60, "", fontsize=1)
    for col, strip_rows, fc in ((0, range(19), "#333333"), (9, range(19), "#333333"),
                                (1, range(27), "#555555"), (8, range(27), "#555555")):
        ax.add_patch(Rectangle((X(col) - 1.25, -(max(strip_rows) + 0.5) * P), 2.5, (len(strip_rows)) * P,
                               fc=fc, alpha=0.35, ec="black", lw=0.8, zorder=2))
        for r in strip_rows:
            if (col, r) in PULLED:
                ax.plot([X(col) - 0.8, X(col) + 0.8], [-r * P - 0.8, -r * P + 0.8], color="red", lw=2.2, zorder=6)
                ax.plot([X(col) - 0.8, X(col) + 0.8], [-r * P + 0.8, -r * P - 0.8], color="red", lw=2.2, zorder=6)
            else:
                ax.add_patch(Rectangle((X(col) - 0.32, -r * P - 0.32), 0.64, 0.64, fc="black", zorder=6))
    for r in range(19):
        ax.text(X(0) - 1.8, -r * P, J2[r] + (f"  {USED[(0, r)]}" if (0, r) in USED else ""), ha="right", va="center",
                fontsize=7.5, fontweight="bold" if (0, r) in USED else "normal")
        ax.text(X(9) + 1.8, -r * P, J3[r] + (f"  {USED[(9, r)]}" if (9, r) in USED else ""), ha="left", va="center",
                fontsize=7.5, fontweight="bold" if (9, r) in USED else "normal")
    for r in range(27):
        for col in (1, 8):
            ax.text(X(col) + (0.9 if col == 1 else -0.9), -r * P + 0.05, xl_label(col, r),
                    ha="left" if col == 1 else "right", va="center", fontsize=6.3, color="#1d4f91", zorder=7)
    for c in range(10):
        ax.text(X(c), 3.0 + 0.2, f"c{c}", ha="center", fontsize=7, color="#6a5a35")
    row_numbers(ax, X, X(9) + 17 + 0 * P)
    ax.text(-0.5 * P, 12.5, "1. TOP SIDE (component side): where the strips and the XL go", fontsize=13,
            fontweight="bold")
    ax.text(-0.5 * P, 9.6, "Case end at the top (row 0 = the socket's pin 1 end). Black squares = header pins. "
            "Red X = pin pulled out of the XL strip.\nLeft names = the ESP32 names of the socket row nearest the case "
            "USB board; right names = the other socket row. Blue = the XL's own print above each pin.",
            fontsize=8.5)
    ax.text(-0.5 * P, -27.6 * P,
            "Socket strips: 19 pins, columns c0 and c9, rows r0-r18.   XL strips: 27 pins, columns c1 and c8, rows r0-r26.\n"
            "Pull out BEFORE fitting: from the c1 strip the pin for row 6 (under XL 'BT'); from the c8 strip the pins for\n"
            "rows 0, 6 and 10 (under XL '35', '+' and '13'). The XL's own holes above them stay empty, never soldered.",
            fontsize=9, va="top")
    ax.set_xlim(-38, 62); ax.set_ylim(-80, 15); ax.set_aspect("equal"); ax.axis("off")
    fig.savefig(os.path.join(out, "wiring_1_top.png"), bbox_inches="tight")


def under(out):
    fig, ax = plt.subplots(figsize=(13, 19), dpi=110)
    X = board(ax, mirror=True)
    for col, strip_rows in ((0, range(19)), (9, range(19)), (1, range(27)), (8, range(27))):
        for r in strip_rows:
            if (col, r) in PULLED:
                ax.text(X(col), -r * P, "empty", fontsize=5.5, color="red", ha="center", va="center", zorder=6)
            else:
                big = col in (0, 9)
                ax.add_patch(Circle((X(col), -r * P), 0.95 if big else 0.8, fc="#c9c9c9" if big else "#e3c27a",
                                    ec="black", lw=0.8, zorder=5))
    for r in range(19):
        ax.text(X(0) + 1.9, -r * P, J2[r] + (f"  {USED[(0, r)]}" if (0, r) in USED else ""), ha="left", va="center",
                fontsize=7.5, fontweight="bold" if (0, r) in USED else "normal")
        ax.text(X(9) - 1.9, -r * P, (f"{USED[(9, r)]}  " if (9, r) in USED else "") + J3[r], ha="right", va="center",
                fontsize=7.5, fontweight="bold" if (9, r) in USED else "normal")
    for name, colour, kind, pts in LINKS:
        xs = [X(c) for c, r in pts]; ys = [-r * P for c, r in pts]
        lw = {"bare": 3.2, "short": 2.6, "wire": 2.4}[kind]
        ax.plot(xs, ys, color=colour, lw=lw, solid_capstyle="round", zorder=8)
        if colour == "black":
            ax.plot(xs, ys, color="white", lw=0.6, ls=(0, (2, 3)), zorder=9)
        mx = (xs[0] + xs[1]) / 2 if kind != "wire" else xs[1]
        my = (ys[0] + ys[1]) / 2 if kind != "wire" else (ys[1] + ys[2]) / 2
        off = {"R6": (0.9, 1.2), "R7": (0.9, -1.0), "R5": (0.9, 0), "L3": (-0.9, 0), "L8": (-0.9, 0)}.get(name, (0, 1.05))
        ax.text(mx + off[0], my + off[1], name, fontsize=7.5, fontweight="bold", color="#222", ha="center",
                va="center", zorder=10, bbox=dict(fc="white", ec="none", pad=0.4, alpha=0.85))
        c1, r1 = pts[-1]
        ax.text(X(c1) + (-1.1 if c1 == 1 else 1.1), -r1 * P - 0.05, f"XL {xl_label(c1, r1)}",
                ha="right" if c1 == 1 else "left", va="center", fontsize=6.2, color="#1d4f91", zorder=10)
    for c in range(10):
        ax.text(X(c), 3.0 + 0.2, f"c{c}", ha="center", fontsize=7, color="#6a5a35")
    row_numbers(ax, X, X(0) + 22)
    ax.text(-0.5 * P, 12.5, "2. UNDERSIDE (solder side): every link and wire", fontsize=13, fontweight="bold")
    ax.text(-0.5 * P, 9.6, "The board turned over left to right: rows stay top to bottom (row 0, the case end, still "
            "at the top) but column c0 is now on the RIGHT.\nThe marker dot is top right. Grey = socket strip pins "
            "(long, they go into the orbs' socket). Gold = XL strip pins.", fontsize=8.5)
    ax.text(-0.5 * P, -27.6 * P,
            "Blue = bare link to the pin right beside it (5 mm of bare solid wire across both joints).\n"
            "Cyan = short insulated link to the pin one row further (8 mm piece, 2 mm bare at each end).\n"
            "Gold / red / black = insulated wire along the route shown, lying flat on the board.\n"
            "Every wire end is soldered INTO a pin's joint. No wire goes through a hole (nothing may poke up under the XL).\n"
            "Two black wires (R6, R7) both end in the joint of the pin under XL '-' (c8, row 4).",
            fontsize=9, va="top")
    ax.set_xlim(-38, 62); ax.set_ylim(-80, 15); ax.set_aspect("equal"); ax.axis("off")
    fig.savefig(os.path.join(out, "wiring_2_under.png"), bbox_inches="tight")


def side(out):
    fig, ax = plt.subplots(figsize=(13, 5.8), dpi=110)
    ax.add_patch(Rectangle((0, 0), 30, 1.6, fc="#e9dcc0", ec="#8a7a55"))
    ax.text(31, 0.8, "adapter board (perfboard)", va="center", fontsize=9)
    for x0, lab in ((2, "socket strip"), (6, "XL strip")):
        ax.add_patch(Rectangle((x0 - 1.25, 1.6), 2.5, 2.5, fc="#333", ec="black"))
        ax.add_patch(Rectangle((x0 - 0.32, 1.6 - 6.0), 0.64, 6.0 + 2.5 + 3.0, fc="#b0b0b0", ec="black"))
        ax.text(x0, -5.2, lab, ha="center", fontsize=8.5)
    ax.add_patch(Rectangle((0.9, -4.4), 2.2, -0.0001, fc="none"))
    ax.plot([2 - 0.4, 2 + 0.4], [4.1 + 0.05] * 2, color="red", lw=2)
    ax.annotate("socket strips: cut these short tops off flush\nwith the plastic BEFORE the XL goes on",
                xy=(2, 4.4), xytext=(-12, 9.5), fontsize=8, arrowprops=dict(arrowstyle="->", color="red"), color="red")
    ax.add_patch(Rectangle((4.6, 4.1), 22, 1.6, fc="#262626"))
    ax.text(15.6, 4.9, "XL (print facing up)", color="white", fontsize=8.5, ha="center", va="center")
    ax.annotate("XL strips: short ends come up through the XL's holes;\nsolder them on the XL's top", xy=(6, 6.4),
                xytext=(10, 9.5), fontsize=8, arrowprops=dict(arrowstyle="->"))
    ax.annotate("plastic ON TOP of the board,\nall four strips the same way", xy=(4.4, 2.8), xytext=(10.5, 1.8),
                fontsize=8, arrowprops=dict(arrowstyle="->"))
    ax.annotate("long ends DOWN (4.4 mm below the board):\nthe socket strips' ones go into the orbs' socket;\n"
                "solder every pin on the underside, right at the board", xy=(2, -2.5), xytext=(10.5, -3.9),
                fontsize=8, arrowprops=dict(arrowstyle="->"))
    ax.set_xlim(-13, 46); ax.set_ylim(-7, 13); ax.set_aspect("equal"); ax.axis("off")
    ax.set_title("3. Which way the strips go in (side view, not to scale across)", fontsize=12, fontweight="bold")
    fig.savefig(os.path.join(out, "wiring_3_side.png"), bbox_inches="tight")


def usb(out):
    fig, ax = plt.subplots(figsize=(10, 6.5), dpi=110)
    ax.add_patch(Rectangle((-14, -9), 40, 18, fc="#1e1e1e"))
    ax.text(-13, 7.5, "orbs board, BACK side (the side the ESP32 / XL is on)", color="white", fontsize=8.5)
    ax.add_patch(Rectangle((-1.5, -1.5), 13.5, 10.5, fc="#2e8b57", ec="black"))
    ax.text(5.25, 6.6, "V1297", color="white", fontsize=10, ha="center", fontweight="bold")
    ax.text(5.25, 8.3, "case USB board (its USB-C faces out through the case wall, at the top here)",
            color="white", fontsize=7.5, ha="center")
    names = ["VCC end pin", "D-  (WHITE)", "D+  (GREEN)", "GND end pin"]
    cols = ["#bbbbbb", "white", "#32cd32", "#bbbbbb"]
    for i in range(4):
        x = i * 2.54 + 1.4
        ax.add_patch(Circle((x, 1.5), 0.75, fc="#cccccc", ec="black", zorder=3))
        ax.text(x, -0.5, names[i], color=cols[i], fontsize=8, ha="center", va="top", rotation=90, fontweight="bold"
                if i in (1, 2) else "normal")
    ax.text(-3.2, -6.5, "VCC", color="white", fontsize=10, fontweight="bold")
    ax.text(10.6, -6.5, "GND", color="white", fontsize=10, fontweight="bold")
    ax.text(-13, -8.3, "Seen from the back side, VCC label on the LEFT (as on our unit): meter check 19 found\n"
            "D+ on the middle pin next to GND. Solder onto the pin tips sticking up through the green board.",
            color="white", fontsize=8)
    ax.set_xlim(-14, 26); ax.set_ylim(-9, 9.5); ax.set_aspect("equal"); ax.axis("off")
    ax.set_title("4. The cut cable's two wires on the case USB board", fontsize=12, fontweight="bold")
    fig.savefig(os.path.join(out, "wiring_4_usb.png"), bbox_inches="tight")


if __name__ == "__main__":
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    top(out); under(out); side(out); usb(out); print(out)
