"""Bambu Studio project (.3mf) for the orbs' taller base (Pico LiPo 2 XL W swap): opens ready to slice (2026-11-15).

ONE plate "Orbs base (XL)" = print_base_xl_3mm.stl (made by make_base.py: the base moved up onto the bed), placed as it is (floor on the bed, the way the
original base file sits), never re-oriented. Settings: Bambu's stock "0.20mm Standard @BBL X1C" with nothing changed,
Generic PETG, Supertack Plate. Pick the filament slot in Bambu Studio. ADD A BRIM: the first print warped without one.
--check slices it on the command line and reads the settings back out of the G-code; nothing is sent to the printer.

Run: python3 make_project.py [--out PATH] [--check]   (needs Bambu Studio installed: PROFILES and CLI below)
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
STL = HERE
OUT = os.path.join(HERE, "orbs_base_xl_petg.3mf")
if "--out" in sys.argv:
    OUT = os.path.abspath(sys.argv[sys.argv.index("--out") + 1])
PICTURES = os.environ.get("PLATE_PICTURES_SCRIPT", "")   # (optional: a script that adds plate pictures; skipped if unset)
PROFILES = "/opt/bambustudio-bin/resources/profiles/BBL"
CLI = "/usr/bin/bambu-studio"
MACHINE = "Bambu Lab X1 Carbon 0.4 nozzle"
PROCESS = "0.20mm Standard @BBL X1C"
OVERRIDES = {}
PRINTED_WITH = {"wall_loops": "2", "sparse_infill_density": "15%", "sparse_infill_pattern": "grid",
                "top_shell_layers": "5", "bottom_shell_layers": "3", "enable_support": "0"}
BED = "Supertack Plate"
FILAMENT = "Generic PETG"
COLOUR = "#7FB069"
PLATES = [("Orbs base (XL)", ["print_base_xl_3mm.stl"])]


def load(kind, name):
    return json.load(open(os.path.join(PROFILES, kind, name + ".json")))


def resolve(kind, name):
    d = load(kind, name)
    base = resolve(kind, d["inherits"]) if d.get("inherits") else {}
    for inc in d.get("include", []):
        base.update({k: v for k, v in load(kind, inc).items() if k not in ("name", "type", "from", "instantiation")})
    base.update(d)
    base.pop("include", None)
    return base


def make():
    work = tempfile.mkdtemp(prefix="orbs_project_")
    m = resolve("machine", MACHINE)
    m.update({"name": MACHINE, "from": "system", "inherits": ""})
    p = resolve("process", PROCESS)
    p.update(OVERRIDES)
    p["curr_bed_type"] = BED
    p.update({"name": PROCESS + " (orbs base)", "from": "User", "inherits": PROCESS})
    f = resolve("filament", FILAMENT)
    f.update({"name": FILAMENT, "from": "system", "inherits": "", "filament_colour": [COLOUR]})
    paths = {k: os.path.join(work, k + ".json") for k in ("machine", "process", "filament")}
    for k, d in (("machine", m), ("process", p), ("filament", f)):
        json.dump(d, open(paths[k], "w"), indent=1)
    plates = [{"plate_name": name, "need_arrange": True,
               "objects": [{"path": os.path.abspath(os.path.join(STL, s)), "count": 1, "filaments": [1],
                            "assemble_index": [0], "pos_x": [0], "pos_y": [0], "pos_z": [0]} for s in stls]}
              for name, stls in PLATES]
    alist = os.path.join(work, "assemble_list.json")
    json.dump({"plates": plates}, open(alist, "w"), indent=1)
    side = os.path.join(os.path.dirname(OUT), "result.json")
    had_side = os.path.exists(side)
    r = subprocess.run([CLI, "--load-settings", f"{paths['machine']};{paths['process']}",
                        "--load-filaments", paths["filament"], "--load-assemble-list", alist,
                        "--orient", "0", "--arrange", "1", "--export-3mf", os.path.basename(OUT),
                        "--outputdir", os.path.dirname(OUT)], capture_output=True, text=True, timeout=600)
    if not had_side and os.path.exists(side):
        os.remove(side)
    if r.returncode != 0 or not os.path.exists(OUT):
        errors = [l for l in (r.stdout + r.stderr).splitlines() if "error" in l.lower()]
        sys.exit("bambu-studio failed (exit %d):\n%s" % (r.returncode, "\n".join(errors[-8:])))
    shutil.rmtree(work)
    gui_presets()
    if PICTURES and os.path.exists(PICTURES):
        subprocess.run([sys.executable, PICTURES, OUT], check=True)
    print("wrote", OUT)


def gui_presets():
    name = "Metadata/project_settings.config"
    with zipfile.ZipFile(OUT) as z:
        items = [(i, z.read(i.filename)) for i in z.infolist()]
    cfg = json.loads(dict((i.filename, d) for i, d in items)[name])
    cfg["print_settings_id"] = PROCESS
    diff = cfg.get("different_settings_to_system") or [""]
    diff[0] = ";".join(sorted(OVERRIDES))
    cfg["different_settings_to_system"] = diff
    tmp = OUT + ".tmp"
    with zipfile.ZipFile(tmp, "w") as z:
        for i, d in items:
            z.writestr(i, json.dumps(cfg, indent=4).encode() if i.filename == name else d)
    os.replace(tmp, OUT)


def check():
    ok = True

    def rep(good, text):
        nonlocal ok
        ok &= bool(good)
        print(("  ok   " if good else "  FAIL ") + text)

    with zipfile.ZipFile(OUT) as z:
        names = z.namelist()
        ms = z.read("Metadata/model_settings.config").decode()
        model = z.read("3D/3dmodel.model").decode()
    plates = re.findall(r'key="plater_name" value="([^"]*)"', ms)
    rep(plates == [n for n, _ in PLATES], f"plates: {plates}")
    rep(all(f"Metadata/plate_{i}.png" in names for i in range(1, len(PLATES) + 1)), "plate pictures in the file")
    items = re.findall(r'<item objectid="(\d+)" [^>]*?transform="([^"]+)"', model)
    rep(len(items) == sum(len(s) for _, s in PLATES), f"{len(items)} objects in the file")
    import numpy as np
    for oid, tr in items:
        a = [float(x) for x in tr.split()]
        R = np.array(a[:9]).reshape(3, 3)
        flat = np.allclose(R[2], [0, 0, 1], atol=1e-6) and np.allclose(R[:, 2], [0, 0, 1], atol=1e-6)
        turn = np.degrees(np.arctan2(R[0, 1], R[0, 0]))
        rep(flat and abs(np.linalg.det(R) - 1) < 1e-6,
            f"object {oid}: same face on the bed as exported (turned {turn:.0f} deg flat on the bed by arrange, "
            f"no tip, no mirror), at x {a[9]:.1f} y {a[10]:.1f}")
    work = tempfile.mkdtemp(prefix="orbs_check_")
    r = subprocess.run([CLI, "--slice", "0", "--export-3mf", "sliced.3mf", "--outputdir", work, OUT],
                       capture_output=True, text=True, timeout=900)
    sliced = os.path.join(work, "sliced.3mf")
    rep(r.returncode == 0 and os.path.exists(sliced), f"sliced on the command line (exit {r.returncode})")
    if not os.path.exists(sliced):
        return False
    with zipfile.ZipFile(sliced) as z:
        gcodes = sorted(n for n in z.namelist() if n.endswith(".gcode"))
        texts = {n: z.read(n).decode("utf-8", "replace") for n in gcodes}
    rep(len(gcodes) == len(PLATES), f"{len(gcodes)} plate(s) sliced")
    want = {"printer_model": "Bambu Lab X1 Carbon", "nozzle_diameter": "0.4", "layer_height": "0.2",
            "print_settings_id": PROCESS, "curr_bed_type": BED, **PRINTED_WITH, **OVERRIDES}
    import trimesh
    for n, g in texts.items():
        def val(key):
            mm = re.search(r"^; %s = (.*)$" % re.escape(key), g, re.M)
            return mm.group(1).strip() if mm else None
        bad = [f"{k}={val(k)}" for k, v in want.items()
               if not (val(k) == v or (val(k) or "").split(",")[0].strip() == v)]
        rep(not bad, f"{os.path.basename(n)}: settings read back from the G-code " + ("match" if not bad else str(bad)))
        fil = re.search(r"^; filament_settings_id = (.*)$", g, re.M)
        rep(fil and FILAMENT in fil.group(1), f"{os.path.basename(n)}: filament {fil.group(1) if fil else None}")
        idx = int(re.search(r"plate_(\d+)", n).group(1)) - 1
        h = max(float(trimesh.load(os.path.join(STL, s)).extents[2]) for s in PLATES[idx][1])
        zmax = float(re.search(r"^; max_z_height: (.*)$", g, re.M).group(1))
        rep(abs(zmax - h) <= 0.21, f"{os.path.basename(n)}: sliced height {zmax:.2f} = tallest part {h:.2f}")
        wt = re.search(r"^; total filament weight \[g\] : (.*)$", g, re.M)
        ln = re.search(r"^; total filament length \[mm\] : (.*)$", g, re.M) or re.search(r"^; filament used \[mm\] = (.*)$", g, re.M)
        tm = re.search(r"^; model printing time: ([^;]*)", g, re.M) or re.search(r"estimated printing time[^=]*= (.*)$", g, re.M)
        temp = val("nozzle_temperature")
        length = f"{float(ln.group(1).split(',')[0]) / 1000:.2f} m of filament" if ln else "length ?"
        print(f"       {os.path.basename(n)}: {wt.group(1).strip() if wt else '?'} g {FILAMENT}, {length}, "
              f"time {tm.group(1).strip() if tm else '?'}, nozzle {temp} C, bed {val('supertack_plate_temp')} C, "
              f"retraction {val('retraction_length')} mm")
    shutil.rmtree(work)
    return ok


if __name__ == "__main__":
    make()
    if "--check" in sys.argv:
        print("checking (sliced on the command line, nothing sent to the printer):")
        sys.exit(0 if check() else 1)
