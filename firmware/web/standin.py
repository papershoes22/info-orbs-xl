#!/usr/bin/python3 -I
"""standin.py: a stand-in for the orbs' XL web server (web/API.md), for building and testing the settings page on a
PC while the real orbs keep running (2026-11-17). It listens on 127.0.0.1 only (nothing else on the network can reach
it), keeps its settings in memory, and checks every change with the same rules the XL uses. It never uses the weather
key: place searches get fixed answers. GET /__log (for the tests) lists the actions the page asked for.

  standin.py [PORT]      default 8099
"""
import copy, io, json, re, sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
SETS = [("clock", "Clock", False), ("weather", "Weather", False), ("hourly", "Hourly", False), ("sky", "Sky", False),
        ("matrix", "Matrix", False), ("rig", "Rig stats", True), ("music", "Music", True), ("trove", "Trove", True),
        ("cams", "Cameras", True), ("viz", "Visualizer", True), ("messages", "Messages", False)]
KEYS = [k for k, _, _ in SETS]
ORBS_KIND = {"clock": "together", "matrix": "together", "viz": "together", "weather": "screens", "hourly": "screens", "sky": "screens"}
THEMES = ["own", "ember", "ocean", "forest", "mono", "neon", "sunset", "ice"]
ACTIONS = ["own", "nothing", "prev_set", "next_set", "qr", "dark", "play_pause", "next_track", "prev_track", "cams_page",
           "clock_style", "clock_format", "middle_page", "brighter", "dimmer"]
STYLES = ["own", "ring", "needle", "bar", "number"]
EVENTS = ["music", "game", "rig_sleep", "person_door", "print_done"]
DAYS = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]
CATALOG = {"sets": {"rig": {"faces": ["CPU LOAD", "CPU TEMP", "MEMORY", "GPU TEMP", "GPU LOAD", "RIG HEALTH", "POWER", "COOLING"],
                            "themes": True, "styles": True},
                    "music": {"faces": ["UP NEXT", "THE SONG", "COVER", "TIME", "PLAYING FROM"], "themes": True},
                    "cams": {"faces": [], "note": "Which camera on which orb: the Rig tab, Cameras."}}}
CAMS = [("cam1", "Camera 1"), ("cam2", "Camera 2"), ("cam3", "Camera 3"), ("cam4", "Camera 4"), ("cam5", "Camera 5"),
        ("cam6", "Camera 6"), ("cam7", "Camera 7")]   # (the same as src/settings_json.cpp's CAM_IDS / CAM_TITLES)
CAM_IDS = [c for c, _ in CAMS]
ZONES = ["America/New_York", "America/Detroit", "America/Chicago", "America/Denver", "America/Phoenix",
         "America/Los_Angeles", "America/Anchorage", "Pacific/Honolulu", "America/Toronto", "America/Vancouver",
         "Europe/London", "Europe/Berlin", "Europe/Paris", "UTC"]
