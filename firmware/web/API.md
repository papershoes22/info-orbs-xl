# The orbs' settings page: its API (2026-11-17)

One contract for three things: the page (`web/index.html`), the stand-in the page is tested against on a PC
(`web/standin.py`) and the XL's own web server (`src/web.cpp`). Plain HTTP on port 80, home network only, no login
(2026-11-16 and 2026-11-17). The Wi-Fi password, the weather key and the Frigate login are never
sent or accepted here; they come over USB from the PC (`tools/orbs_xl.py wifi`, `key`, `frigate`) or, for the Wi-Fi only, the
setup hotspot's own page.

## Requests
- `GET /` the page (compressed).
- `GET /api/state` everything: `{"settings": {...}, "status": {...}, "meta": {...}}`.
- `POST /api/settings` a part of `settings` (any sections, any fields). Checked as a whole first: all or nothing.
  `{"ok": true, "settings": {...}}` or `{"ok": false, "errors": {"clock.color": "why", ...}}`. Shown on the orbs at once,
  written to flash 3 s after the last change.
- `POST /api/action` `{"do": "show", "set": "weather"}` | `{"do": "dark", "on": true}` (until turned on again or a
  restart, not saved) | `{"do": "test", "orb": 0}` (that orb's test picture for 5 s) | `{"do": "restart"}` |
  `{"do": "defaults"}` (every setting back to its default; Wi-Fi, key and Frigate login kept) |
  `{"do": "press", "button": "left"|"middle"|"right", "kind": "short"|"medium"|"long"}` (a press done by the orbs' own
  press code, exactly as the real button: short = a press, medium = held ½ s, long = held 2 s; the 15 s hold, which
  wipes everything, can't be sent). `{"ok": true}` or
  `{"ok": false, "error": "why"}`.
- `GET /api/geocode?q=Springfield,IL,US` up to 5 places from OpenWeatherMap's geocoding, asked by the XL with its key:
  `{"ok": true, "places": [{"name": "Springfield", "state": "Illinois", "country": "US", "lat": 39.80, "lon": -89.64}]}`.
- `GET /api/orb/<0-4>` what that orb shows now: the rig's last picture (JPEG) on the rig's sets, else the XL's own
  picture at half size (BMP, 120 x 120).
- `GET /api/backup` the settings as a file (`orbs-settings.json`, the same shape as `settings`).

## settings
```
sets:    order [10 set keys], enabled {key: bool}, cycle_s 0 | 5-3600, start "last" | set key
clock:   style "normal"|"nixie", format "24"|"12"|"12ampm", ticks bool, color "#rrggbb", shadow bool,
         shadow_color "#rrggbb", nixie_color "#rrggbb" ("#000000" = the tubes' own colour), tz (meta.zones), ntp host
weather: enabled bool, city (1-23 chars), lat -90..90, lon -180..180 (numbers), units "imperial"|"metric",
         dark bool, hl_cycle_s 0 | 3-600, refresh_min 5-60
matrix:  enabled bool, big_font bool, color, head_color, line_min 1-30, line_max line_min-40, speed_min 1-30,
         speed_max speed_min-40, frame_ms 20-1000
rig:     rig_hz 1|2|4, rig_middle "memory"|"health"|"power"|"cooling", cams_page1 [5 camera ids], cams_page2 [5 ids],
         cam_quality 60-95, cam_fps 1-15, viz_color "album"|"rainbow"|"#rrggbb"
screens: brightness 10-255, night {mode "off"|"dim"|"dark", start "HH:MM", end "HH:MM", brightness 5-255},
         rotation [5 x 0|90|180|270]
```
Set keys: clock weather hourly sky matrix rig music trove cams viz. Camera ids: cam1 ... cam7 (src/settings_json.cpp's
CAM_IDS: put your Frigate camera names there).

## status
`build, up_s, temp_c, clk_mhz, heap_kb, time ("1:23 AM"), showing (set key), dark (bool), wifi {ssid, ip, rssi, mac,
host}, rig {connected}, frigate {state, error}, weather {ok, age_s, error}, last_restart {watchdog, step, crash}`

## meta
`sets [{key, title, rig}], zones [IANA names], cams [{id, title}], defaults {the settings' defaults}`
