#!/usr/bin/env -S python3 -I
"""orbs_xl.py: the PC's side of the orbs' XL firmware (firmware/ in this repository), 2026-11-16. Linux (it reads
/sys to find the XL). Add --ser SERIAL to any command to pick one XL by its USB serial number.

  python3 -I orbs_xl.py hello              the XL's answer to "?" (build, SPI clock, MAC, uptime, restarts)
  python3 -I orbs_xl.py buttons [SECS]     every button press and release for SECS seconds (default 60), then totals
  python3 -I orbs_xl.py load [UF2]         new firmware onto the XL (default ../build/orbs_xl.uf2) with picotool, then
                                           checks it answers with that build
  python3 -I orbs_xl.py wifi               asks for the Wi-Fi name and password (typed in this terminal, never shown) and
                                           sends them to the XL over USB; they're kept only on the XL
  python3 -I orbs_xl.py key                asks for your OpenWeatherMap key (not shown) and sends it to the XL over USB
                                           (kept only on the XL, never printed)
  python3 -I orbs_xl.py set NAME           shows a set: clock weather hourly sky matrix (the rig's: rig music trove cams viz)
  python3 -I orbs_xl.py frigate HOST PORT USER   the XL's login for the cameras straight from a Frigate recorder when
                                           the rig isn't there (asks for the password, not shown), plus the SHA-256 of
                                           the certificate Frigate shows now, which the XL then trusts and no other:
                                           compare the printed fingerprint with Frigate's own (firmware/README.md)
  python3 -I orbs_xl.py frigate off        wipes that login from the XL
  python3 -I orbs_xl.py test-invoff D SECS  TEST: screen D (1-5) loses its colour inversion (black turns white) and the
                                           XL's 5 s settings refresh is held off for SECS seconds, then it puts it right
  python3 -I orbs_xl.py test-fetch-host H  TEST: GET / from host H over HTTPS (one whose certificate doesn't chain to the
                                           XL's roots must fail: proves the check is on)
  python3 -I orbs_xl.py test-fetch         TEST: the XL fetches the weather once over HTTPS and says how it went
  python3 -I orbs_xl.py bench              the live faces' drawing measured (needs live faces from the rig)

It only ever talks to a USB device whose product name is "Info Orbs XL" (this firmware's own), never another Pico.
To load firmware the XL must be running this firmware (so its serial number is read from it), or you give --ser;
every picotool command carries --ser. A brand-new XL (no firmware yet): hold BOOTSEL while plugging it in and copy
build/orbs_xl.uf2 onto the drive that appears instead.
"""
import getpass, glob, json, os, re, shutil, socket, subprocess, sys, termios, time, tty

PRODUCT = "Info Orbs XL"                     # USBD_PRODUCT in CMakeLists.txt
XL_SERIAL = None                             # from --ser, or read from the one running XL (find_xl)
PICOTOOL = os.environ.get("PICOTOOL") or shutil.which("picotool") or "picotool"
HERE = os.path.dirname(os.path.realpath(__file__))


def usb_devices():
    """(sysfs dir, vid, pid, serial) of every USB device"""
    out = []
    for d in glob.glob("/sys/bus/usb/devices/*"):
        try:
            vid = open(f"{d}/idVendor").read().strip()
            pid = open(f"{d}/idProduct").read().strip()
            ser = open(f"{d}/serial").read().strip() if os.path.exists(f"{d}/serial") else ""
        except OSError:
            continue
        out.append((d, vid, pid, ser))
    return out


def running_xls():
    """[(tty path, serial)] of every USB serial port whose device says it is an Info Orbs XL"""
    out = []
    for t in sorted(glob.glob("/sys/class/tty/ttyACM*")):
        dev = os.path.realpath(f"{t}/device/..")
        try:
            prod = open(f"{dev}/product").read().strip()
            ser = open(f"{dev}/serial").read().strip()
        except OSError:
            continue
        if prod == PRODUCT:
            out.append(("/dev/" + os.path.basename(t), ser))
    return out


def xl_tty():
    """the XL's serial port while its firmware runs, else None (with two or more XLs and no --ser: stops and asks)"""
    global XL_SERIAL
    xls = running_xls()
    if XL_SERIAL:
        return next((p for p, s in xls if s == XL_SERIAL), None)
    if len(xls) > 1:
        sys.exit("More than one Info Orbs XL on USB: say which with --ser " + " or ".join(s for _, s in xls))
    if xls:
        XL_SERIAL = xls[0][1]
        return xls[0][0]
    return None