DEFAULTS = {
    "sets": {"order": KEYS[:], "enabled": {k: True for k in KEYS}, "cycle_s": 0, "start": "last"},
    "clock": {"style": "normal", "format": "12ampm", "ticks": True, "color": "#4aff00", "shadow": True,
              "shadow_color": "#201408", "nixie_color": "#000000", "tz": "America/New_York", "ntp": "pool.ntp.org"},
    "weather": {"enabled": True, "city": "New York", "lat": 40.7128, "lon": -74.006, "units": "imperial",
                "dark": True, "hl_cycle_s": 20, "refresh_min": 10},
    "matrix": {"enabled": True, "big_font": False, "color": "#31a110", "head_color": "#6ac67b", "line_min": 3,
               "line_max": 15, "speed_min": 3, "speed_max": 15, "frame_ms": 100},
    "rig": {"rig_hz": 4, "rig_middle": "memory", "kwh_price": 0.0, "cams_page1": CAM_IDS[:5],
            "cams_page2": ["cam6", "cam7", "cam1", "cam2", "cam3"],
            "cam_quality": 86, "cam_fps": 8, "viz_color": "album"},
    "screens": {"brightness": 255, "night": {"mode": "off", "start": "22:00", "end": "07:00", "brightness": 40},
                "rotation": [0, 0, 0, 0, 0], "orbs": [{"brightness": 100, "gain": [100, 100, 100]} for _ in range(5)]},
    "formats": {"temp": "C", "date": "mdy", "decimals": "auto"},
    "rules": [],
    "warnings": {"cpu": {"on": True, "limit": 90, "how": "colour"}, "gpu": {"on": True, "limit": 85, "how": "colour"},
                 "ssd": {"on": True, "limit": 70, "how": "colour"}, "ram": {"on": True, "limit": 80, "how": "colour"}},
    "custom": [{"kind": "none", "title": "", "text": "", "color": "#000000", "bg": "#000000", "size": "auto", "target": 0} for _ in range(8)],
}
DEFAULTS["sets"]["per"] = {k: {"cycle_s": 0, "brightness": 0, "theme": "own", "faces": [0, 1, 2, 3, 4],
                               "styles": ["own"] * 5,
                               "buttons": {b: {kd: "own" for kd in ("short", "medium", "long")} for b in ("left", "middle", "right")}}
                           for k in KEYS}
HEX = re.compile(r"^#[0-9a-fA-F]{6}$")
HHMM = re.compile(r"^([01]\d|2[0-3]):[0-5]\d$")
HOST = re.compile(r"^[A-Za-z0-9.-]{1,39}$")


def merge(base, patch, path, errors):
    """patch into base (dicts merge, everything else replaces); unknown fields are errors"""
    for k, v in patch.items():
        p = f"{path}.{k}" if path else k
        if k not in base:
            errors[p] = "not a setting"
        elif p == "custom":
            if not isinstance(v, list) or len(v) > 8:
                errors[p] = "up to 8 faces"
            else:
                for i, item in enumerate(v):
                    if item is None:
                        continue
                    if not isinstance(item, dict):
                        errors[f"custom.{i}"] = "a face"
                        continue
                    merge(base[k][i], item, f"custom.{i}", errors)
        elif isinstance(base[k], dict) and isinstance(v, dict):
            merge(base[k], v, p, errors)
        elif isinstance(base[k], dict):
            errors[p] = "must be a group of settings"
        else:
            base[k] = v


