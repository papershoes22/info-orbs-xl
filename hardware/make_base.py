#!/usr/bin/env python3
"""make_base.py: the orbs' base (the part the ESP32 points into), made taller for the Pico LiPo 2 XL W (2026-11-15).

  python3 -I make_base.py [--extra MM] [--plug-width MM] [--check]

Starts from the base of Skeetz1230's "Info Orbs enclosure" (printables.com/model/1016115, CC BY 4.0),
bottom_20mmscrews.stl (a copy is in source/). Nothing is redrawn: the model is stretched at one height, z = -23.5,
where everything the plane cuts is a straight up-and-down wall (the four outer walls, the four screw columns' rings
and their screw-head holes). Everything above that plane (the lip, the posts that hold the board, the screw webs,
the screw holes, the middle posts) is the exact original; everything below it (the floor, the rounded bottom edge,
the bottoms of the screw-head holes) moves down by --extra. So:
  - the inside gets --extra mm deeper,
  - each screw-head hole gets --extra mm deeper, and the web the screw head pulls against stays the same distance
    from the top, so the SAME 20 mm screws reach exactly as far into the case top as before,
  - the lip, posts and screw positions that meet the case top are unchanged.

--plug-width (added 2026-11-15 evening): the two middle posts (x -5..+5, inner faces 4.62 from the middle, hanging from
the lip) slide down past the USB-C plug in the XL's port when the base goes on. Their inner faces are cut back so a
plug body that wide passes between them with 0.5 mm each side, centred on the XL (y -0.29: the socket's middle, from
the orbs board's v0.2 design file; the base's USB notch at x 14.5..24.8 confirms how the board sits in the case). Only
post material inside x -5.6..5.6, z -7.0..-17.5 is removed; the posts keep their full length and their outer part.

Output: base_xl_<extra>mm.stl next to this script. --check prints the checks (watertight, no mirror, every face cut
by the plane vertical, heights before/after).
"""
import os, sys
import numpy as np
import trimesh

XL_Y = -0.287            # the XL's centre line across the case (socket rows at y +11.213 / -11.787)
POST_X = 5.6             # middle posts are x -5..+5; the cut box reaches 0.6 past them
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "source", "bottom_20mmscrews.stl")
Z_CUT = -23.5            # no vertex between -25.5 (floor top) and -22.09; every face crossing it is vertical
HERE = os.path.dirname(os.path.abspath(__file__))


def stretch(mesh, z_cut, extra):
    out = mesh.copy()
    v = out.vertices.copy()
    v[v[:, 2] < z_cut, 2] -= extra
    out.vertices = v
    return out


def trim_posts(mesh, plug_width):
    half = np.ceil((plug_width / 2 + 0.5) * 10) / 10          # 0.5 mm each side, rounded up to 0.1
    lo, hi = XL_Y - half, XL_Y + half
    box = trimesh.creation.box(extents=[2 * POST_X, hi - lo, 10.5],
                               transform=trimesh.transformations.translation_matrix([0, (lo + hi) / 2, -12.25]))
    out = trimesh.boolean.difference([mesh, box], engine="manifold")
    return out, lo, hi


def faces_cut(mesh, z_cut):
    zs = mesh.vertices[mesh.faces][:, :, 2]
    return np.where((zs.min(1) < z_cut) & (zs.max(1) > z_cut))[0]


def ray_down(mesh, x, y):
    T = mesh.triangles; v0 = T[:, 0]; e1 = T[:, 1] - v0; e2 = T[:, 2] - v0
    o = np.array([x, y, 50.0]); d = np.array([0, 0, -1.0])
    p = np.cross(d, e2); det = (e1 * p).sum(1); ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1 / np.where(ok, det, 1), 0); s = o - v0
    u = (s * p).sum(1) * inv; q = np.cross(s, e1); w = (q * d).sum(1) * inv; t = (q * e2).sum(1) * inv
    sel = ok & (u >= -1e-9) & (w >= -1e-9) & (u + w <= 1 + 1e-9) & (t > 0)
    return sorted(set(np.round(50 - t[sel], 3)), reverse=True)