def xl_in_bootsel():
    return bool(XL_SERIAL) and any(v == "2e8a" and p == "000f" and s == XL_SERIAL for _, v, p, s in usb_devices())


class Link:
    def __init__(self, path):
        dev = os.path.realpath(f"/sys/class/tty/{os.path.basename(path)}/device/..")
        ser = open(f"{dev}/serial").read().strip()
        prod = open(f"{dev}/product").read().strip() if os.path.exists(f"{dev}/product") else ""
        if prod != PRODUCT or (XL_SERIAL and ser != XL_SERIAL):
            sys.exit(f"{path} is {prod!r} serial {ser}, not the XL: refusing.")
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(self.fd)
        a = termios.tcgetattr(self.fd)
        a[6][termios.VMIN], a[6][termios.VTIME] = 0, 1
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.buf = b""

    def lines(self, timeout):
        """every line that comes within timeout seconds"""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.buf += os.read(self.fd, 4096)
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                yield l.decode(errors="replace").strip()

    def ask(self, cmd, want, timeout=3):
        os.write(self.fd, cmd)
        for l in self.lines(timeout):
            if l.startswith(want):
                return l
        raise TimeoutError(f"no {want!r} answer to {cmd!r}")


# (a PC program that feeds the orbs, not in this repository, may hold the XL's port and offer this socket instead)
CTL_SOCK = os.path.join(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"), "info-orbs", "link.sock")


GUARDED = b"WKLHXBSFGE"     # (the XL wants A5 5A after these letters since 2026-11-17: stray bytes can't make one)


def xl_ask(frame, want, timeout=5):
    """one message to the XL and its answer line: through the orbs feed (info-orbs service) when it holds the port,
    else straight to the port"""
    if frame[:1] and frame[0] in GUARDED:
        frame = frame[:1] + b"\xA5\x5A" + frame[1:]
    if os.path.exists(CTL_SOCK):
        try:
            c = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            c.settimeout(timeout + 5)
            c.connect(CTL_SOCK)
            c.sendall((json.dumps({"frame": frame.hex(), "want": [want], "timeout": timeout}) + "\n").encode())
            data = b""
            while not data.endswith(b"\n"):
                part = c.recv(65536)
                if not part:
                    break
                data += part
            c.close()
            r = json.loads(data)
            if "line" in r:
                return r["line"]
            raise TimeoutError(r.get("error", "no answer"))
        except (ConnectionRefusedError, FileNotFoundError):
            pass                                   # the feed isn't running: straight to the port
    p = xl_tty()
    if not p:
        raise TimeoutError("the XL isn't on USB with its firmware running")
    return Link(p).ask(frame, want, timeout)


def now():
    return time.strftime("%I:%M:%S %p")


def hello():
    try:
        print(xl_ask(b"?", "ORBS ").replace(" ", "\n  "))
        for pfx in ("PICS ", "NET ", "FRIGATE "):        # (the same "?" answers with these lines too)
            print(xl_ask(b"?", pfx).replace(" ", "\n  "))
    except TimeoutError as e:
        sys.exit(f"{e}" + (" (It's waiting in BOOTSEL.)" if xl_in_bootsel() else ""))


def bench():
    """asks the XL (over its settings page's own action, as the page would) to build every live face it holds whole,
    once on one core and once on both, then reads the result from its "?" answer"""
    import json, urllib.request
    ip = (xl_ask(b"?", "ORBS ").split("ip=")[1].split()[0])
    if ip in ("", "-"):
        sys.exit("the orbs aren't on Wi-Fi: the benchmark is asked for over their settings page")
    before = xl_ask(b"?", "BENCH ").split("runs=")[1].split()[0]
    r = urllib.request.Request(f"http://{ip}/api/action", data=b'{"do":"bench"}', headers={"Content-Type": "application/json"})
    urllib.request.urlopen(r, timeout=10).read()
    for _ in range(20):
        time.sleep(0.5)
        b = xl_ask(b"?", "BENCH ")
        if b.split("runs=")[1].split()[0] != before:
            break
    else:
        sys.exit("the XL didn't run it (no live faces loaded? show Rig stats first)")
    b1 = xl_ask(b"?", "BENCH1 ")
    kv = lambda line: {t.split("=")[0]: t.split("=")[1] for t in line.split()[1:] if "=" in t}
    two, one = kv(b), kv(b1)
    ms = lambda v: [round(int(x) / 1000, 1) for x in v.split(",")]
    o, t = ms(one["one_core_us"]), ms(two["orb_us"])
    print(f"each live face built whole, ms per orb (0 = no live face there): one core {o} = {sum(o):.1f} ms; both cores "
          f"{t} = {sum(t):.1f} ms ({sum(o) / max(sum(t), 0.1):.2f} times as fast)")
    print("the same pixels both ways:", "yes" if one.get("same") == "1" else "NO", "| the first core helps now:",
          "yes" if one.get("two_cores_now") == "1" else "no (test switch off)")


def frigate(args):
    if args[:1] == ["off"]:
        print(send_frame(b"L" + bytes([0, 0, 0, 0, 0]) + bytes(32), "LOGIN "))
        return
    if len(args) != 3 or not args[1].isdigit():
        sys.exit("frigate HOST PORT USER   (e.g. frigate 192.168.1.50 8971 viewer), or frigate off")
    host, port, user = args[0], int(args[1]), args[2]
    if not sys.stdin.isatty():
        sys.exit("Run this in a terminal (it asks for the password without showing it).")
    import hashlib, ssl
    der = ssl.PEM_cert_to_DER_cert(ssl.get_server_certificate((host, port), timeout=10))
    pin = hashlib.sha256(der).digest()
    print(f"Frigate's certificate at {host}:{port}: SHA-256 {pin.hex()}")
    if input("Is that Frigate's own certificate (checked on the Frigate machine itself)? [y/N] ").strip().lower() != "y":
        sys.exit("Nothing sent.")
    pw = getpass.getpass(f"Frigate password for {user} (not shown): ").encode()
    h, u = host.encode(), user.encode()
    frame = b"L" + bytes([len(h)]) + h + FRIGATE_PORT.to_bytes(2, "little") + bytes([len(u)]) + u + bytes([len(pw)]) + pw + pin
    print(send_frame(frame, "LOGIN "))
    del pw, frame


def buttons(secs):
    if os.path.exists(CTL_SOCK):
        print(f"{now()} (the orbs feed holds the port: totals after {secs} s; each press is in journalctl --user -u info-orbs only if"
              " it changed something)")
        time.sleep(secs)
        print(now(), xl_ask(b"b", "BTN "))
        return
    p = xl_tty()
    if not p:
        sys.exit("The XL isn't on USB with its firmware running.")
    link = Link(p)
    print(f"{now()} watching the buttons for {secs} s on {p} ...", flush=True)
    for l in link.lines(secs):
        if l.startswith("EV "):
            print(now(), l, flush=True)
    print(now(), link.ask(b"b", "BTN "))


def run(cmd, timeout, must=True):
    print("  $", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    # (picotool's progress bars left out: each step's last line says how it went)
    out = "\n".join(l for l in (r.stdout + r.stderr).splitlines() if l.strip() and "%" not in l)
    if out:
        print("   ", out.replace("\n", "\n    "))
    if r.returncode and must:
        sys.exit(f"  failed (exit {r.returncode})")
    return r.returncode


def load(uf2):
    uf2 = os.path.abspath(uf2)
    ver_file = os.path.join(os.path.dirname(uf2), "orbs_xl.version")
    want = open(ver_file).read().strip() if os.path.exists(ver_file) else None
    running = xl_tty()
    if not XL_SERIAL:
        sys.exit("No running Info Orbs XL found. Give its serial with --ser (picotool info shows it in BOOTSEL), or copy "
                 "the .uf2 onto its BOOTSEL drive yourself.")
    print(f"{now()} loading {uf2} (build {want}) onto the XL ({XL_SERIAL})")
    if running:
        print(f"{now()} the XL is running: asking it to restart into BOOTSEL")
        # (2026-11-17: at 250 MHz the XL drops off USB before picotool hears it agree, so picotool says "failed" (exit 249)
        # although the XL did go to BOOTSEL: what counts is whether it shows up there, checked next)
        rc = run([PICOTOOL, "reboot", "-u", "-f", "--ser", XL_SERIAL], 30, must=False)
        if rc:
            print(f"{now()} picotool said {rc}; waiting to see if the XL is in BOOTSEL anyway")
    elif not xl_in_bootsel():
        sys.exit("The XL isn't on USB (running or in BOOTSEL): nothing loaded.")
    end = time.monotonic() + 15
    while not xl_in_bootsel():
        if time.monotonic() > end:
            sys.exit("It didn't show up in BOOTSEL within 15 s: nothing loaded.")
        time.sleep(0.2)
    print(f"{now()} in BOOTSEL: writing and verifying")
    # (2026-11-17: right after it shows up, the system may not yet let this user open it: picotool says 249. A moment
    # first, and one more try)
    time.sleep(1.5)
    if run([PICOTOOL, "load", "-v", "-x", uf2, "--ser", XL_SERIAL], 180, must=False):
        print(f"{now()} trying the write once more")
        time.sleep(2)
        run([PICOTOOL, "load", "-v", "-x", uf2, "--ser", XL_SERIAL], 180)
    end = time.monotonic() + 20
    while not xl_tty():
        if time.monotonic() > end:
            sys.exit("Loaded, but its USB serial port didn't come back within 20 s.")
        time.sleep(0.2)
    time.sleep(0.5)
    l = None
    for _ in range(10):
        try:
            l = xl_ask(b"?", "ORBS ", 5)
            break
        except (TimeoutError, OSError):
            time.sleep(0.5)
    if not l:
        sys.exit("Loaded, but it doesn't answer \"?\".")
    print(f"{now()} it answers: {l}")
    if want and f"build={want} " not in l + " ":
        sys.exit(f"  but that's not build {want}.")
    print(f"{now()} OK: build {want} running")


def send_frame(frame, want, timeout=5):
    try:
        return xl_ask(frame, want, timeout)
    except TimeoutError as e:
        sys.exit(str(e))


def wifi():
    if not sys.stdin.isatty():
        sys.exit("Run this in a terminal (it asks for the password without showing it).")
    try:
        cur = subprocess.run(["nmcli", "-t", "-f", "active,ssid", "dev", "wifi"], capture_output=True, text=True,
                             timeout=10).stdout
        default = next((l.split(":", 1)[1] for l in cur.splitlines() if l.startswith("yes:")), "")
    except (OSError, subprocess.TimeoutExpired):
        default = ""
    name = input(f"Wi-Fi name [{default}]: ").strip() or default
    if not name:
        sys.exit("No name: nothing sent.")
    pw = getpass.getpass("Wi-Fi password (not shown): ")
    n, p = name.encode(), pw.encode()
    if not 1 <= len(n) <= 32 or len(p) > 64:
        sys.exit("The name must be 1-32 bytes and the password at most 64: nothing sent.")
    frame = b"W" + bytes([len(n)]) + n + bytes([len(p)]) + p
    del pw, p
    print(send_frame(frame, "WIFI "))
    del frame
    print("The XL joins it now; `orbs_xl.py hello` shows wifi=up and its address once it has.")


def key():
    if not sys.stdin.isatty():
        sys.exit("Run this in a terminal (it asks for the key without showing it).")
    k = getpass.getpass("OpenWeatherMap API key (not shown): ").strip().encode()
    if not 1 <= len(k) <= 63:
        sys.exit("No key (or longer than 63 characters): nothing sent.")
    print(send_frame(b"K" + bytes([len(k)]) + k, "KEY "))
    del k


def main():
    global XL_SERIAL
    a = sys.argv[1:]
    if "--ser" in a:
        i = a.index("--ser")
        if i + 1 >= len(a):
            sys.exit("--ser needs the XL's USB serial number")
        XL_SERIAL = a[i + 1]
        del a[i:i + 2]
    if not a or a[0] not in ("hello", "buttons", "load", "wifi", "key", "test-invoff", "test-fetch", "test-fetch-host", "set", "frigate",
                             "bench"):
        sys.exit(__doc__)
    if a[0] == "hello":
        hello()
    elif a[0] == "bench":
        bench()
    elif a[0] == "wifi":
        wifi()
    elif a[0] == "key":
        key()
    elif a[0] == "frigate":
        frigate(a[1:])
    elif a[0] == "set":
        names = ["clock", "weather", "hourly", "sky", "matrix", "rig", "music", "trove", "cams", "viz"]
        n = names.index(a[1]) if a[1] in names else int(a[1])
        print(send_frame(b"S" + bytes([n]), "SET "))
    elif a[0] == "test-fetch-host":
        h = a[1].encode()
        print(send_frame(b"G" + bytes([len(h)]) + h, "FETCH ", 40))
    elif a[0] == "test-fetch":
        print(send_frame(b"F", "FETCH ", 40))
    elif a[0] == "test-invoff":
        d, secs = int(a[1]), int(a[2])
        if not 1 <= d <= 5 or not 0 <= secs <= 3600:
            sys.exit("test-invoff D SECS: D 1-5, SECS 0-3600")
        print(send_frame(b"X" + bytes([d - 1]) + secs.to_bytes(2, "little"), "TEST "))
    elif a[0] == "buttons":
        buttons(float(a[1]) if len(a) > 1 else 60)
    else:
        load(a[1] if len(a) > 1 else os.path.join(HERE, "..", "build", "orbs_xl.uf2"))


if __name__ == "__main__":
    main()
