"""Reads a .kicad_pcb (KiCad 6-8 s-expressions) and prints: the board outline's extent (Edge.Cuts), every footprint's
reference, library name, side, position (mm, relative to the outline's top-left corner, x right, y down) and rotation,
and every pad of the socket/screen/button/USB footprints (absolute, same origin), plus mounting holes.
  python3 -I kicad_parts.py FILE"""
import math, re, sys

src = open(sys.argv[1]).read()


def parse(s):
    toks = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', s)
    stack, cur = [], []
    for t in toks:
        if t == "(":
            stack.append(cur); cur = []
        elif t == ")":
            done = cur; cur = stack.pop(); cur.append(done)
        else:
            cur.append(t.strip('"') if t.startswith('"') else t)
    return cur[0]


root = parse(src)
kids = lambda n, name: [c for c in n if isinstance(c, list) and c and c[0] == name]
xs, ys = [], []
for kind in ("gr_line", "gr_rect", "gr_arc", "gr_circle"):
    for g in kids(root, kind):
        if any(isinstance(c, list) and c[0] == "layer" and c[1] == "Edge.Cuts" for c in g):
            for c in g:
                if isinstance(c, list) and c[0] in ("start", "end", "mid", "center"):
                    xs.append(float(c[1])); ys.append(float(c[2]))
x0, y0 = min(xs), min(ys)
print(f"outline (Edge.Cuts): {max(xs) - x0:.2f} x {max(ys) - y0:.2f} mm; origin below = its top-left corner")
for fp in kids(root, "footprint") + kids(root, "module"):
    name = fp[1]
    at = kids(fp, "at")[0]
    fx, fy, rot = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0
    layer = kids(fp, "layer")[0][1]
    ref = "?"
    for p in kids(fp, "property"):
        if p[1] == "Reference":
            ref = p[2]
    for t in kids(fp, "fp_text"):
        if t[1] == "reference":
            ref = t[2]
    print(f"{ref:10} {name.split(':')[-1][:44]:44} {layer[:1]} at ({fx - x0:7.2f}, {fy - y0:6.2f}) rot {rot:g}")
    if not ref.startswith("G"):
        for pd in kids(fp, "pad"):
            pat = kids(pd, "at")[0]
            px, py = float(pat[1]), float(pat[2])
            a = math.radians(-rot)              # KiCad: pad offsets turn with the footprint (y down, angle counter-clockwise)
            ax = fx + px * math.cos(a) - py * math.sin(a)
            ay = fy + px * math.sin(a) + py * math.cos(a)
            drill = kids(pd, "drill")
            nets = kids(pd, "net")
            print(f"    pad {pd[1]:>3} ({ax - x0:7.2f}, {ay - y0:6.2f}) {pd[2]:5} drill {drill[0][1] if drill else '-':>4} net {nets[0][2] if nets and len(nets[0]) > 2 else '-'}")
