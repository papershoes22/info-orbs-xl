#!/usr/bin/env python3
"""render_posts.py: the base's two middle posts before and after the cut for the USB-C plug (2026-11-15).

  python3 -I render_posts.py base_xl_3mm.stl PLUG_WIDTH OUT.png [PLUG_LENGTH PLUG_THICKNESS]
  (length = from the XL's port to where the wires leave the moulding; defaults 20.93 and 6.04, readings 20-22)

Left: looking straight down into the base at the middle (as the base sits on the desk, open side up), the original base's
posts in grey, the new posts in green, the plug's outline in blue (centred on the XL, y -0.29). Right: a cut across
the width at the posts' middle (x = 0), both bases, with the plug's cross-section where it sits in the XL's port.
"""
import os, sys
import numpy as np
import trimesh
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "source", "bottom_20mmscrews.stl")
XL_Y = -0.287
# heights: board back surface sits 2.99 below the lip (z -8.5) -> z = -5.51 - H (H = mm above the board)
PLUG_CENTRE_H = 16.5      # middle of the XL's USB-C (adapter stack in draw_adapter.py)


def section(mesh, axis, value):
    tri = mesh.triangles; d = tri[:, :, axis] - value; segs = []
    other = 1 if axis == 0 else 0
    for t, dd in zip(tri, d):
        pts = []
        for i in range(3):
            j = (i + 1) % 3
            if (dd[i] < 0) != (dd[j] < 0):
                f = dd[i] / (dd[i] - dd[j]); p = t[i] + f * (t[j] - t[i]); pts.append((p[other], p[2]))
        if len(pts) == 2:
            segs.append(pts)
    return segs


def top_slice(mesh, z):
    tri = mesh.triangles; d = tri[:, :, 2] - z; segs = []
    for t, dd in zip(tri, d):
        pts = []
        for i in range(3):
            j = (i + 1) % 3
            if (dd[i] < 0) != (dd[j] < 0):
                f = dd[i] / (dd[i] - dd[j]); p = t[i] + f * (t[j] - t[i]); pts.append((p[0], p[1]))
        if len(pts) == 2:
            segs.append(pts)
    return segs


def draw(ax, segs, colour, lw):
    for a, b in segs:
        ax.plot([a[0], b[0]], [a[1], b[1]], color=colour, lw=lw)


def main():
    new = trimesh.load(sys.argv[1]); plug = float(sys.argv[2]); out = sys.argv[3]
    length = float(sys.argv[4]) if len(sys.argv) > 4 else 20.93
    thick = float(sys.argv[5]) if len(sys.argv) > 5 else 6.04
    old = trimesh.load(SRC)
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(16, 7.5), dpi=110)
    for z, lw_old, lw_new in ((-9.0, 2.4, 1.3),):
        draw(a1, top_slice(old, z), "0.6", lw_old)
        draw(a1, top_slice(new, z), "tab:green", lw_new)
    a1.add_patch(Rectangle((-11.19, XL_Y - plug / 2), length, plug, fc="#5b8fd633", ec="#2b5fa6", lw=1.5))
    a1.text(-10.5, XL_Y + plug / 2 + 0.6, f"USB-C plug body ({plug:.2f} wide, {length:.2f} past the port, {thick:.2f} thick) toward the middle",
            fontsize=8.5, color="#2b5fa6")
    a1.annotate("XL's port\n(x -11.2)", xy=(-11.0, XL_Y), xytext=(-24, -6), fontsize=8,
                arrowprops=dict(arrowstyle="->"))
    a1.text(-4.6, 12.0, "post", fontsize=9); a1.text(-4.6, -13.0, "post", fontsize=9)
    a1.text(16, 13.5, "lip notch for\nthe case USB board", fontsize=8)
    a1.set_xlim(-26, 30); a1.set_ylim(-18.5, 18.5); a1.set_aspect("equal")
    a1.set_title("Looking down into the middle of the base, 0.5 mm below the lip\n"
                 "grey = the original base (gap 9.24), green = new (gap cut back)", fontsize=11)
    a1.set_xlabel("mm along the orbs (case end to the left)"); a1.set_ylabel("mm across")
    draw(a2, section(old, 0, 0.0), "0.6", 2.4)
    draw(a2, section(new, 0, 0.0), "tab:green", 1.3)
    zc = -5.51 - PLUG_CENTRE_H
    a2.add_patch(Rectangle((XL_Y - plug / 2, zc - thick / 2), plug, thick, fc="#5b8fd633", ec="#2b5fa6", lw=1.5))
    a2.text(XL_Y, zc, "plug\n(here when\nclosed)", ha="center", va="center", fontsize=8, color="#2b5fa6")
    a2.annotate("the posts slide past the plug's sides\nwhile the base goes on: they need the gap,\n"
                "not depth", xy=(XL_Y + plug / 2, zc + 3.2), xytext=(3, -12.0), fontsize=8,
                arrowprops=dict(arrowstyle="->"))
    a2.set_xlim(-19, 19); a2.set_ylim(-33, -5); a2.set_aspect("equal")
    a2.set_title("Cut across the width through the posts (x = 0)\nas the base sits on the desk: lip at the top, floor at the bottom",
                 fontsize=11)
    a2.set_xlabel("mm across"); a2.set_ylabel("mm (height in the base file)")
    fig.tight_layout(); fig.savefig(out); print(out)


if __name__ == "__main__":
    main()