def check(s):
    """every rule from web/API.md; {path: why}"""
    e = {}

    def boolean(p, v):
        if not isinstance(v, bool):
            e[p] = "on or off"

    def number(p, v, lo, hi, zero_ok=False, integer=True):
        if isinstance(v, bool) or not isinstance(v, (int, float)) or (integer and int(v) != v):
            e[p] = "a whole number" if integer else "a number"
        elif not (lo <= v <= hi or (zero_ok and v == 0)):
            e[p] = (f"0 or {lo}-{hi}" if zero_ok else f"{lo} to {hi}")

    def one_of(p, v, opts):
        if v not in opts:
            e[p] = "one of " + ", ".join(map(str, opts))

    def colour(p, v):
        if not isinstance(v, str) or not HEX.match(v):
            e[p] = "a colour like #4aff00"
    st = s["sets"]
    if not isinstance(st["order"], list) or sorted(st["order"]) != sorted(KEYS):
        e["sets.order"] = "every set once"
    for k in KEYS:
        boolean(f"sets.enabled.{k}", st["enabled"].get(k))
    if set(st["enabled"]) != set(KEYS):
        e["sets.enabled"] = "every set"
    number("sets.cycle_s", st["cycle_s"], 5, 3600, zero_ok=True)
    one_of("sets.start", st["start"], ["last"] + KEYS)
    c = s["clock"]
    one_of("clock.style", c["style"], ["normal", "nixie"])
    one_of("clock.format", c["format"], ["24", "12", "12ampm"])
    for k in ("ticks", "shadow"):
        boolean(f"clock.{k}", c[k])
    for k in ("color", "shadow_color", "nixie_color"):
        colour(f"clock.{k}", c[k])
    one_of("clock.tz", c["tz"], ZONES)
    if not isinstance(c["ntp"], str) or not HOST.match(c["ntp"]):
        e["clock.ntp"] = "a server name (letters, digits, dots, dashes)"
    w = s["weather"]
    for k in ("enabled", "dark"):
        boolean(f"weather.{k}", w[k])
    if not isinstance(w["city"], str) or not (1 <= len(w["city"]) <= 23):
        e["weather.city"] = "1 to 23 characters"
    number("weather.lat", w["lat"], -90, 90, integer=False)
    number("weather.lon", w["lon"], -180, 180, integer=False)
    one_of("weather.units", w["units"], ["imperial", "metric"])
    number("weather.hl_cycle_s", w["hl_cycle_s"], 3, 600, zero_ok=True)
    number("weather.refresh_min", w["refresh_min"], 5, 60)
    m = s["matrix"]
    for k in ("enabled", "big_font"):
        boolean(f"matrix.{k}", m[k])
    for k in ("color", "head_color"):
        colour(f"matrix.{k}", m[k])
    number("matrix.line_min", m["line_min"], 1, 30)
    number("matrix.line_max", m["line_max"], 1, 40)
    if "matrix.line_min" not in e and "matrix.line_max" not in e and m["line_max"] < m["line_min"]:
        e["matrix.line_max"] = "at least the shortest line"
    number("matrix.speed_min", m["speed_min"], 1, 30)
    number("matrix.speed_max", m["speed_max"], 1, 40)
    if "matrix.speed_min" not in e and "matrix.speed_max" not in e and m["speed_max"] < m["speed_min"]:
        e["matrix.speed_max"] = "at least the slowest"
    number("matrix.frame_ms", m["frame_ms"], 20, 1000)
    r = s["rig"]
    one_of("rig.rig_hz", r["rig_hz"], [1, 2, 4])
    kp = r["kwh_price"]
    if isinstance(kp, bool) or not isinstance(kp, (int, float)) or not 0 <= kp <= 2.0:
        e["rig.kwh_price"] = "0 to 2.000 (dollars a kWh)"
    else:
        r["kwh_price"] = round(kp * 1000) / 1000
    one_of("rig.rig_middle", r["rig_middle"], ["memory", "health", "power", "cooling"])
    for p in ("cams_page1", "cams_page2"):
        if not isinstance(r[p], list) or len(r[p]) != 5 or any(x not in CAM_IDS for x in r[p]):
            e[f"rig.{p}"] = "five cameras"
    number("rig.cam_quality", r["cam_quality"], 60, 95)
    number("rig.cam_fps", r["cam_fps"], 1, 15)
    if r["viz_color"] not in ("album", "rainbow") and not (isinstance(r["viz_color"], str) and HEX.match(r["viz_color"])):
        e["rig.viz_color"] = "album, rainbow or a colour"
    sc = s["screens"]
    number("screens.brightness", sc["brightness"], 10, 255)
    one_of("screens.night.mode", sc["night"]["mode"], ["off", "dim", "dark"])
    for k in ("start", "end"):
        if not isinstance(sc["night"][k], str) or not HHMM.match(sc["night"][k]):
            e[f"screens.night.{k}"] = "a time like 22:00"
    number("screens.night.brightness", sc["night"]["brightness"], 5, 255)
    if not isinstance(sc["rotation"], list) or len(sc["rotation"]) != 5 or any(x not in (0, 90, 180, 270) for x in sc["rotation"]):
        e["screens.rotation"] = "five of 0, 90, 180, 270"
    # ---- version 4 ----
    if not isinstance(sc["orbs"], list) or len(sc["orbs"]) != 5:
        e["screens.orbs"] = "five orbs"
    else:
        for i, o in enumerate(sc["orbs"]):
            number(f"screens.orbs.{i}.brightness", o.get("brightness"), 50, 100)
            g = o.get("gain")
            if not isinstance(g, list) or len(g) != 3 or any(isinstance(x, bool) or not isinstance(x, int) or not 50 <= x <= 100 for x in g):
                e[f"screens.orbs.{i}.gain"] = "three of 50-100 (red, green, blue percent)"
    for k, ps in st["per"].items():
        b = f"sets.per.{k}"
        if k not in KEYS:
            e[b] = "not a set"
            continue
        number(f"{b}.cycle_s", ps["cycle_s"], 5, 3600, zero_ok=True)
        number(f"{b}.brightness", ps["brightness"], 10, 255, zero_ok=True)
        one_of(f"{b}.theme", ps["theme"], THEMES)
        f = ps["faces"]
        native = ORBS_KIND.get(k) == "screens"
        ok = isinstance(f, list) and len(f) == 5 and all(
            (x == "blank" and not native) or (isinstance(x, int) and not isinstance(x, bool) and 0 <= x <= (4 if native else 31)) for x in f)
        if ok and native and len(set(f)) != 5:
            ok = False
        if not ok:
            e[f"{b}.faces"] = "its five screens, each once" if native else "five faces (0-31 or blank)"
        if not isinstance(ps["styles"], list) or len(ps["styles"]) != 5 or any(x not in STYLES for x in ps["styles"]):
            e[f"{b}.styles"] = "five of own, ring, needle, bar, number"
        for bn, kinds in ps["buttons"].items():
            for kd, a in kinds.items():
                if not (a in ACTIONS or (isinstance(a, str) and a.startswith("show:") and a[5:] in KEYS)):
                    e[f"{b}.buttons.{bn}.{kd}"] = "one of the actions"
    fm = s["formats"]
    one_of("formats.temp", fm["temp"], ["C", "F"])
    one_of("formats.date", fm["date"], ["mdy", "dmy", "ymd"])
    one_of("formats.decimals", fm["decimals"], ["none", "auto", "more"])
    rl = s["rules"]
    if not isinstance(rl, list) or len(rl) > 12:
        e["rules"] = "up to 12 rules"
    else:
        for i, r in enumerate(rl):
            b = f"rules.{i}"
            if not isinstance(r, dict) or r.get("kind") not in ("time", "event"):
                e[f"{b}.kind"] = "time or event"
                continue
            if r.get("set") != "dark" and r.get("set") not in KEYS:
                e[f"{b}.set"] = "a set or dark"
            if r["kind"] == "time":
                d = r.get("days")
                if not isinstance(d, list) or not d or any(x not in DAYS for x in d):
                    e[f"{b}.days"] = "days like sun, mon"
                for k2 in ("start", "end"):
                    if not isinstance(r.get(k2), str) or not HHMM.match(r[k2]):
                        e[f"{b}.{k2}"] = "a time like 22:00"
            else:
                one_of(f"{b}.event", r.get("event"), EVENTS)
                number(f"{b}.secs", r.get("secs", 0), 5, 3600, zero_ok=True)
    for k, w2 in s["warnings"].items():
        boolean(f"warnings.{k}.on", w2["on"])
        number(f"warnings.{k}.limit", w2["limit"], 30, 110)
        one_of(f"warnings.{k}.how", w2["how"], ["colour", "pulse"])
    for i, cf in enumerate(s["custom"]):
        b = f"custom.{i}"
        one_of(f"{b}.kind", cf["kind"], ["none", "message", "countdown"])
        if not isinstance(cf["title"], str) or len(cf["title"]) > 23:
            e[f"{b}.title"] = "up to 23 characters"
        if not isinstance(cf["text"], str) or len(cf["text"]) > 47:
            e[f"{b}.text"] = "up to 47 characters"
        colour(f"{b}.color", cf["color"])
        colour(f"{b}.bg", cf["bg"])
        one_of(f"{b}.size", cf["size"], ["auto", "small", "medium", "big"])
        number(f"{b}.target", cf["target"], 0, 4102444800, zero_ok=True)
    return e


