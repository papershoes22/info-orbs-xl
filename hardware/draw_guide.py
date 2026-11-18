#!/usr/bin/env python3
"""draw_guide.py: the drawings for the printed XL adapter build guide (2026-11-15).

  python3 -I draw_guide.py      -> renders/guide/*.png (200 dpi)

Words used on every drawing (the same as the guide): the DOT corner (marked on both sides of the board), the DOT END
(short edge at the dot = the orbs' case end), the DOT SIDE (long edge at the dot). Rows are numbered 1-27 from the dot
end, so "pin 7" of any strip is in row 7. The four strips: dot-side socket strip (19 pins, the ESP32 names 3V3 ... 5V),
far socket strip (GND ... CLK), dot-side XL strip (next to it, under the XL's "VB" row), far XL strip (under the XL's
"0" row). Underside drawings are the board turned over sideways like a page: the dot is then at the TOP RIGHT.
Facts: the orbs board's v0.2 design file (b6ead4e), photos and meter checks of a real unit; same links as draw_wiring.py.
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, FancyBboxPatch, Polygon

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "renders", "guide")
P = 2.54
DPI = 200
J2 = ["3V3", "EN", "SVP", "SVN", "P34", "P35", "P32", "P33", "P25", "P26", "P27", "P14", "P12", "GND", "P13", "SD2",
      "SD3", "CMD", "5V"]                        # dot-side socket strip, pin 1..19
J3 = ["GND", "P23", "P22", "TX", "RX", "P21", "GND", "P19", "P18", "P5", "P17", "P16", "P4", "P0", "P2", "P15",
      "SD1", "SD0", "CLK"]                       # far socket strip
XL_L = ["VB", "VS", "-", "3E", "+", "VA", "$28", "-", "$27", "$26", "RU", "22", "-", "21", "20", "19", "18", "-",
        "17", "16", "BT", "$47", "-", "$46", "$45", "$44", "$43", "-", "39", "38"]
XL_R = ["0", "1", "-", "2", "3", "4", "5", "-", "6", "7", "8", "9", "-", "10", "11", "12", "13", "-", "14", "15",
        "+", "31", "-", "32", "33", "34", "35", "-", "36", "37"]
# internal columns: 0 dot socket, 1 dot XL, 2..7 middle, 8 far XL, 9 far socket; rows 1..27
def xl_name(col, row):
    return (XL_L if col == 1 else XL_R)[27 - row]
PULLED = {(1, 7), (8, 1), (8, 7), (8, 11)}
SOCKET_USE = {(0, 7): "screen D3 select", (0, 8): "screen D2 select", (0, 9): "screen D4 select",
              (0, 10): "LFT button", (0, 11): "OK button", (0, 12): "RHT button", (0, 15): "screen D1 select",
              (0, 19): "USB 5 V", (9, 1): "USB ground", (9, 2): "screens' clock", (9, 6): "screen D5 select",
              (9, 7): "screens' ground", (9, 8): "screens' DC", (9, 9): "screens' reset", (9, 11): "screens' data"}
# step id, link name, kind, colour, route [(col,row)...] from the socket pin to the XL pin
STEPS = [
    ("D1", "R1", "bare", "#1565c0", [(9, 2), (8, 2)]),
    ("D2", "R2", "bare", "#1565c0", [(9, 6), (8, 6)]),
    ("D3", "R3", "bare", "#1565c0", [(9, 8), (8, 8)]),
    ("D4", "R4", "bare", "#1565c0", [(9, 9), (8, 9)]),
    ("D5", "L1", "bare", "#1565c0", [(0, 8), (1, 8)]),
    ("D6", "L2", "bare", "#1565c0", [(0, 9), (1, 9)]),
    ("D7", "L5", "short", "#00838f", [(0, 10), (1, 11)]),
    ("D8", "L6", "short", "#00838f", [(0, 11), (1, 12)]),
    ("D9", "L7", "short", "#00838f", [(0, 12), (1, 13)]),
    ("D10", "L4", "short", "#00838f", [(0, 15), (1, 16)]),
    ("D11", "L3", "wire", "#b8860b", [(0, 7), (2, 7), (2, 4), (1, 4)]),
    ("D12", "L8", "wire", "#c62828", [(0, 19), (0, 27), (1, 27)]),
    ("D13", "R5", "wire", "#b8860b", [(9, 11), (7, 11), (7, 18), (8, 18)]),
    ("D14", "R6", "wire", "#111111", [(9, 1), (7, 1), (7, 5), (8, 5)]),
    ("D15", "R7", "wire", "#111111", [(9, 7), (7, 7), (7, 5), (8, 5)]),
]


def socket_name(col, row):
    return (J2 if col == 0 else J3)[row - 1]


def pin_word(col, row):
    if col in (0, 9):
        side = "dot-side" if col == 0 else "far"
        return f"{side} socket strip pin {row}: {socket_name(col, row)}"
    side = "dot-side" if col == 1 else "far"
    return f"{side} XL strip pin {row} (XL {xl_name(col, row)})"


def X(col, under):
    return (9 - col) * P if under else col * P


def Y(row):
    return -(row - 1) * P


def draw_board(ax, under, rows=27, done=(), current=None, show_names=True, title=None, fs=1.0, short=False):
    ax.add_patch(Rectangle((-0.5 * P, Y(rows) - 0.5 * P), 10 * P, rows * P, fc="#efe3c4", ec="#8a7a55", lw=1.5,
                           zorder=0))
    if rows < 27:   # torn edge: the board carries on
        xs = [-0.5 * P + i * 1.27 for i in range(21)]
        ys = [Y(rows) - 0.5 * P + (0.6 if i % 2 else -0.2) for i in range(21)]
        ax.plot(xs, ys, color="#8a7a55", lw=1.0, zorder=1)
        ax.text(4.5 * P, Y(rows) - 4.2, f"(the board carries on to row 27)", ha="center", fontsize=7 * fs, color="#6a5a35")
    for c in range(10):
        for r in range(1, rows + 1):
            ax.add_patch(Circle((X(c, under), Y(r)), 0.45, fc="white", ec="#b49a5e", lw=0.6, zorder=1))
    dot_x = X(0, under) + (1.9 if under else -1.9)
    ax.add_patch(Circle((dot_x, 1.9), 0.8, fc="black", zorder=3))
    for r in range(1, rows + 1):
        for xx in (X(0, under) + (5.2 if under else -5.2) * 0 + (7.6 if under else -7.6) * 0,):
            pass
        lx = 10 * P + 1.0 if under else -0.5 * P - 1.6
        rx = -0.5 * P - 1.6 if under else 10 * P + 1.0
    # pins
    for c, rng in ((0, range(1, 20)), (9, range(1, 20)), (1, range(1, 28)), (8, range(1, 28))):
        for r in rng:
            if r > rows:
                continue
            if (c, r) in PULLED:
                ax.text(X(c, under), Y(r), "x", ha="center", va="center", fontsize=8 * fs, color="#c62828",
                        fontweight="bold", zorder=5)
                continue
            if under:
                ax.add_patch(Circle((X(c, under), Y(r)), 0.95 if c in (0, 9) else 0.82,
                                    fc="#cfcfcf" if c in (0, 9) else "#e6c27a", ec="black", lw=0.8, zorder=4))
            else:
                ax.add_patch(Rectangle((X(c, under) - 0.4, Y(r) - 0.4), 0.8, 0.8, fc="black", zorder=4))
    # done links (grey) and the current one
    for st in STEPS:
        if st[0] in done and max(r for c, r in st[4]) <= rows:
            xs = [X(c, under) for c, r in st[4]]; ys = [Y(r) for c, r in st[4]]
            ax.plot(xs, ys, color="#9e9e9e", lw=2.4, solid_capstyle="round", zorder=6)
    if current:
        sid, name, kind, colour, pts = current
        xs = [X(c, under) for c, r in pts]; ys = [Y(r) for c, r in pts]
        ax.plot(xs, ys, color=colour, lw=4.2 if kind != "wire" else 3.6, solid_capstyle="round", zorder=8)
        if colour == "#111111":
            ax.plot(xs, ys, color="white", lw=0.8, ls=(0, (2, 3)), zorder=9)
        for (c, r), lab in ((pts[0], socket_name(*pts[0]) + (f"\n({SOCKET_USE[pts[0]]})" if pts[0] in SOCKET_USE else "")),
                            (pts[-1], "XL " + xl_name(*pts[-1]))):
            ax.add_patch(Circle((X(c, under), Y(r)), 1.45, fc="none", ec=colour, lw=2.2, zorder=9))
        cx, cy = pts[-1]
        tx = X(cx, under) + (3.2 if X(cx, under) < 4.5 * P else -3.2)
        ax.text(tx, Y(cy), f"XL {xl_name(cx, cy)}", ha="left" if X(cx, under) < 4.5 * P else "right", va="center",
                fontsize=8 * fs, fontweight="bold", color=colour, zorder=11,
                bbox=dict(fc="white", ec=colour, lw=1.0, pad=0.6))
    # column headings
    heads = ({0: ("dot-side socket strip", 8.6), 1: ("dot-side XL strip", 4.4), 8: ("far XL strip", 4.4),
              9: ("far socket strip", 8.6)} if not short else
             {0: ("dot-side\nsocket", 9.4), 1: ("dot-side\nXL", 4.0), 8: ("far\nXL", 4.0), 9: ("far\nsocket", 9.4)})
    for c, (h, hy) in heads.items():
        x = X(c, under)
        ax.plot([x, x], [1.6, hy - 0.4], color="#444", lw=0.6, zorder=2)
        left = x < 4.5 * P
        ax.text(x + (0.4 if not left else -0.4), hy, h, ha="right" if left else "left", va="bottom",
                fontsize=6.6 * fs, fontweight="bold")
    # row numbers on both sides
    for r in range(1, rows + 1):
        ax.text(-0.5 * P - 1.0, Y(r), str(r), ha="right", va="center", fontsize=6.6 * fs, color="#6a5a35")
        ax.text(9.5 * P + 1.0, Y(r), str(r), ha="left", va="center", fontsize=6.6 * fs, color="#6a5a35")
    if show_names:
        for r in range(1, min(rows, 19) + 1):
            for c in (0, 9):
                name = socket_name(c, r)
                used = (c, r) in SOCKET_USE
                left = (X(c, under) < 4.5 * P)
                tx = -0.5 * P - 4.6 if left else 9.5 * P + 4.6
                tx = tx if not short else (-0.5 * P - 4.9 if left else 9.5 * P + 4.9)
                ax.text(tx, Y(r), name + (f"  {SOCKET_USE[(c, r)]}" if used and not short else ""), ha="right" if left else "left",
                        va="center", fontsize=6.6 * fs, fontweight="bold" if used else "normal",
                        color="#111" if used else "#555")
    if title:
        ax.set_title(title, fontsize=11 * fs, fontweight="bold", loc="left")
    ax.set_aspect("equal"); ax.axis("off")


def save(fig, name):
    os.makedirs(OUT, exist_ok=True)
    fig.savefig(os.path.join(OUT, name), dpi=DPI, bbox_inches="tight", pad_inches=0.06)
    plt.close(fig)


def fig_board():
    fig, ax = plt.subplots(figsize=(7.2, 6.2))
    W, H = 10, 27
    ax.add_patch(Rectangle((0, 0), W * P, H * P, fc="#efe3c4", ec="#8a7a55", lw=1.5))
    for c in range(W):
        for r in range(H):
            ax.add_patch(Circle(((c + 0.5) * P, (H - r - 0.5) * P), 0.42, fc="white", ec="#b49a5e", lw=0.5))
    ax.add_patch(Circle((-1.6, H * P + 1.6), 0.9, fc="black"))
    ax.text(-3.0, H * P - 2.5, "DOT corner\n(dot on BOTH sides)", ha="right", va="center", fontsize=9, fontweight="bold")
    ax.annotate("", xy=(W * P, H * P + 4.6), xytext=(0, H * P + 4.6), arrowprops=dict(arrowstyle="<->"))
    ax.text(W * P / 2, H * P + 5.6, "10 holes across  =  the DOT END (short edge)", ha="center", fontsize=9)
    ax.annotate("", xy=(-5.5, 0), xytext=(-5.5, H * P), arrowprops=dict(arrowstyle="<->"))
    ax.text(-6.6, H * P / 2, "27 holes along\n=  the DOT SIDE\n(long edge)", ha="right", va="center", fontsize=9)
    ax.text(W * P / 2, -2.6, "FAR END", ha="center", fontsize=9, fontweight="bold")
    ax.text(W * P + 1.5, H * P / 2, "FAR SIDE", ha="left", va="center", fontsize=9, fontweight="bold", rotation=90)
    ax.text(W * P / 2, H * P / 2, "the adapter board\n(top side up)", ha="center", va="center", fontsize=10,
            bbox=dict(fc="white", ec="#8a7a55", alpha=0.9))
    ax.set_xlim(-24, 34); ax.set_ylim(-5, 80); ax.set_aspect("equal"); ax.axis("off")
    save(fig, "g_board.png")


def fig_top():
    fig, ax = plt.subplots(figsize=(7.6, 9.6))
    draw_board(ax, under=False, show_names=True, fs=1.0)
    xl_top, xl_bot = 3 * P + 1.57, -(26 * P + 1.57)
    ax.add_patch(Rectangle((0.91, xl_bot), 21.04, xl_top - xl_bot, fc="none", ec="black", lw=1.6, ls="--", zorder=7))
    ax.add_patch(Rectangle((0.91, 1.27), 21.04, xl_top - 1.27, fc="#2b2b2b22", ec="none", zorder=7))
    ax.add_patch(Rectangle((11.43 - 4.5, xl_bot - 1.5), 9.0, 5.0, fc="#c8c8c8", ec="black", zorder=8))
    ax.annotate("XL's USB-C", xy=(15.9, xl_bot + 1.0), xytext=(30, xl_bot - 3.0), fontsize=7.5,
                arrowprops=dict(arrowstyle="->"), zorder=9)
    ax.annotate("the XL (dashed outline) sits on its two strips,\nprint facing up. It hangs ~9 mm past the dot end:\n"
                "that part is its antenna", xy=(21.9, xl_top - 2), xytext=(31, 15), fontsize=7.5,
                arrowprops=dict(arrowstyle="->"), zorder=9)
    for c, lab in ((1, "VB"), (8, "0")):
        ax.text(X(c, False) + (-1.2 if c == 1 else 1.2), Y(27) - 2.2, f'XL "{lab}"', ha="right" if c == 1 else "left",
                fontsize=7, color="#1d4f91", fontweight="bold")
    ax.text(-33, -76, "black squares = pins    red x = pin pulled out (that hole stays empty)", fontsize=7.5)
    ax.set_xlim(-34, 62); ax.set_ylim(-78, 20)
    save(fig, "g_top.png")


def fig_side():
    fig, ax = plt.subplots(figsize=(7.6, 3.6))
    ax.add_patch(Rectangle((0, 0), 30, 1.6, fc="#efe3c4", ec="#8a7a55"))
    ax.text(31, 0.8, "adapter board", va="center", fontsize=8.5)
    for x0, lab in ((2, "socket\nstrip"), (6, "XL\nstrip")):
        ax.add_patch(Rectangle((x0 - 1.25, 1.6), 2.5, 2.5, fc="#333", ec="black"))
        ax.add_patch(Rectangle((x0 - 0.32, 1.6 - 6.0), 0.64, 6.0 + 2.5 + (3.0 if x0 == 6 else 0.0), fc="#b0b0b0",
                               ec="black"))
        ax.text(x0, -6.8, lab, ha="center", fontsize=8, va="top")
    ax.add_patch(Rectangle((4.6, 4.1), 22, 1.6, fc="#262626"))
    ax.text(15.6, 4.9, "XL (print up)", color="white", fontsize=8, ha="center", va="center")
    ax.annotate("socket strip: short top cut off\nflush with the plastic (step B4)", xy=(2, 4.1), xytext=(-14, 8.0),
                fontsize=7.5, arrowprops=dict(arrowstyle="->"))
    ax.annotate("XL strip: short end through\nthe XL, soldered on top (C5)", xy=(6, 7.0), xytext=(9, 9.2),
                fontsize=7.5, arrowprops=dict(arrowstyle="->"))
    ax.annotate("plastic ON TOP, all four strips", xy=(4.4, 2.9), xytext=(11, 2.0), fontsize=7.5,
                arrowprops=dict(arrowstyle="->"))
    ax.annotate("long ends DOWN (4.4 mm): soldered here, right at the\nboard; the socket strips' long ends go into the orbs",
                xy=(2.4, -1.0), xytext=(9, -4.6), fontsize=7.5, arrowprops=dict(arrowstyle="->"))
    ax.set_xlim(-15, 47); ax.set_ylim(-9, 12); ax.set_aspect("equal"); ax.axis("off")
    save(fig, "g_side.png")


def fig_under_all():
    fig, ax = plt.subplots(figsize=(7.6, 9.6))
    draw_board(ax, under=True, show_names=True)
    for st in STEPS:
        sid, name, kind, colour, pts = st
        xs = [X(c, True) for c, r in pts]; ys = [Y(r) for c, r in pts]
        ax.plot(xs, ys, color=colour, lw=3.4 if kind != "wire" else 2.8, solid_capstyle="round", zorder=8)
        if colour == "#111111":
            ax.plot(xs, ys, color="white", lw=0.7, ls=(0, (2, 3)), zorder=9)
        mx, my = (xs[0] + xs[1]) / 2, (ys[0] + ys[1]) / 2
        if kind == "wire":
            mx, my = xs[1], (ys[1] + ys[2]) / 2
        ax.text(mx, my, sid, fontsize=6.5, fontweight="bold", ha="center", va="center", zorder=10,
                bbox=dict(fc="white", ec=colour, lw=0.8, pad=0.5))
    ax.text(-33, Y(27) - 6.0, "turned over sideways: the dot is TOP RIGHT.  grey = socket pins, gold = XL pins, "
            "x = empty hole", fontsize=7.5)
    ax.set_xlim(-34, 60)
    save(fig, "g_under_all.png")


def fig_steps():
    done = []
    for st in STEPS:
        rows = max(max(r for c, r in st[4]) + 1, 10)
        rows = 27 if rows > 21 else rows
        fig, ax = plt.subplots(figsize=(4.3, 0.95 + rows * 0.19))
        draw_board(ax, under=True, rows=rows, done=done, current=st, show_names=True, fs=1.22, short=True)
        ax.set_xlim(-15, 41); ax.set_ylim(Y(rows) - 6.0, 16.5)
        save(fig, f"g_step_{st[0]}.png")
        done.append(st[0])


def fig_usb():
    fig, ax = plt.subplots(figsize=(6.6, 4.2))
    ax.add_patch(Rectangle((-14, -9), 40, 18.5, fc="#1e1e1e"))
    ax.text(-13, 8.2, "orbs board, BACK side (the side the ESP32 is on)", color="white", fontsize=8)
    ax.add_patch(Rectangle((-1.5, -1.5), 13.5, 10.0, fc="#2e8b57", ec="black"))
    ax.text(5.25, 6.4, "V1297", color="white", fontsize=10, ha="center", fontweight="bold")
    ax.text(5.25, 4.4, "case USB board\n(its USB-C points out of the case)", color="white", fontsize=6.6, ha="center")
    labs = [("VCC end", "#bbbbbb"), ("WHITE (D-)", "white"), ("GREEN (D+)", "#4cd964"), ("GND end", "#bbbbbb")]
    for i, (t, col) in enumerate(labs):
        x = i * 2.54 + 1.4
        ax.add_patch(Circle((x, 1.4), 0.8, fc="#d0d0d0", ec="black", zorder=3))
        ax.text(x, -0.4, t, color=col, fontsize=7.5, ha="center", va="top", rotation=90,
                fontweight="bold" if i in (1, 2) else "normal")
    ax.text(-3.4, -7.0, "VCC", color="white", fontsize=10, fontweight="bold")
    ax.text(10.6, -7.0, "GND", color="white", fontsize=10, fontweight="bold")
    ax.text(-13, -8.4, "the printed VCC label on the LEFT, GND on the right (as on our unit)", color="#cccccc",
            fontsize=7)
    ax.set_xlim(-14, 26); ax.set_ylim(-9, 9.5); ax.set_aspect("equal"); ax.axis("off")
    save(fig, "g_usb.png")


def fig_fit():
    fig, axs = plt.subplots(2, 1, figsize=(7.4, 6.6))
    for k, ax in enumerate(axs):
        # orbs board back, case end on the LEFT, dot side (socket row 3V3...5V) at the BOTTOM
        ax.add_patch(Rectangle((0, 0), 120, 28.5, fc="#1e1e1e"))
        sx = 16.0                                     # socket pin 1 (case end)
        for yy in (2.0, 25.0):                        # bottom row = 3V3...5V (dot side), top row = GND...CLK
            ax.add_patch(Rectangle((sx - 1.27, yy - 1.27), 19 * P, 2.54, fc="#000", ec="#8a8a8a", lw=1.0))
        ax.text(sx - 4.0, 13.4, "V1", color="white", fontsize=10, fontweight="bold", ha="right", va="center")
        ax.add_patch(Rectangle((sx + 19 * P + 2.0, 5.2), 17, 18, fc="none", ec="white", lw=1.0))
        ax.text(sx + 19 * P + 10.5, 14.2, "^ ^ ^\n!USB This Way!", color="white", fontsize=6.0, ha="center",
                va="center", rotation=90)
        ax.add_patch(Rectangle((98, -3.0), 13, 8.5, fc="#2e8b57", ec="black"))
        ax.text(104.5, 1.2, "case USB\nboard", color="white", fontsize=6.3, ha="center", va="center")
        if k == 0:
            ax.set_title("A. ESP32 out: the empty socket", fontsize=9.5, loc="left", fontweight="bold")
            ax.text(sx + 9 * P, 13.4, "socket: 2 rows of 19 holes", color="#dddddd", fontsize=7.5, ha="center",
                    va="center")
        else:
            ax.set_title("B. adapter in: dot end at the V1 end, the XL's USB-C toward '!USB This Way!'",
                         fontsize=9.5, loc="left", fontweight="bold")
            ax.add_patch(Rectangle((sx - 1.27, 0.7), 27 * P, 27.1, fc="#efe3c4aa", ec="#8a7a55", lw=1.2))
            x0 = sx - (2 * P + 1.57)
            ax.add_patch(Rectangle((x0, 3.4), 76.4, 21.04, fc="#262626e0", ec="#d4a017", lw=1.4))
            ax.text(x0 + 38, 13.9, "XL (print up)", color="white", fontsize=8.5, ha="center", va="center")
            ax.add_patch(Rectangle((x0 + 76.4 - 1.0, 9.9), 2.6, 8.0, fc="#c8c8c8", ec="black"))
            ax.add_patch(Circle((sx - 1.27 - 1.6, -1.8), 1.1, fc="white", ec="black"))
            ax.text(sx - 4.5, -5.6, "dot (dot end, dot side)", fontsize=7, ha="left")
            ax.plot([x0 + 78.0, 92, 101.0], [14.3, 12.0, 5.5], color="#4cd964", lw=1.6)
            ax.plot([x0 + 78.0, 92.6, 102.6], [13.5, 11.2, 5.5], color="white", lw=1.6)
            ax.text(95, 21.0, "cut cable", color="white", fontsize=7, ha="center")
        ax.text(1.0, -5.6 if k == 0 else 31.0, "case end" if k == 0 else "", fontsize=7)
        ax.text(119.0, -5.6, "toward the middle of the orbs", fontsize=7, ha="right")
        ax.set_xlim(-2, 122); ax.set_ylim(-8, 33); ax.set_aspect("equal"); ax.axis("off")
    fig.tight_layout()
    save(fig, "g_fit.png")


if __name__ == "__main__":
    fig_board(); fig_top(); fig_side(); fig_under_all(); fig_steps(); fig_usb(); fig_fit()
    print(OUT, sorted(os.listdir(OUT)))
