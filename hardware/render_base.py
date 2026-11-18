#!/usr/bin/env python3
"""render_base.py: pictures of the old and the taller orbs base, side by side (2026-11-15).

  python3 -I render_base.py base_xl_3mm.stl OUT.png

Top row: a cut along the length through one screw column (old grey, new green), and a cut across the width.
Bottom row: both bases seen from above at an angle. Heights are the model's own numbers.
"""
import os, sys
import numpy as np
import trimesh
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "source", "bottom_20mmscrews.stl")


def section(mesh, axis, value):
    """Segments where the plane (axis = value) cuts the mesh; returns list of ((a,z),(b,z)) in the other horizontal axis."""
    tri = mesh.triangles
    d = tri[:, :, axis] - value
    segs = []
    other = 1 - axis if axis in (0, 1) else 0
    for t, dd in zip(tri, d):
        pts = []
        for i in range(3):
            j = (i + 1) % 3
            if (dd[i] < 0) != (dd[j] < 0):
                f = dd[i] / (dd[i] - dd[j])
                p = t[i] + f * (t[j] - t[i])
                pts.append((p[other], p[2]))
        if len(pts) == 2:
            segs.append(pts)
    return segs


def draw_section(ax, segs, colour, lw):
    for (a, b) in segs:
        ax.plot([a[0], b[0]], [a[1], b[1]], color=colour, lw=lw)


def draw_3d(ax, mesh, colour, title):
    v = mesh.vertices
    polys = v[mesh.faces]
    light = np.array([0.3, -0.5, 0.8]); light /= np.linalg.norm(light)
    shade = 0.45 + 0.55 * np.clip(mesh.face_normals @ light, 0, 1)
    base = np.array(matplotlib.colors.to_rgb(colour))
    cols = np.clip(shade[:, None] * base[None, :], 0, 1)
    pc = Poly3DCollection(polys, facecolors=cols, edgecolors="none")
    ax.add_collection3d(pc)
    ax.set_xlim(-105, 105); ax.set_ylim(-105, 105); ax.set_zlim(-60, 20)
    ax.set_box_aspect((210, 210, 80))
    ax.view_init(elev=38, azim=-62)
    ax.set_axis_off()
    ax.set_title(title, fontsize=11)


def main():
    new = trimesh.load(sys.argv[1]); out = sys.argv[2]
    old = trimesh.load(SRC)
    fig = plt.figure(figsize=(16, 10), dpi=110)
    ax1 = fig.add_subplot(2, 2, 1)
    draw_section(ax1, section(old, 1, 11.22), "0.55", 2.2)
    draw_section(ax1, section(new, 1, 11.22), "tab:green", 1.2)
    ax1.set_xlim(-104, -60); ax1.set_ylim(-34, -5); ax1.set_aspect("equal")
    ax1.set_title("Cut along the length through a screw column (one end)\ngrey = the original base, green = new", fontsize=11)
    ax1.set_xlabel("mm along the length"); ax1.set_ylabel("mm (height)")
    for z, txt in ((-8.5, "lip  (same)"), (-7.25, "posts  (same)"), (-18.5, "screw web top  (same)"),
                   (-25.5, "old inside floor"), (-28.5, "new inside floor  (+3.0)"), (-29.0, "old bottom"),
                   (-32.0, "new bottom  (+3.0)")):
        ax1.axhline(z, color="0.85", lw=0.6, zorder=0)
        ax1.text(-68, z + 0.2, txt, fontsize=8)
    ax1.grid(False)
    ax2 = fig.add_subplot(2, 2, 2)
    draw_section(ax2, section(old, 0, -40.0), "0.55", 2.2)
    draw_section(ax2, section(new, 0, -40.0), "tab:green", 1.2)
    ax2.set_xlim(-20, 20); ax2.set_ylim(-34, -5); ax2.set_aspect("equal")
    ax2.set_title("Cut across the width (where the XL sits)\ninside 30.44 wide; depth 17.00 -> 20.00", fontsize=11)
    ax2.set_xlabel("mm across"); ax2.set_ylabel("mm (height)")
    draw_3d(fig.add_subplot(2, 2, 3, projection="3d"), old, "#9a9a9a", f"the original base: {old.extents[2]:.2f} tall overall")
    draw_3d(fig.add_subplot(2, 2, 4, projection="3d"), new, "#7fb069", f"new base: {new.extents[2]:.2f} tall overall (+3.00)")
    fig.tight_layout()
    fig.savefig(out)
    print(out)


if __name__ == "__main__":
    main()