class XL:
    def __init__(self):
        self.settings = copy.deepcopy(DEFAULTS)
        self.showing, self.dark, self.t0, self.log = "clock", False, time.time(), []
        self.trying, self.try_until, self.try_saved = False, 0.0, None
        self.presets = [None] * 8

    def status(self):
        return {"build": "2611.standin", "up_s": int(time.time() - self.t0) + 3600, "temp_c": 32.4, "clk_mhz": 250,
                "heap_kb": 55, "time": time.strftime("%I:%M %p").lstrip("0"), "showing": self.showing, "dark": self.dark,
                "wifi": {"ssid": "HomeWiFi", "ip": "192.168.1.50", "rssi": -45, "mac": "28:cd:c1:00:00:01", "host": "info-orbs"},
                "rig": {"connected": True}, "frigate": {"state": "ok", "error": ""},
                "weather": {"ok": True, "age_s": 300, "error": ""}, "last_restart": {"watchdog": False, "step": "0000", "crash": "0"},
                "try": {"active": self.trying, **({"left_s": max(0, int(self.try_until - time.time()))} if self.trying else {})},
                "catalog": CATALOG}


xl = XL()


def orb_picture(i):
    """a stand-in for /api/orb/N: the orb's number and the set showing, as a JPEG"""
    from PIL import Image, ImageDraw
    im = Image.new("RGB", (120, 120), (0, 0, 0))
    d = ImageDraw.Draw(im)
    hue = [(74, 255, 0), (80, 160, 255), (255, 170, 60), (220, 90, 255), (60, 220, 200)][i]
    d.ellipse([10, 10, 110, 110], outline=hue, width=4)
    d.text((60, 52), f"D{i + 1}", fill=hue, anchor="mm")
    d.text((60, 72), xl.showing, fill=(200, 200, 200), anchor="mm")
    b = io.BytesIO()
    im.save(b, "JPEG", quality=80)
    return b.getvalue()


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json", extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/":
            self.send(200, (HERE / "index.html").read_bytes(), "text/html; charset=utf-8")
        elif p == "/wifi":                                   # (the setup hotspot's page, made-up networks)
            self.send(200, (HERE / "wifi.html").read_bytes(), "text/html; charset=utf-8")
        elif p == "/api/wifi/networks":
            self.send(200, {"networks": [{"ssid": "HomeWiFi", "rssi": -48, "secured": True},
                                         {"ssid": "Neighbour 5G", "rssi": -71, "secured": True},
                                         {"ssid": "Coffee shop", "rssi": -83, "secured": False}]})
        elif p == "/api/state":
            self.send(200, {"settings": xl.settings, "status": xl.status(),
                            "meta": {"sets": [{"key": k, "title": t, "rig": r, "orbs": ORBS_KIND.get(k, "faces")} for k, t, r in SETS],
                                     "zones": ZONES, "cams": [{"id": c, "title": t} for c, t in CAMS], "defaults": DEFAULTS,
                                     "themes": THEMES, "actions": ACTIONS}})
        elif p == "/api/geocode":
            q = self.path.split("q=", 1)[-1]
            xl.log.append({"geocode": q})
            self.send(200, {"ok": True, "places": [
                {"name": "Springfield", "state": "Illinois", "country": "US", "lat": 39.7990, "lon": -89.6440},
                {"name": "Springfield", "state": "Massachusetts", "country": "US", "lat": 42.1015, "lon": -72.5898}]})
        elif re.match(r"^/api/orb/[0-4]$", p):
            self.send(200, orb_picture(int(p[-1])), "image/jpeg")
        elif p == "/api/backup":
            self.send(200, xl.settings, "application/json", {"Content-Disposition": 'attachment; filename="orbs-settings.json"'})
        elif p == "/api/presets":
            self.send(200, {"presets": [{"slot": q["slot"], "name": q["name"]} if q else None for q in xl.presets]})
        elif p == "/__log":
            self.send(200, xl.log)
        else:
            self.send(404, {"ok": False, "error": "no such page"})

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return self.send(400, {"ok": False, "error": "not JSON"})
        if self.path == "/api/settings":
            new, errors = copy.deepcopy(xl.settings), {}
            if not isinstance(body, dict):
                return self.send(200, {"ok": False, "errors": {"": "must be a group of settings"}})
            merge(new, body, "", errors)
            if not errors:
                errors = check(new)
            if errors:
                xl.log.append({"refused": body, "errors": errors})
                return self.send(200, {"ok": False, "errors": errors})
            xl.settings = new
            xl.log.append({"changed": body})
            self.send(200, {"ok": True, "settings": xl.settings})
        elif self.path == "/api/presets":
            d, slot = body.get("do"), body.get("slot")
            xl.log.append({"preset": body})
            if not isinstance(slot, int) or not 0 <= slot < 8:
                return self.send(200, {"ok": False, "error": "slot 0-7", "presets": xl.presets})
            if d == "save":
                name = body.get("name") or ""
                if not 1 <= len(name) <= 23:
                    return self.send(200, {"ok": False, "error": "a name of 1 to 23 characters", "presets": xl.presets})
                xl.presets[slot] = {"slot": slot, "name": name, "_s": copy.deepcopy(xl.settings)}
            elif d == "apply":
                if not xl.presets[slot]:
                    return self.send(200, {"ok": False, "error": "that preset is empty", "presets": xl.presets})
                keep = xl.settings
                xl.settings = copy.deepcopy(xl.presets[slot]["_s"])
                xl.settings["weather"].update({k: keep["weather"][k] for k in ("city", "lat", "lon")})
                xl.settings["clock"].update({k: keep["clock"][k] for k in ("tz", "ntp")})
            elif d == "delete":
                xl.presets[slot] = None
            else:
                return self.send(200, {"ok": False, "error": "save, apply or delete", "presets": xl.presets})
            self.send(200, {"ok": True, "presets": [{"slot": p["slot"], "name": p["name"]} if p else None for p in xl.presets]})
        elif self.path == "/api/wifi":
            ssid, pw = body.get("ssid", ""), body.get("password", "")
            xl.log.append({"wifi": {"ssid": ssid, "password_len": len(pw)}})   # (the stand-in never keeps the password)
            if not 1 <= len(ssid) <= 32:
                return self.send(200, {"ok": False, "error": "the network name: 1 to 32 characters"})
            if pw and not 8 <= len(pw) <= 63:
                return self.send(200, {"ok": False, "error": "a Wi-Fi password is 8 to 63 characters, or none for an open network"})
            self.send(200, {"ok": True})
        elif self.path == "/api/action":
            d = body.get("do")
            xl.log.append({"action": body})
            if d == "show" and body.get("set") in KEYS:
                xl.showing = body["set"]
            elif d == "dark":
                xl.dark = bool(body.get("on"))
            elif d == "defaults":
                xl.settings = copy.deepcopy(DEFAULTS)
            elif d in ("restart", "test"):
                pass
            elif d == "press":
                if body.get("button") not in ("left", "middle", "right") or body.get("kind") not in ("short", "medium", "long"):
                    return self.send(200, {"ok": False, "error": "button left, middle or right; kind short, medium or long"})
                # (the stand-in only follows the plain left / right press: previous / next set in the order)
                if body["kind"] == "short" and body["button"] != "middle":
                    on = [k for k in xl.settings["sets"]["order"] if xl.settings["sets"]["enabled"].get(k)]
                    if xl.showing in on and on:
                        xl.showing = on[(on.index(xl.showing) + (1 if body["button"] == "right" else -1)) % len(on)]
            elif d == "try":
                if not xl.trying:
                    xl.trying, xl.try_saved = True, copy.deepcopy(xl.settings)
                xl.try_until = time.time() + 30
            elif d == "keep" and xl.trying:
                xl.trying = False
            elif d == "revert" and xl.trying:
                xl.trying, xl.settings = False, xl.try_saved
            else:
                return self.send(200, {"ok": False, "error": "unknown action"})
            self.send(200, {"ok": True})
        else:
            self.send(404, {"ok": False, "error": "no such page"})


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
    print(f"stand-in on http://127.0.0.1:{port}/", flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