def main():
    extra = float(sys.argv[sys.argv.index("--extra") + 1]) if "--extra" in sys.argv else 3.0
    src = trimesh.load(SRC)
    vz = src.vertices[:, 2]
    assert not ((vz > -25.5 + 1e-6) & (vz < -22.09 - 1e-6)).any(), "a vertex sits in the stretch band"
    cut = faces_cut(src, Z_CUT)
    nz = np.abs(src.face_normals[cut][:, 2])
    out = stretch(src, Z_CUT, extra)
    plug = float(sys.argv[sys.argv.index("--plug-width") + 1]) if "--plug-width" in sys.argv else None
    if plug:
        before = out
        out, lo, hi = trim_posts(out, plug)
    path = os.path.join(HERE, f"base_xl_{extra:g}mm.stl")
    out.export(path)
    back = trimesh.load(path)
    # print copy for Bambu (2026-11-15): the same mesh moved up so its floor sits on the bed (z min 0); Bambu's
    # command line leaves a part that pokes below the bed off the plate. Pure move, nothing else changes.
    pr = back.copy(); pr.apply_translation([0, 0, -pr.bounds[0][2]])
    print_path = os.path.join(HERE, f"print_base_xl_{extra:g}mm.stl")
    pr.export(print_path)
    if "--check" in sys.argv:
        print(f"source  {SRC}")
        print(f"output  {path}")
        print(f"faces cut by z={Z_CUT}: {len(cut)}, largest |normal z| {nz.max():.2e} (0 = straight up and down)")
        print(f"source  z {src.bounds[0][2]:.2f} .. {src.bounds[1][2]:.2f}  height {src.extents[2]:.2f}  "
              f"watertight {src.is_watertight}  volume {src.volume:.0f} mm3")
        print(f"output  z {back.bounds[0][2]:.2f} .. {back.bounds[1][2]:.2f}  height {back.extents[2]:.2f}  "
              f"watertight {back.is_watertight}  volume {back.volume:.0f} mm3  (reloaded from the STL)")
        print(f"x/y footprint unchanged: {np.allclose(src.bounds[:, :2], back.bounds[:, :2])}")
        print("no mirror: only z of vertices below the plane moved, by a pure shift (determinant +1)")
        if plug:
            def post_faces(m, z):
                T = m.triangles; v0 = T[:, 0]; e1 = T[:, 1] - v0; e2 = T[:, 2] - v0
                o = np.array([0.0, -20.0, z]); d = np.array([0, 1.0, 0])
                p = np.cross(d, e2); det = (e1 * p).sum(1); ok = np.abs(det) > 1e-12
                inv = np.where(ok, 1 / np.where(ok, det, 1), 0); s_ = o - v0
                u = (s_ * p).sum(1) * inv; q = np.cross(s_, e1); w = (q * d).sum(1) * inv; t = (q * e2).sum(1) * inv
                sel = ok & (u >= -1e-9) & (w >= -1e-9) & (u + w <= 1 + 1e-9) & (t > 0)
                return sorted(set(np.round(-20 + t[sel], 2)))
            removed = before.volume - back.volume
            print(f"posts cut for a {plug:.2f} mm plug: gap now y {lo:.2f} .. {hi:.2f} ({hi-lo:.2f} wide, was 9.24); "
                  f"removed {removed:.0f} mm3 (posts only)")
            for z in (-7.6, -10.0, -14.0, -16.0):
                print(f"   y crossings at x=0, z={z}: {post_faces(back, z)}")
            print(f"   post tops that hold the board, each: {10.0 * (15.22 - hi):.0f} and {10.0 * (15.22 + lo):.0f} mm2 "
                  f"(were {10.0 * (15.22 - 4.62):.0f} each)")
        # heights measured with calipers on a printed base: lip (outside height), posts, inside depth, screw web
        for name, m in (("source", src), ("output", back)):
            wall = ray_down(m, 0, 16.2)          # inside the side wall
            floor = ray_down(m, -60, 0)          # open floor
            screw = ray_down(m, -96.41 + 2.2, 11.22)   # 2.2 mm off the screw axis: misses the 3.2 mm screw hole, hits the web
            print(f"{name}: lip top {wall[0]:.2f}, bottom face {wall[-1]:.2f} -> lip height {wall[0]-m.bounds[0][2]:.2f}; "
                  f"inside floor {floor[0]:.2f} -> inside depth {wall[0]-floor[0]:.2f}; "
                  f"screw web (top, bottom) {screw[0]:.2f}, {screw[1]:.2f} -> web to bottom face (screw-head hole depth) {screw[1]-m.bounds[0][2]:.2f}, web top to post top {m.bounds[1][2]-screw[0]:.2f}")
    if "--check" in sys.argv:
        prb = trimesh.load(print_path)
        same = np.allclose(np.sort(prb.vertices - [0, 0, prb.bounds[0][2] - back.bounds[0][2]], axis=0),
                           np.sort(back.vertices, axis=0), atol=1e-4)
        print(f"print copy {print_path}: z {prb.bounds[0][2]:.2f} .. {prb.bounds[1][2]:.2f}, watertight "
              f"{prb.is_watertight}, same shape moved up {-back.bounds[0][2]:.2f}: {same}")
    print(path)


if __name__ == "__main__":
    main()
