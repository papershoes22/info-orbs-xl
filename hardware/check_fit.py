#!/usr/bin/env python3
"""check_fit.py: can the new base close over the XL adapter and the USB-C plug? (2026-11-15)

  python3 -I check_fit.py base_xl_3mm.stl

The base goes on by moving straight toward the board, so every part inside must have NO base material anywhere in
its outline (seen from above) from the height of that part's top up to the base's posts. Rays straight down on a
0.25 mm grid over each part's outline find the highest base surface there; it must be below the part's top.
Coordinates: base file (x along the orbs, socket at negative x; y across; z: lip -8.5, floor -28.5).
Height above the orbs board H -> base z = -5.51 - H (board back surface 2.99 below the lip, reading 6).
Parts (from the readings and the orbs board's v0.2 design file):
  socket pin 1 at x -79.895; XL pin k at row 26 - k; XL holes 1.07 + 0.5 from its ends (reading 24)
  XL board x -89.09 .. -12.69 (76.4 long), y -0.287 +- 10.52 (21.04), top parts up to H 21.08
  perfboard x -81.17 .. -12.55, y -13.06 .. 12.48, up to H 13.6 (socket strips' plastic on top)
  plug body (readings 20-22): from the XL's port (x -11.19) 27.58 - 6.65 = 20.93 long, 10.97 wide, 6.04 thick,
  centred on the port (H 16.3-17.0) -> top up to H 20.04
"""
import sys
import numpy as np
import trimesh

m = trimesh.load(sys.argv[1])
T = m.triangles; v0 = T[:, 0]; e1 = T[:, 1] - v0; e2 = T[:, 2] - v0


def top_z(x, y):
    o = np.array([x, y, 50.0]); d = np.array([0, 0, -1.0])
    p = np.cross(d, e2); det = (e1 * p).sum(1); ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1 / np.where(ok, det, 1), 0); s = o - v0
    u = (s * p).sum(1) * inv; q = np.cross(s, e1); w = (q * d).sum(1) * inv; t = (q * e2).sum(1) * inv
    sel = ok & (u >= -1e-9) & (w >= -1e-9) & (u + w <= 1 + 1e-9) & (t > 0)
    return 50 - t[sel].min() if sel.any() else None


H = lambda z: -5.51 - z
port_x = -89.09 + 76.4 + 1.5
parts = [("XL board and its parts", (-89.09, -12.69), (-0.287 - 10.52, -0.287 + 10.52), 21.08),
         ("perfboard + socket strips", (-81.17, -12.55), (-13.06, 12.48), 13.6),
         ("USB-C plug body", (port_x, port_x + 20.93), (-0.287 - 10.97 / 2, -0.287 + 10.97 / 2), 20.04)]
ok = True
for name, (x0, x1), (y0, y1), htop in parts:
    worst = None
    for x in np.arange(x0, x1 + 1e-9, 0.25):
        for y in np.arange(y0, y1 + 1e-9, 0.25):
            z = top_z(x, y)
            if z is not None and (worst is None or z > worst[0]):
                worst = (z, x, y)
    gap = H(worst[0]) - htop          # height of the highest base surface in the outline minus the part's top
    good = gap > 0
    ok &= good
    print(f"{'ok  ' if good else 'FAIL'} {name}: x {x0:.2f}..{x1:.2f}, y {y0:.2f}..{y1:.2f}, top H {htop:.2f}; "
          f"highest base surface in that outline at H {H(worst[0]):.2f} (x {worst[1]:.2f}, y {worst[2]:.2f}) "
          f"-> {gap:.2f} mm clear")
# side clearances while closing: nearest post material beside each part's outline
def nearest(x0, x1, y0, y1, zlo):
    best = 99
    for x in np.arange(x0 - 6, x1 + 6, 0.25):
        for y in np.arange(y0 - 6, y1 + 6, 0.25):
            if x0 <= x <= x1 and y0 <= y <= y1:
                continue
            z = top_z(x, y)
            if z is not None and z > zlo:      # base material that reaches up past the part's top
                dx = max(x0 - x, 0, x - x1); dy = max(y0 - y, 0, y - y1)
                best = min(best, float(np.hypot(dx, dy)))
    return best
for name, (x0, x1), (y0, y1), htop in parts:
    print(f"     {name}: nearest base material that passes beside it while closing: "
          f"{nearest(x0, x1, y0, y1, -5.51 - htop):.2f} mm")
sys.exit(0 if ok else 1)
