/* Info Orbs on the Pimoroni Pico LiPo 2 XL W, build step 2 (2026-11-16): the orbs' own sets,
 * drawn by the original firmware's own drawing code (screen.cpp, clock_widget.cpp), with the time from the network.
 * Step 2a: the Clock set (Normal + Nixie, 12/24 h), Wi-Fi and the time. Weather, Hourly, Sky and Matrix follow.
 *
 * Buttons as the original (on release: < 0.5 s short, 0.5-2 s medium, 2-15 s long, 15 s+ very long):
 *   left short = previous set, right short = next set, anything else goes to the set showing
 *   (Clock: middle short = Normal/Nixie, middle medium = 24 h / 12 h / 12 h + AM/PM); left held 15 s = settings
 *   wiped (Wi-Fi and key too) and a restart.
 *
 * USB serial (rig/orbs_xl.py). Text answers end in "\n"; the XL also sends "EV ..." lines by itself.
 *   "?"                                -> "ORBS 2 build=... wifi=... ip=... time=... ..." (never the password or key)
 *   "b"                                -> "BTN left=<n> middle=<n> right=<n> held=<lmr> lost=<n>"
 *   "W" u8 n, n bytes name, u8 m, m bytes password  -> "WIFI saved ssid=<name> pass_len=<m>" (the password isn't echoed)
 *   "K" u8 n, n bytes key              -> "KEY saved len=<n>"
 *   "F"                                -> TEST: fetch the weather once over HTTPS, then
 *                                         "FETCH status=<http> bytes=<n> ms=<n> heap_used=<n>" or "FETCH failed: <why>"
 *   "G" u8 n, n bytes host             -> TEST: GET / from that host over HTTPS (a host whose certificate doesn't chain
 *                                         to the XL's roots must FAIL), then "FETCH ..." as above
 *   "B" u8 button, u8 kind             -> TEST: a press through the real press code (0 left 1 middle 2 right;
 *                                         1 short 2 medium 3 long, never the 15 s wipe): "TESTPRESS <b> <k>"
 *   (since 2026-11-17 the commands that change something - W K L H X B S F G - carry two code bytes A5 5A right
 *    after their letter; without them they're dropped: orbs_xl.py adds them)
 *   "S" u8 set                         -> shows that set (0 Clock, 1 Weather, 2 Hourly, 3 Sky, 4 Matrix): "SET <n>"
 *   (the rig's own: "Q" status, "P" a picture, "V" visualizer levels: src/rig.h)
 *   "X" u8 orb, u16 seconds            -> TEST: that screen's colour inversion off, the 5 s settings refresh held off
 *                                         for the seconds given, then it puts the screen right by itself
 */
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "hardware/psram.h"
#include "screen.h"
#include "clock_widget.h"
#include "weather.h"
#include "sets_new.h"
#include "matrix_widget.h"
#include "rig.h"
#include "pics.h"
#include "frigate_cams.h"
#include "web.h"
#include "settings_json.h"
extern "C" {
#include "hotspot.h"
}
extern "C" {
#include "../third_party/qrcodegen/qrcodegen.h"
}
#include "pico/rand.h"
#include "hardware/adc.h"
#include "hardware/vreg.h"
#include "hardware/structs/qmi.h"
#include "lwip/opt.h"
extern "C" {
#include "lcd.h"
#include "net.h"
#include "orbs_time.h"
#include "settings.h"
#include "presets.h"
#include "https.h"
#include "camlogin.h"
}

extern "C" uint32_t orbs_millis(void) { return to_ms_since_boot(get_absolute_time()); }

/* ---- what was happening when it last stopped (2026-11-16: one watchdog restart 10 s after a load, cause
   unknown). Kept in the watchdog's scratch registers, which survive its restart (the SDK uses 4-7, these are 0-3):
   0 = the first core's last step in its loop (and the USB command it was on), 1-3 = a crash: where (pc, lr) and
   which core. Said in "EV START" and "?" as last_step= / crash=. ---- */
#define STEP_MAGIC 0x57E90000u
#define CRASH_MAGIC 0xC8A50000u
static inline void step(uint32_t n, uint32_t cmd = 0) { if(get_core_num() == 0) watchdog_hw->scratch[0] = STEP_MAGIC | (cmd & 0xFF) << 8 | (n & 0xFF); }
extern "C" void orbs_step(uint32_t n) { step(n); }       /* (for lcd.c: the screen bus) */
static uint32_t boot_step, boot_crash_pc, boot_crash_lr, boot_crash_core;
extern "C" void __attribute__((used)) orbs_hardfault_c(uint32_t *sp)
{
	watchdog_hw->scratch[1] = sp[6];                /* the stacked pc */
	watchdog_hw->scratch[2] = sp[5];                /* lr */
	watchdog_hw->scratch[3] = CRASH_MAGIC | get_core_num();
	for(;;)
		tight_loop_contents();                       /* (the watchdog restarts it; the second core: "EV STUCK") */
}
extern "C" void __attribute__((naked)) isr_hardfault(void)
{
	__asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b orbs_hardfault_c\n");
}
extern "C" uint32_t orbs_random32(void) { return get_rand_32(); }

/* each orb's picture in the XL's PSRAM (5 x 115,200 bytes) */
static uint16_t fb_psram[N_ORB_FB][ORB_W * ORB_H] __uninitialized_psram("orbfb");

/* ---- buttons: 1 ms timer, 35 ms debounce (the original's), presses timed on release ---- */
static const uint8_t btn_pin[3] = { 18, 19, 20 };          /* LFT, OK, RHT */
static const char *const btn_name[3] = { "LEFT", "MIDDLE", "RIGHT" };
#define DEBOUNCE_MS 35
typedef struct { uint8_t btn, down; uint32_t t; } bev_t;
#define BQ 32
static bev_t bq[BQ];
static volatile uint32_t bq_in, bq_out, bq_lost;
static bool b_state[3];
static uint8_t b_count[3];
static uint32_t presses[3], down_at[3];
static int8_t page_press_b = -1, page_press_k;              /* a press sent from the settings page, done by the main loop */

static bool btn_tick(repeating_timer_t *)
{
	for(int i = 0; i < 3; i++)
	{
		bool raw = gpio_get(btn_pin[i]);
		if(raw == b_state[i])
		{
			b_count[i] = 0;
			continue;
		}
		if(++b_count[i] < DEBOUNCE_MS)
			continue;
		b_state[i] = raw;
		b_count[i] = 0;
		if(bq_in - bq_out >= BQ)
		{
			bq_lost++;
			continue;
		}
		bq[bq_in % BQ] = (bev_t){ (uint8_t)i, (uint8_t)raw, to_ms_since_boot(get_absolute_time()) };
		__dmb();
		bq_in++;
	}
	return true;
}

static ButtonState press_state(uint32_t ms)
{
	if(ms >= 15000) return BTN_VERY_LONG;
	if(ms >= 2000) return BTN_LONG;
	if(ms >= 500) return BTN_MEDIUM;
	return BTN_SHORT;
}

/* ---- the sets ---- */
static uint32_t wx_next_ms;
static TFT_eSPI tft;
static ScreenManager screens(tft);
static ClockWidget clockw(screens);
static WeatherData wxd;
static WeatherWidget weatherw(screens, wxd);
static SkyExtra skyx;
static HourlyWidget hourlyw(screens, wxd);
static SkyWidget skyw(screens, wxd, skyx);
static MatrixWidget matrixw(screens);
static RigPictureSet rigpics(screens);
static VisualizerWidget vizw(screens);
static FrigateCams fgcams(screens);
static bool cams_from_rig = true;            /* the Cameras set: the rig's live video, or (rig away) Frigate's pictures */
orbs_tz_t *frigate_tz;                       /* (frigate_cams.cpp: the time on its labels) */
bool frigate_12h = true;
/* the default order; the rig's sets only while the rig is there */
enum { SET_CLOCK, SET_WEATHER, SET_HOURLY, SET_SKY, SET_MATRIX, SET_RIG, SET_MUSIC, SET_TROVE, SET_CAMS, SET_VIZ,
       SET_PRINTER, SET_CLAUDE, SET_AI, SET_EVENTS, SET_HOUSE, SET_NETWORK, SET_BACKUPS, SET_PROJECTS, SET_MOVERS, SET_GAMING,
       SET_RADAR, SET_MESSAGES, N_SETS };    /* (from PRINTER on: sets added 2026-11-17) */
static const int set_rig[N_SETS] = { -1, -1, -1, -1, -1, RIG_SET_RIG, RIG_SET_MUSIC, RIG_SET_TROVE, RIG_SET_CAMS, RIG_SET_VIZ,
                                     RIG_SET_PRINTER, RIG_SET_CLAUDE, RIG_SET_AI, RIG_SET_EVENTS, RIG_SET_HOUSE, RIG_SET_NETWORK,
                                     RIG_SET_BACKUPS, RIG_SET_PROJECTS, RIG_SET_MOVERS, RIG_SET_GAMING, RIG_SET_RADAR, -1 };
static const char *const set_title[N_SETS] = { "CLOCK", "WEATHER", "HOURLY", "SKY", "MATRIX", "RIG STATS", "MUSIC", "TROVE",
                                               "CAMERAS", "VISUALIZER", "3D PRINTER", "CLAUDE", "AI AND ART", "SECURITY",
                                               "HOUSE", "NETWORK", "BACKUPS", "PROJECTS", "TROVE MOVERS", "GAMING", "RADAR",
                                               "MESSAGES" };
static_assert(N_SETS <= N_SETS_SAVED, "the settings have room for every set");

/* a set is in the rotation if it's switched on (settings page), its data can come (the weather on, the rig there, or
   for Cameras a Frigate login) */
static bool messages_any(void);
static bool set_available(int s)
{
	if(s < 0 || s >= N_SETS || !(settings.set_enabled >> s & 1) || !(settings_sets_known() >> s & 1))
		return false;                          /* (switched off, or a new set not built yet / not in the rig's catalogue) */
	if((s == SET_WEATHER || s == SET_HOURLY || s == SET_SKY) && !settings.weather_enabled)
		return false;
	if(s == SET_CAMS && camlogin_set())
		return true;                           /* (from Frigate while the rig is away) */
	if(s == SET_MESSAGES && !messages_any())
		return false;                          /* (nothing written yet) */
	return set_rig[s] < 0 || rig.alive(to_ms_since_boot(get_absolute_time()));
}

/* the next set in the chosen order (dir +1 / -1) that's available; nothing else available: the same set, and if even
   that isn't, the Clock (something always shows) */
static int next_set(int from, int dir)
{
	int pos = 0;
	for(int i = 0; i < N_SETS_SAVED; i++)
		if(settings.set_order[i] == from)
			pos = i;
	for(int k = 1; k <= N_SETS_SAVED; k++)
	{
		int s = settings.set_order[((pos + dir * k) % N_SETS_SAVED + N_SETS_SAVED) % N_SETS_SAVED];
		if(set_available(s))
			return s;
	}
	return set_available(from) ? from : SET_CLOCK;
}
static int current_set = SET_CLOCK;
static uint32_t set_since_ms;                /* (auto-cycle: when this set came up, or the last button press) */
static bool manual_dark;                     /* the page's "Screens dark" (until turned on, or a button, or a restart) */
static uint8_t shown_brightness = 255;       /* the brightness the orbs show with now (day, night or dark) */
static uint32_t night_wake_until;            /* a button pressed at night-dark: on for a minute */
static uint32_t overlay_until;               /* a test picture or the QR code is up: the set isn't drawn meanwhile */
static int overlay_kind;                     /* 1 test picture, 2 the QR code, 3 the setup hotspot's screens */
static uint32_t save_due_ms;                 /* a change from the page: saved to flash 3 s after the last one */
/* the start-up set (2026-11-19): the set a person picked last (a button, the page's "show", `orbs_xl.py set`) is kept in
   settings.current_set and opened at start-up when the page says "the last one you picked". Sets the orbs move to by
   themselves (the auto-cycle, a rule, on from a rig set when the rig goes away) aren't kept, so they never wear the
   flash and a restart goes back to your pick. Before: whatever showed at the last settings save. */
static int picked_set = -1;                  /* the set a person picked last (-1: none yet) */
static int saved_pick = -1;                  /* the pick the flash holds */
static uint32_t pick_save_due_ms;            /* a pick: saved to flash 20 s after the last one (not while trying) */
static int rig_waits = -1;                   /* a rig set waiting for the rig's feed: shown again as soon as the feed comes,
                                                unless a person picks a set first. Two ways in: start-up on a rig set
                                                before the feed is there (after a power cut the orbs start long before
                                                the PC), and the feed stopping while a rig set shows (the PC rebooting
                                                or asleep while the orbs keep their USB power, the feed program restarting;
                                                2026-11-20: before, the orbs stayed on the set they'd moved to) */
static bool rig_waits_start;                 /* (it came from start-up: for the log line) */
/* the page's "try before saving" (version 4): changes show but aren't saved; back to the saved ones 30 s after the last
   change unless kept */
static bool try_active;
static uint32_t try_until;
static orbs_settings_t try_saved;
/* rules (version 4): a time rule's or an event's set showing, and where to go back to */
static int rule_active = -1, rule_back_set = -1;
static orbs_rule_t rule_shown;               /* the rule showing, as it was when it came on: a change or removal of it on
                                                the page is noticed (rules_poll, 2026-11-21) */
static uint32_t rule_until;                  /* an event rule with seconds: back then (0 = when the event ends) */
static bool rule_dark;                       /* a rule made the screens dark */
/* the rig's catalogue of sets and faces (JSON, "U" kind CATALOG), for the page */
static char catalog_json[12 * 1024] __uninitialized_psram("catalog");   /* (the rig's catalogue: ~5 KB with every set; PSRAM, 2026-11-17) */
static uint32_t catalog_at;
static uint32_t settings_version = 1;        /* +1 on every change (the rig asks "R" when it moves) */
static uint32_t orb_pics_wanted_until;       /* the page shows the orbs' pictures: keep the rig's last ones */
static uint32_t restart_at;                  /* the page's Restart: after its answer has gone */
static bool orbs_dark(void);
static void pics_settle(void);
static void save_settings(void);
static uint32_t draw_ms, draws, draw_max_ms, frames_sent;
static orbs_tz_t tz;
static int64_t last_shown_s = -1;
static char status_shown[96];

static ClockTime clock_time_now(void)
{
	orbs_tm_t tm;
	orbs_tz_local(&tz, orbs_time_now(), &tm);
	ClockTime t;
	t.minute = tm.min;
	t.second = tm.sec;
	t.pm = tm.hour >= 12;
	if(clockw.m_format == CLOCK_FORMAT_24_HOUR)
		t.hour = tm.hour;
	else
		t.hour = tm.hour % 12 == 0 ? 12 : tm.hour % 12;   /* hourFormat12 */
	return t;
}

static WeatherTime weather_time_now(void)
{
	orbs_tm_t tm;
	orbs_tz_local(&tz, orbs_time_now(), &tm);
	WeatherTime t;
	t.hour = clockw.m_format == CLOCK_FORMAT_24_HOUR ? tm.hour : (tm.hour % 12 == 0 ? 12 : tm.hour % 12);
	t.minute = tm.min;
	t.mday = tm.mday;
	t.mon = tm.mon;
	t.wday = tm.wday;
	return t;
}

static void load_clock_settings(void)
{
	clockw.m_type = settings.clock_type <= 1 ? settings.clock_type : 0;
	clockw.m_format = settings.clock_format <= 2 ? settings.clock_format : 2;
	clockw.m_showSecondTicks = settings.show_second_ticks;
	clockw.m_shadowing = settings.clock_shadowing;
	clockw.m_fgColor = rgb888_to_565(settings.clock_color);           /* (settings keep 0xRRGGBB; the drawing RGB565) */
	clockw.m_shadowColor = rgb888_to_565(settings.clock_shadow_color);
	/* (the original passes its RGB565 Nixie colour on to the picture colouriser too: kept, for the same look) */
	clockw.m_overrideNixieColor = settings.clock_nixie_color ? rgb888_to_565(settings.clock_nixie_color) : 0;
	const char *rule = orbs_tz_rule_for(settings.tz_name);
	if(!rule || !orbs_tz_parse(rule, &tz))
		orbs_tz_parse("EST5EDT,M3.2.0,M11.1.0", &tz);
}

static void show_setup(void);
static bool setup_shown;                        /* the setup hotspot's screens are what's on the orbs */
/* the setup screens stay for as long as the hotspot is open (2026-11-17; before, the sets came
   back after a minute) */
static bool setup_up(void) { return setup_shown && net_hotspot(); }
/* something over the set (a test picture, the QR code, the setup screens): the set isn't drawn, the rig sends nothing */
static bool covered(void) { return overlay_until || setup_up(); }

/* until the time is known: what's happening, on the middle orb (the original's Roboto) */
static void show_status(void)
{
	if(net_hotspot())
	{
		if(!setup_shown)
			show_setup();                      /* (no Wi-Fi: the setup hotspot's screens instead) */
		return;
	}
	setup_shown = false;
	char line[96];
	if(!settings.wifi_ssid[0])
		snprintf(line, sizeof line, "Wi-Fi not set up|on the rig: orbs wifi");
	else if(!net_up())
		snprintf(line, sizeof line, "Joining Wi-Fi|%s", settings.wifi_ssid);
	else
		snprintf(line, sizeof line, "Getting the time|%s", settings.ntp_server);
	if(strcmp(line, status_shown) == 0)
		return;
	strcpy(status_shown, line);
	screens.clearAllScreens();
	screens.setFont(ROBOTO_REGULAR);
	screens.selectScreen(2);
	char *bar = strchr(line, '|');
	*bar = 0;
	screens.setFontColor(TFT_WHITE, TFT_BLACK);
	screens.drawString(line, 120, 100, 16, Align::MiddleCenter);
	screens.setFontColor(0x8410, TFT_BLACK);
	screens.drawString(bar + 1, 120, 140, 12, Align::MiddleCenter);
	screens.flush();
}

static bool wx_waiting_shown;

/* the Weather set before its first answer: what it's waiting for, on the middle orb */
/* while it lives, the XL's own sets of five screens (Weather, Hourly, Sky) draw each screen on the orb the page placed it
   on ("Which face on which orb"). 2026-11-18: the page saved the choice and
   nothing here used it. Only around those sets' own drawing: overlays (QR code, messages) keep their orbs. */
struct FaceMap {
	FaceMap() { screens.setFaceMap(settings.set_faces[current_set]); }
	~FaceMap() { screens.setFaceMap(nullptr); }
};

static void weather_waiting(void)
{
	screens.clearAllScreens();
	screens.setFont(ROBOTO_REGULAR);
	screens.selectScreen(2);
	screens.setFontColor(TFT_WHITE, TFT_BLACK);
	screens.drawString("Getting the weather", 120, 110, 14, Align::MiddleCenter);
	screens.setFontColor(0x8410, TFT_BLACK);
	screens.drawString(settings.owm_key[0] ? settings.weather_city : "no weather key: orbs key", 120, 140, 11, Align::MiddleCenter);
	wx_waiting_shown = true;
}

/* ---- the Messages set (version 4): your own faces, drawn here (the rig not needed) ---- */
static bool messages_any(void)
{
	for(int i = 0; i < N_CUSTOM; i++)
		if(settings.custom[i].kind == CUSTOM_MESSAGE || settings.custom[i].kind == CUSTOM_COUNTDOWN)
			return true;
	return false;
}

/* a message's lines: by its length (or the size picked) one, two or three lines, broken at spaces */
static int message_lines(const char *t, int size, String out[3])
{
	int len = (int)strlen(t);
	int n = size == 3 ? 1 : size == 2 ? 2 : size == 1 ? 3 : len <= 12 ? 1 : len <= 28 ? 2 : 3;
	if(n == 1) { out[0] = t; return 1; }
	int per = (len + n - 1) / n, start = 0, k = 0;
	while(k < n && start < len)
	{
		int end = start + per;
		if(end >= len || k == n - 1) end = len;
		else
		{
			int sp = end;
			while(sp > start && t[sp] != ' ') sp--;        /* (back to a space) */
			if(sp > start) end = sp;
		}
		out[k++] = String(t + start).substring(0, end - start);
		start = end;
		while(start < len && t[start] == ' ') start++;
	}
	return k;
}

static void countdown_text(char *out, size_t n, uint32_t target)
{
	int64_t left = (int64_t)target - orbs_time_now();
	if(left <= 0) { snprintf(out, n, "Now!"); return; }
	long d = (long)(left / 86400), h = (long)(left % 86400 / 3600), m = (long)(left % 3600 / 60), sec = (long)(left % 60);
	if(d > 0) snprintf(out, n, "%ldd %02ld:%02ld", d, h, m);
	else snprintf(out, n, "%02ld:%02ld:%02ld", h, m, sec);
}

static void draw_messages(bool force)
{
	for(int orb = 0; orb < 5; orb++)
	{
		uint8_t idx = settings.set_faces[SET_MESSAGES][orb];
		if(idx == 0xFF)
			continue;                              /* (blank: cleared when the set came up) */
		const orbs_custom_t &cf = settings.custom[idx < N_CUSTOM ? idx : 0];
		if(!force && cf.kind != CUSTOM_COUNTDOWN)
			continue;
		uint16_t fg = rgb888_to_565(cf.color ? cf.color : 0xFFFFFF), bg = rgb888_to_565(cf.bg);
		screens.selectScreen(orb);
		screens.setFont(ROBOTO_REGULAR);
		if(cf.kind != CUSTOM_MESSAGE && cf.kind != CUSTOM_COUNTDOWN)
		{
			if(force)
			{
				screens.fillScreen(TFT_BLACK);
				char h[24];
				snprintf(h, sizeof h, "Message %d", idx + 1);
				screens.drawString(h, 120, 104, 15, Align::MiddleCenter, 0x8410, TFT_BLACK);
				screens.drawString("Add it on the settings page", 120, 132, 11, Align::MiddleCenter, 0x6B4D, TFT_BLACK);
			}
			continue;
		}
		if(force)
		{
			screens.fillScreen(bg);
			if(cf.title[0])
			{
				/* the small line in the face's own colour, a little dimmer (2026-11-17: it took the last orb's colour) */
				uint32_t c8 = cf.color ? cf.color : 0xFFFFFF;
				uint32_t dim = ((c8 >> 16 & 255) * 7 / 10) << 16 | ((c8 >> 8 & 255) * 7 / 10) << 8 | (c8 & 255) * 7 / 10;
				screens.setFontColor(rgb888_to_565(dim), bg);
				screens.drawFittedString(cf.title, 120, 66, 150, 22, Align::MiddleCenter);
			}
		}
		if(cf.kind == CUSTOM_MESSAGE)
		{
			String lines[3];
			int n = message_lines(cf.text, cf.size, lines);
			int h = n == 1 ? 60 : n == 2 ? 34 : 26, y0 = 120 - (n - 1) * h / 2;
			screens.setFontColor(fg, bg);
			for(int i = 0; i < n; i++)
				screens.drawFittedString(lines[i], 120, y0 + i * h, i == 0 && n == 3 ? 150 : 176, h - 4, Align::MiddleCenter);
		}
		else
		{
			char t[24];
			countdown_text(t, sizeof t, cf.target);
			screens.fillRect(28, 92, 184, 50, bg);
			screens.setFontColor(fg, bg);
			screens.drawFittedString(t, 120, 117, 176, 44, Align::MiddleCenter);
			if(force && cf.text[0])
				screens.drawFittedString(cf.text, 120, 166, 150, 22, Align::MiddleCenter);
		}
	}
	screens.flush();
}

static void draw_set(bool force)
{
	step(40, (uint32_t)current_set);
	uint32_t t0 = orbs_millis();
	if(current_set == SET_CLOCK)
	{
		ClockTime t = clock_time_now();
		clockw.update(t, force);
		clockw.draw(force);
	}
	else if(current_set == SET_MATRIX)
		matrixw.draw();                    /* (every loop: it draws every 100 ms by itself) */
	else if(current_set == SET_VIZ)
		vizw.draw(orbs_millis(), force);   /* (every loop: it draws when levels come or peaks fall) */
	else if(current_set == SET_CAMS && !cams_from_rig)
		;                                  /* (Frigate's pictures: drawn by fgcams.poll as they come) */
	else if(set_rig[current_set] >= 0)
		rigpics.draw(orbs_millis());       /* ("Connecting to the rig..." if its pictures don't come) */
	else if(current_set == SET_MESSAGES)
		draw_messages(force);              /* (countdowns every second) */
	else if(current_set == SET_HOURLY || current_set == SET_SKY)
	{
		if(!wxd.valid)
		{
			if(force || !wx_waiting_shown)
				weather_waiting();
		}
		else
		{
			if(wx_waiting_shown)
			{
				wx_waiting_shown = false;
				screens.clearAllScreens();
				force = true;
			}
			orbs_tm_t tm;
			int64_t now_s = orbs_time_now();
			orbs_tz_local(&tz, now_s, &tm);
			bool h24 = clockw.m_format == CLOCK_FORMAT_24_HOUR;
			FaceMap fm;
			if(current_set == SET_HOURLY)
				hourlyw.draw(h24, tm.utc_offset_s, force);
			else
				skyw.draw(h24, tm.utc_offset_s, now_s, force);
		}
	}
	else if(current_set == SET_WEATHER)
	{
		if(!wxd.valid)
		{
			if(force || !wx_waiting_shown)
				weather_waiting();
		}
		else
		{
			if(wx_waiting_shown)
			{
				wx_waiting_shown = false;
				screens.clearAllScreens();
				force = true;
			}
			FaceMap fm;
			weatherw.draw(weather_time_now(), force);
		}
	}
	step(41, (uint32_t)current_set);
	if(screens.flush() > 0)
		frames_sent++;
	step(42, (uint32_t)current_set);
	draw_ms = orbs_millis() - t0;
	if(draw_ms > draw_max_ms)
		draw_max_ms = draw_ms;
	draws++;
}

/* a person picked the set showing now: remembered for the next start-up (saved 20 s after the last pick) */
static void set_picked(void)
{
	picked_set = current_set;
	settings.current_set = (uint8_t)current_set;
	rig_waits = -1;
	pick_save_due_ms = orbs_millis() + 20000;
	if(!pick_save_due_ms)
		pick_save_due_ms = 1;
}

/* the settings in memory always carry the pick (a preset, Defaults or a put-back try must not replace it) */
static void keep_pick(void)
{
	if(picked_set >= 0 && picked_set < N_SETS)
		settings.current_set = (uint8_t)picked_set;
}

static uint8_t target_brightness(void);
static void rule_event(int ev, bool on);
static void set_changed(void)
{
	step(31, (uint32_t)current_set);
	setup_shown = false;                   /* (the screens are cleared below) */
	pics_cancel();                         /* pictures still queued for the old set are skipped */
	pics_settle();                         /* (the second core finishes what it's on before anything is drawn here) */
	step(33, (uint32_t)current_set);
	set_since_ms = orbs_millis();
	{
		uint8_t tb = target_brightness();      /* (a set with its own brightness: drawn at it) */
		if(tb && tb != shown_brightness)
		{
			shown_brightness = tb;
			screens.setBrightness(tb);
		}
	}
	overlay_until = 0;
	screens.setMirror(false);
	matrixw.stop();
	rig.active = -1;
	screens.clearAllScreens();
	step(34, (uint32_t)current_set);
	if(current_set == SET_CLOCK)
		clockw.setup();
	else if(current_set == SET_WEATHER || current_set == SET_HOURLY || current_set == SET_SKY)
	{
		wx_waiting_shown = false;
		weatherw.setup(settings.weather_dark, settings.weather_hl_cycle_s);
	}
	else if(current_set == SET_MATRIX)
	{
		screens.flush();                   /* (the clear goes to all five first) */
		screens.setMirror(true);
		matrixw.m_bigFont = settings.matrix_big_font;
		matrixw.m_textColor = rgb888_to_565(settings.matrix_color);
		matrixw.m_headTextColor = rgb888_to_565(settings.matrix_head_color);
		matrixw.m_lineMin = settings.matrix_line_min;
		matrixw.m_lineMax = settings.matrix_line_max;
		matrixw.m_speedMin = settings.matrix_speed_min;
		matrixw.m_speedMax = settings.matrix_speed_max;
		matrixw.m_updateInterval = settings.matrix_frame_ms;
		matrixw.setup();
	}
	else if(current_set == SET_VIZ)
	{
		rig.active = RIG_SET_VIZ;
		vizw.setup();
	}
	else if(current_set == SET_CAMS && !rig.alive(orbs_millis()))
	{
		cams_from_rig = false;
		fgcams.enter();
	}
	else if(set_rig[current_set] >= 0)
	{
		if(current_set == SET_CAMS)
			cams_from_rig = true;
		rigpics.enter(set_rig[current_set], set_title[current_set]);
	}
	step(35, (uint32_t)current_set);
	if(orbs_dark())
	{
		screens.clearAllScreens();         /* (dark: nothing drawn, the rig told "-" so it sends nothing) */
		screens.flush();
		return;
	}
	if(net_hotspot())
	{
		show_setup();                      /* (the set is drawn when the hotspot closes) */
		return;
	}
	draw_set(true);
	step(36, (uint32_t)current_set);
}

static void apply_brightness(bool redraw);
static void show_qr(void);

static void show_qr(void);
static void apply_brightness(bool redraw);
/* a button's action from the page (settings_json.cpp ACTIONS; 0xC0 + set = show that set): true if done here */
static bool do_button_action(uint8_t act, int bi, int ki)
{
	static const char *const BN[3] = { "left", "middle", "right" }, *const KN[3] = { "short", "medium", "long" };
	const char *rs = set_rig[current_set] >= 0 ? rig_set_name[set_rig[current_set]] : SET_KEYS[current_set];
	if(act >= 0xC0 && act - 0xC0 < N_SETS)
	{
		int k = act - 0xC0;
		if(set_available(k)) { current_set = k; set_changed(); set_picked(); }
		return true;
	}
	switch(act)
	{
	case 1: return true;                                                 /* nothing */
	case 2: current_set = next_set(current_set, -1); set_changed(); set_picked(); return true;
	case 3: current_set = next_set(current_set, +1); set_changed(); set_picked(); return true;
	case 4: show_qr(); return true;
	case 5: manual_dark = !manual_dark; night_wake_until = 0; apply_brightness(true); return true;
	case 6: case 7: case 8: case 9: case 12:                            /* the rig's: it does them */
		printf("EV PRESS set=%s button=%s kind=%s action=%s\n", rs, BN[bi], KN[ki], ACTIONS[act]);
		return true;
	case 10:
		settings.clock_type = settings.clock_type ? 0 : 1;
		load_clock_settings();
		save_settings();
		if(current_set == SET_CLOCK) set_changed();
		return true;
	case 11:
		settings.clock_format = (uint8_t)((settings.clock_format + 1) % 3);
		load_clock_settings();
		save_settings();
		if(current_set == SET_CLOCK) set_changed();
		return true;
	case 13: case 14:
	{
		int nb = settings.brightness + (act == 13 ? 32 : -32);
		settings.brightness = (uint8_t)(nb < 10 ? 10 : nb > 255 ? 255 : nb);
		settings_version++;
		save_due_ms = orbs_millis() + 3000;
		apply_brightness(false);
		return true;
	}
	}
	return false;
}

static void button(int b, ButtonState st)
{
	if(b == BUTTON_LEFT && st == BTN_VERY_LONG)
	{
		printf("EV WIPE left held 15 s: settings, Wi-Fi, key and camera login wiped, restarting\n");
		pics_settle();
		settings_wipe();
		camlogin_wipe();
		watchdog_reboot(0, 0, 100);
		for(;;)
			tight_loop_contents();
	}
	if(!orbs_time_synced())
		return;
	set_since_ms = orbs_millis();              /* (a press holds off the auto-cycle) */
	if(orbs_dark())
	{
		/* any press wakes dark screens: the page's "dark" ends, night-dark lifts for a minute */
		manual_dark = false;
		night_wake_until = orbs_millis() + 60000;
		apply_brightness(true);
		return;
	}
	if(overlay_until)
	{
		overlay_until = 0;                     /* (a press ends the test picture or the QR code) */
		set_changed();
		return;
	}
	if(setup_up())
	{
		printf("EV PRESS ignored: the setup screens stay while the hotspot is open\n");
		return;
	}
	if(b == BUTTON_OK && st == BTN_LONG)
	{
		show_qr();                             /* middle held 2 s on any set: the settings page's address */
		return;
	}
	/* the page's buttons (version 4): this set's action for this button and press, 0 = the set's own as always */
	{
		int bi = b == BUTTON_LEFT ? 0 : b == BUTTON_OK ? 1 : 2;
		int ki = st == BTN_SHORT ? 0 : st == BTN_MEDIUM ? 1 : st == BTN_LONG ? 2 : -1;
		uint8_t act = ki >= 0 && current_set < N_SETS ? settings.buttons[current_set][bi * 3 + ki] : 0;
		if(act && do_button_action(act, bi, ki))
			return;
	}
	if(b == BUTTON_LEFT && st == BTN_SHORT)
	{
		current_set = next_set(current_set, -1);
		set_changed();
		set_picked();
	}
	else if(b == BUTTON_RIGHT && st == BTN_SHORT)
	{
		current_set = next_set(current_set, +1);
		set_changed();
		set_picked();
	}
	else if(current_set == SET_CAMS && !cams_from_rig && b == BUTTON_OK && st == BTN_SHORT)
		fgcams.nextPage();                     /* (the cameras from Frigate: the XL pages them itself) */
	else if(set_rig[current_set] >= 0 && b == BUTTON_OK)
	{
		/* the rig's sets: the rig does the middle button's job (PLAN pick 7: Music play/pause, Rig stats the middle
		   orb's page, Cameras one or five) */
		static const char *const kind[] = { "short", "medium", "long", "very-long" };
		printf("EV PRESS set=%s button=middle kind=%s\n", rig_set_name[set_rig[current_set]],
		       st == BTN_SHORT ? kind[0] : st == BTN_MEDIUM ? kind[1] : st == BTN_LONG ? kind[2] : kind[3]);
	}
	else if(current_set == SET_WEATHER)
	{
		{
			FaceMap fm;                    /* (a short press redraws the three days' screen where it was placed) */
			weatherw.buttonPressed((uint8_t)b, st);
		}
		screens.flush();
		if(weatherw.m_wantUpdate)
		{
			weatherw.m_wantUpdate = false;
			wx_next_ms = 0;                /* middle 0.5-2 s: fetch now */
		}
	}
	else if(current_set == SET_CLOCK)
	{
		clockw.buttonPressed((uint8_t)b, st);
		if(clockw.m_needTime)
		{
			clockw.m_needTime = false;
			clockw.update(clock_time_now(), true);
			clockw.draw(true);
		}
		screens.flush();
		if(clockw.m_changed)
		{
			clockw.m_changed = false;
			settings.clock_type = (uint8_t)clockw.m_type;
			settings.clock_format = (uint8_t)clockw.m_format;
			save_settings();
		}
	}
}

/* ---- USB commands ---- */
static uint32_t refresh_hold_until;       /* (the "X" test) */
/* a picture from the rig comes into one of the second core's buffers (pics.h) */
static uint8_t *pic_buf;
static int pic_slot = -1, pic_orb, pic_set;
static uint32_t pic_len, pic_got, pic_t;
static bool pic_receiving, pic_wait_slot;
static uint32_t pic_answers[4];              /* PK, PN, PF, PB sent */
/* a live-face upload from the rig ("U", pics.h; 2026-11-17): into a second-core buffer, in bulk like a picture */
static uint8_t *lu_buf;
static int lu_buf_id = -1, lu_orb, lu_kind, lu_index, lu_set;
static uint32_t lu_len, lu_got, lu_t;
static bool lu_receiving, lu_wait;
static uint32_t lu_answers[3];               /* LK applied, LF refused, LB dropped */
static bool live_pref[5];                    /* the rig's last word for that orb was a live face, not a picture */
static uint8_t live_shown;                   /* the orbs drawing their live face now */
static bool usb_guard_ok, usb_discard;       /* (usb_poll: a guarded command's code taken; throwing bytes away) */
static uint32_t usb_discard_t, usb_guard_drops;
/* the weather: every 10 minutes (the original's WEATHER_UPDATE_DELAY), a minute after a failure */
static uint32_t wx_ok, wx_fails, wx_last_ok_ms, wx_last_ms;
static char wx_err[96] = "none yet";
static bool wx_fetching;
static int wx_kind;                          /* what's being fetched: 0 the weather, 1 the air quality */
static uint32_t air_next_ms, air_ok, air_fails;
/* one HTTPS answer (the weather: ~20 KB); in PSRAM since 2026-11-17 (SRAM for the live faces) */
static char http_buf[48 * 1024] __uninitialized_psram("httpbuf");
static bool fetch_test;

/* OpenWeatherMap One Call 3.0, as the original asked it (units, language), with the hourly part kept for the Hourly set */
static void weather_path(char *out, size_t n)
{
	snprintf(out, n, "/data/3.0/onecall?lat=%s&lon=%s&appid=%s&units=%s&exclude=minutely,alerts&lang=en", settings.weather_lat,
	         settings.weather_lon, settings.owm_key, settings.weather_units ? "imperial" : "metric");
}
static uint8_t rx[200];
static int rx_n;
static uint32_t rx_t;
static bool wd_restart;

extern volatile uint8_t pics_core1_step;   /* (pics.cpp) */
extern "C" char end, __HeapLimit;           /* (the linker's: where the heap starts and stops) */
/* lwIP's count of mbedTLS's memory (altcp_tls_mbedtls_mem.c, ALTCP_MBEDTLS_PLATFORM_ALLOC_STATS): the same layout */
extern "C" struct tls_mem_stats { size_t allocedBytes, allocCnt, maxBytes, totalBytes; } altcp_mbedtls_malloc_stats;

/* the chip's own temperature sensor (the last ADC input), degrees C */
static float chip_temp_c(void)
{
	adc_select_input(NUM_ADC_CHANNELS - 1);
	uint32_t sum = 0;
	for(int i = 0; i < 16; i++)
		sum += adc_read();
	float v = (float)sum / 16.0f * 3.3f / 4096.0f;
	return 27.0f - (v - 0.706f) / 0.001721f;
}

static void cmd_hello(void)
{
	uint32_t max_was = draw_max_ms;
	orbs_tm_t tm{};
	if(orbs_time_synced())
		orbs_tz_local(&tz, orbs_time_now(), &tm);
	struct mallinfo mi = mallinfo();
	printf("ORBS 2 build=%s board=pico_lipo2xl_w spi_hz=%lu mac=%s up_ms=%lu wd_restart=%d psram=%u wifi=%s ip=%s rssi=%d "
	       "joins=%lu sntp_restarts=%lu ssid=%s key_set=%d time=%d local=%04d-%02d-%02dT%02d:%02d:%02d tz=%s syncs=%lu set=%d draw_ms=%lu "
	       "draws=%lu draw_max_ms=%lu frames_sent=%lu heap_used=%d refreshes=%lu shown=%s weather_ok=%lu weather_fails=%lu weather_age_s=%ld weather_ms=%lu weather_err=\"%s\" air_ok=%lu air_fails=%lu aqi=%d\n",
	       ORBS_BUILD, (unsigned long)lcd_spi_hz(), net_mac(), (unsigned long)orbs_millis(), wd_restart,
	       (unsigned)psram_get_size(), net_state(), net_ip(), net_rssi(), (unsigned long)net_joins(), (unsigned long)net_sntp_restarts(),
	       settings.wifi_ssid[0] ? settings.wifi_ssid : "-", settings.owm_key[0] != 0, orbs_time_synced(), tm.year,
	       tm.mon, tm.mday, tm.hour, tm.min, tm.sec, settings.tz_name, (unsigned long)orbs_time_syncs(), current_set,
	       (unsigned long)draw_ms, (unsigned long)draws, (unsigned long)draw_max_ms, (unsigned long)frames_sent, mi.uordblks,
	       (unsigned long)lcd_refreshes(),
	       orbs_dark() ? "dark" : setup_up() ? "setup" : overlay_until ? (overlay_kind == 1 ? "test" : "qr") :
	       !orbs_time_synced() ? "status" : SET_KEYS[current_set],
	       (unsigned long)wx_ok, (unsigned long)wx_fails, wx_ok ? (long)((orbs_millis() - wx_last_ok_ms) / 1000) : -1L,
	       (unsigned long)wx_last_ms, wx_err, (unsigned long)air_ok, (unsigned long)air_fails, skyx.aqi);
	(void)max_was;
	draw_max_ms = 0;                          /* (the slowest since the last "?") */
	uint32_t dec, fail, avg, mx;
	pics_stats(&dec, &fail, &avg, &mx);
	{
		uint32_t fga = fgcams.lastOkAgeS(orbs_millis());
		printf("FRIGATE state=%s host=%s port=%u user=%s pictures=%lu fails=%lu ok_age_s=%ld last_ms=%u last_lit=%d last_decode=%d err=\"%s\"\n",
		       fgcams.state(), camlogin.host[0] ? camlogin.host : "-", camlogin.port, camlogin.user[0] ? camlogin.user : "-",
		       (unsigned long)fgcams.pictures(), (unsigned long)fgcams.fails(), fga == 0xFFFFFFFFu ? -1L : (long)fga,
		       fgcams.lastMs(), fgcams.lastLit(), fgcams.lastDecode(), fgcams.error());
	}
	printf("NET ntp=%s last_step=%04lx crash=%lu:%08lx:%08lx\n", net_ntp_info(), (unsigned long)boot_step,
	       (unsigned long)boot_crash_core, (unsigned long)boot_crash_pc, (unsigned long)boot_crash_lr);
	printf("CLOCKS sys_mhz=%lu peri_mhz=%lu vreg=%d flash_div=%lu psram_div=%lu psram_rxdelay=%lu lcd_hz=%lu\n",
	       (unsigned long)(clock_get_hz(clk_sys) / 1000000), (unsigned long)(clock_get_hz(clk_peri) / 1000000), (int)vreg_get_voltage(),
	       (unsigned long)((qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB),
	       (unsigned long)((qmi_hw->m[1].timing & QMI_M1_TIMING_CLKDIV_BITS) >> QMI_M1_TIMING_CLKDIV_LSB),
	       (unsigned long)((qmi_hw->m[1].timing & QMI_M1_TIMING_RXDELAY_BITS) >> QMI_M1_TIMING_RXDELAY_LSB), (unsigned long)lcd_spi_hz());
	{
		uint32_t lf, la, lm, lp;
		pics_live_stats(&lf, &la, &lm, &lp);
		uint32_t fb_bytes, fus = pics_live_fetch_us(&fb_bytes);
		printf("LIVE shown=%u frames=%lu avg_us=%lu max_us=%lu avg_px=%lu fetch_us=%lu fetch_kb=%lu lk=%lu lf=%lu lb=%lu "
		       "usb_guard_drops=%lu\n",
		       (unsigned)live_shown, (unsigned long)lf, (unsigned long)la, (unsigned long)lm, (unsigned long)lp,
		       (unsigned long)fus, (unsigned long)(fb_bytes / 1024),
		       (unsigned long)lu_answers[0], (unsigned long)lu_answers[1], (unsigned long)lu_answers[2],
		       (unsigned long)usb_guard_drops);
		uint32_t pr[12];
		pics_live_prof(pr);
		printf("LIVEPROF base=%lu gauge=%lu dot=%lu cells=%lu scroll=%lu tint=%lu span=%lu masks=%lu out=%lu (us)\n",
		       (unsigned long)pr[0], (unsigned long)pr[8], (unsigned long)pr[7], (unsigned long)pr[3], (unsigned long)pr[9],
		       (unsigned long)pr[6], (unsigned long)pr[1], (unsigned long)pr[10], (unsigned long)pr[11]);
	}
	{
		uint32_t bu[5], bp[12], runs = pics_live_bench_result(bu, bp), b1[5], crc[2], s0;
		pics_live_bench_two(b1, crc, &s0);
		printf("BENCH1 one_core_us=%lu,%lu,%lu,%lu,%lu same=%d crc=%08lx strips_by_first_core=%lu two_cores_now=%d\n",
		       (unsigned long)b1[0], (unsigned long)b1[1], (unsigned long)b1[2], (unsigned long)b1[3], (unsigned long)b1[4],
		       crc[0] == crc[1], (unsigned long)crc[1], (unsigned long)s0, (int)pics_help_get());
		printf("BENCH runs=%lu orb_us=%lu,%lu,%lu,%lu,%lu base=%lu gauge=%lu dot=%lu cells=%lu scroll=%lu tint=%lu span=%lu "
		       "sprite=%lu chart=%lu needle=%lu masks=%lu out=%lu fetch=%lu\n", (unsigned long)runs, (unsigned long)bu[0],
		       (unsigned long)bu[1], (unsigned long)bu[2], (unsigned long)bu[3], (unsigned long)bu[4],
		       (unsigned long)bp[0], (unsigned long)bp[8], (unsigned long)bp[7], (unsigned long)bp[3], (unsigned long)bp[9],
		       (unsigned long)bp[6], (unsigned long)bp[1], (unsigned long)bp[2], (unsigned long)bp[4], (unsigned long)bp[5],
		       (unsigned long)bp[10], (unsigned long)bp[11], (unsigned long)pics_live_bench_fetch_us());
	}
	printf("TLSMEM now=%lu peak=%lu of=%u\n", (unsigned long)altcp_mbedtls_malloc_stats.allocedBytes,
	       (unsigned long)altcp_mbedtls_malloc_stats.maxBytes, (unsigned)MEM_SIZE);
	printf("PICS heap_peak=%d heap_limit=%lu clk_mhz=%lu temp_c=%.1f decoded=%lu decode_failed=%lu decode_avg_us=%lu decode_max_us=%lu pk=%lu pn=%lu pf=%lu pb=%lu\n",
	       mi.arena, (unsigned long)(&__HeapLimit - &end), (unsigned long)(clock_get_hz(clk_sys) / 1000000), (double)chip_temp_c(), (unsigned long)dec, (unsigned long)fail,
	       (unsigned long)avg, (unsigned long)mx, (unsigned long)pic_answers[0], (unsigned long)pic_answers[1],
	       (unsigned long)pic_answers[2], (unsigned long)pic_answers[3]);
}

/* how many bytes the command at rx[0] needs, or 0 if not known yet */
static int cmd_len(void)
{
	switch(rx[0])
	{
	case '?': case 'b': return 1;
	case 'W':
		if(rx_n < 2) return 0;
		if(rx_n < 3 + rx[1]) return 0;
		return 3 + rx[1] + rx[2 + rx[1]];
	case 'K':
		if(rx_n < 2) return 0;
		return 2 + rx[1];
	case 'X': return 4;
	case 'F': return 1;
	case 'S': return 2;
	case 'B': return 3;
	case 'E': return 3;
	case 'J': return 1;
	case 'Q': return 1;
	case 'R': return 1;
	case 'H': return 2;
	case 'V': return 64;
	case 'U': return 9;                    /* u8 orb, kind, index, set, u32 len; the bytes then go straight to a buffer */
	case 'Y':                              /* u8 orb, u8 n, n x (u8 slot, u16 value) */
		if(rx_n < 3) return 0;
		return rx[2] > 64 ? 3 : 3 + 3 * rx[2];
	case 'P': return 7;                    /* (the header; the picture itself is read straight into pic_buf) */
	case 'G':
		if(rx_n < 2) return 0;
		return 2 + rx[1];
	case 'L':                              /* u8 hl, host, u16 port, u8 ul, user, u8 pl, pass, 32 bytes pin */
	{
		if(rx_n < 2) return 0;
		int at = 2 + rx[1] + 2;
		if(rx_n < at + 1) return 0;
		at += 1 + rx[at];
		if(rx_n < at + 1) return 0;
		at += 1 + rx[at];
		return at + 32;
	}
	default: return 1;                 /* unknown byte: dropped */
	}
}

static void cmd_run(int n)
{
	step(20, rx[0]);
	if(rx[0] == '?')
		cmd_hello();
	else if(rx[0] == 'b')
		printf("BTN left=%lu middle=%lu right=%lu held=%d%d%d lost=%lu\n", (unsigned long)presses[0], (unsigned long)presses[1],
		       (unsigned long)presses[2], b_state[0], b_state[1], b_state[2], (unsigned long)bq_lost);
	else if(rx[0] == 'W')
	{
		int sl = rx[1], pl = rx[2 + sl];
		if(sl < 1 || sl > 32 || pl > 64)
		{
			printf("WIFI refused (name 1-32 bytes, password up to 64)\n");
			return;
		}
		memset(settings.wifi_ssid, 0, sizeof settings.wifi_ssid);
		memset(settings.wifi_pass, 0, sizeof settings.wifi_pass);
		memcpy(settings.wifi_ssid, rx + 2, sl);
		memcpy(settings.wifi_pass, rx + 3 + sl, pl);
		pics_settle();
		bool ok = settings_save();
		printf("WIFI %s ssid=%s pass_len=%d\n", ok ? "saved" : "NOT-saved", settings.wifi_ssid, pl);
		status_shown[0] = 0;
		net_reconnect();
	}
	else if(rx[0] == 'K')
	{
		int kl = rx[1];
		if(kl < 1 || kl >= (int)sizeof settings.owm_key)
		{
			printf("KEY refused (1-%d bytes)\n", (int)sizeof settings.owm_key - 1);
			return;
		}
		memset(settings.owm_key, 0, sizeof settings.owm_key);
		memcpy(settings.owm_key, rx + 2, kl);
		pics_settle();
		bool ok = settings_save();
		printf("KEY %s len=%d\n", ok ? "saved" : "NOT-saved", kl);
	}
	else if(rx[0] == 'F')
	{
		if(wx_fetching)
			printf("FETCH failed: the weather fetch is running\n");
		else if(!net_up() || !orbs_time_synced() || !settings.owm_key[0])
			printf("FETCH failed: not ready (wifi=%s time=%d key_set=%d)\n", net_state(), orbs_time_synced(), settings.owm_key[0] != 0);
		else
		{
			char path[256];
			weather_path(path, sizeof path);
			if(https_start("api.openweathermap.org", path, http_buf, sizeof http_buf))
				fetch_test = true;
			else
				printf("FETCH failed: one already running\n");
			memset(path, 0, sizeof path);
		}
	}
	else if(rx[0] == 'Q')
	{
		rig.contact_ms = orbs_millis();
		struct mallinfo mi = mallinfo();
		int rs = orbs_dark() || covered() ? -1 : set_rig[current_set];   /* (dark, or a test/QR/setup up: the rig sends nothing) */
		uint32_t fga = fgcams.lastOkAgeS(orbs_millis());
		/* live=<face>.<set> per orb (hex face id, 0 = none): the rig uploads what's missing */
		char lv[64];
		int ln = 0;
		for(int o = 0; o < 5; o++)
		{
			int fs = -1;
			uint16_t fid = pics_live_face(o, &fs);
			ln += snprintf(lv + ln, sizeof lv - (size_t)ln, "%s%x.%d", o ? "," : "", fid, fid ? fs : -1);
		}
		printf("STATUS set=%s need=%u drawn=%lu bad=%lu up=%lu heap=%d viz_frames=%lu fg=%s fg_ok_age=%ld sv=%lu live=%s shown=%u\n",
		       rs >= 0 ? rig_set_name[rs] : "-",
		       (unsigned)rig.need, (unsigned long)rig.drawn, (unsigned long)rig.bad, (unsigned long)(orbs_millis() / 1000),
		       mi.uordblks, (unsigned long)vizw.frames, fgcams.state(), fga == 0xFFFFFFFFu ? -1L : (long)fga, (unsigned long)settings_version,
		       lv, (unsigned)live_shown);
	}
	else if(rx[0] == 'H')
	{
		/* TEST: the setup hotspot opened (1) or closed (0) now, the saved Wi-Fi untouched */
		if(rx[1])
			net_hotspot_test_open();
		else
			net_wifi_saved_test_close();
		printf("HOTSPOT %s\n", rx[1] ? "opening (scanning first)" : "closing");
	}
	else if(rx[0] == 'R')
	{
		/* the rig's part of the settings (the settings page; the rig follows: rig/orbsd.py) */
		static const char *const mid[4] = { "memory", "health", "power", "cooling" };
		char viz[12];
		if(settings.viz_color == VIZ_ALBUM) strcpy(viz, "album");
		else if(settings.viz_color == VIZ_RAINBOW) strcpy(viz, "rainbow");
		else snprintf(viz, sizeof viz, "%06lx", (unsigned long)(settings.viz_color & 0xFFFFFF));
		printf("RIGSET sv=%lu rig_hz=%u middle=%s cams1=%u,%u,%u,%u,%u cams2=%u,%u,%u,%u,%u quality=%u fps=%u viz=%s\n",
		       (unsigned long)settings_version, settings.rig_hz, mid[settings.rig_middle & 3], settings.cams_page[0][0],
		       settings.cams_page[0][1], settings.cams_page[0][2], settings.cams_page[0][3], settings.cams_page[0][4],
		       settings.cams_page[1][0], settings.cams_page[1][1], settings.cams_page[1][2], settings.cams_page[1][3],
		       settings.cams_page[1][4], settings.cam_quality, settings.cam_fps, viz);
	}
	else if(rx[0] == 'U')
	{
		rig.contact_ms = orbs_millis();
		lu_orb = rx[1];
		lu_kind = rx[2];
		lu_index = rx[3];
		lu_set = rx[4];
		lu_len = (uint32_t)rx[5] | (uint32_t)rx[6] << 8 | (uint32_t)rx[7] << 16 | (uint32_t)rx[8] << 24;
		lu_got = 0;
		lu_t = orbs_millis();
		uint32_t max = lu_kind == LIVE_UP_FACE ? LIVE_PACK_MAX : LIVE_SMALL_MAX;
		if(lu_orb > 4 || lu_kind < LIVE_UP_FACE || lu_kind > LIVE_UP_CATALOG || lu_len < 1 || lu_len > max)
		{
			lu_answers[2]++;
			printf("LB\n");                 /* (can't trust where the next one starts: the rig resyncs) */
		}
		else
			lu_wait = true;
	}
	else if(rx[0] == 'Y')
	{
		rig.contact_ms = orbs_millis();
		int orb = rx[1], n = rx[2];
		if(orb <= 4 && n <= 64)
		{
			for(int i = 0; i < n; i++)
				pics_live_slots(orb, rx[3 + 3 * i], (uint16_t)(rx[4 + 3 * i] | rx[5 + 3 * i] << 8));
			live_pref[orb] = true;
		}
	}
	else if(rx[0] == 'V')
	{
		rig.contact_ms = orbs_millis();
		rig.viz_packets++;
		uint16_t h = (uint16_t)(rx[2] | rx[3] << 8);
		if(current_set == SET_VIZ && !orbs_dark() && !covered())
			vizw.levels(rx + 4, (h == 0xFFFF || h >= 3600) ? -1 : h);
	}
	else if(rx[0] == 'P')
	{
		rig.contact_ms = orbs_millis();
		pic_orb = rx[1];
		pic_set = rx[2];
		if(pic_orb <= 4)
			live_pref[pic_orb] = false;          /* (a picture for that orb: its live face steps aside) */
		pic_len = (uint32_t)rx[3] | (uint32_t)rx[4] << 8 | (uint32_t)rx[5] << 16 | (uint32_t)rx[6] << 24;
		pic_got = 0;
		pic_t = orbs_millis();
		if(pic_len < 4 || pic_len > PICS_MAX || pic_orb > 4)
		{
			rig.bad++;
			pic_answers[3]++;
			printf("PB\n");                 /* (can't trust where the next one starts: the rig resyncs) */
		}
		else
			pic_wait_slot = true;            /* (usb_poll takes a free buffer, then the bytes) */
	}
	else if(rx[0] == 'J')
	{
		/* every setting as the page's JSON on one line, for the rig (version 4: faces, styles, colours, warnings, formats,
		   own faces); never the Wi-Fi password or the key (settings_to_json never has them) */
		JsonDocument doc;
		settings_to_json(settings, doc.to<JsonObject>());
		/* (the settings as JSON: ~8.8 KB with the 22 sets of 2026-11-17 night, past the 8 KB this was; 16 KB in PSRAM) */
		static char js[16384] __uninitialized_psram("setjson");
		size_t n = serializeJson(doc, js, sizeof js);
		if(n >= sizeof js - 1)
			printf("SETJSON sv=%lu {\"error\":\"too long\"}\n", (unsigned long)settings_version);
		else
			printf("SETJSON sv=%lu %s\n", (unsigned long)settings_version, js);
	}
	else if(rx[0] == 'E')
	{
		/* an event from the rig (version 4 rules): u8 event, u8 1 started / 0 over */
		rule_event(rx[1], rx[2] != 0);
		printf("EVENT %d %d\n", rx[1], rx[2]);
	}
	else if(rx[0] == 'B')
	{
		/* TEST: a press as if from the button (1 short, 2 medium, 3 long; never 4, the 15 s wipe) */
		static const int btn_of[3] = { BUTTON_LEFT, BUTTON_OK, BUTTON_RIGHT };
		if(rx[1] < 3 && rx[2] >= BTN_SHORT && rx[2] <= BTN_LONG)
		{
			printf("TESTPRESS %d %d\n", rx[1], rx[2]);
			button(btn_of[rx[1]], (ButtonState)rx[2]);
		}
		else
			printf("TESTPRESS refused (button 0-2, kind 1-3)\n");
	}
	else if(rx[0] == 'S')
	{
		if(rx[1] < N_SETS && orbs_time_synced() && set_available(rx[1]))
		{
			current_set = rx[1];
			set_changed();
			set_picked();
			printf("SET %d\n", current_set);
		}
		else
			printf("SET refused (0-%d, once the time is known, a rig set only while the rig is there)\n", N_SETS - 1);
	}
	else if(rx[0] == 'G')
	{
		static char host[64];
		int hl = rx[1] < (int)sizeof host - 1 ? rx[1] : (int)sizeof host - 1;
		memcpy(host, rx + 2, hl);
		host[hl] = 0;
		if(wx_fetching || !net_up() || !orbs_time_synced())
			printf("FETCH failed: not ready or busy\n");
		else if(https_start(host, "/", http_buf, sizeof http_buf))
			fetch_test = true;
		else
			printf("FETCH failed: one already running\n");
	}
	else if(rx[0] == 'L')
	{
		int hl = rx[1], at = 2 + hl;
		int port = rx[at] | rx[at + 1] << 8;
		int ul = rx[at + 2];
		const uint8_t *user = rx + at + 3;
		int pl = rx[at + 3 + ul];
		const uint8_t *pass = rx + at + 4 + ul;
		const uint8_t *pin = pass + pl;
		pics_settle();
		if(hl == 0)
		{
			camlogin_wipe();
			fgcams.forgetToken();
			printf("LOGIN wiped\n");
		}
		else if(hl >= (int)sizeof camlogin.host || ul < 1 || ul >= (int)sizeof camlogin.user || pl < 1 || pl >= (int)sizeof camlogin.pass || !port)
			printf("LOGIN refused (host 1-39, user 1-31, password 1-63 bytes, a port)\n");
		else
		{
			memset(&camlogin, 0, sizeof camlogin);
			memcpy(camlogin.host, rx + 2, hl);
			camlogin.port = (uint16_t)port;
			memcpy(camlogin.user, user, ul);
			memcpy(camlogin.pass, pass, pl);
			memcpy(camlogin.pin, pin, 32);
			bool ok = camlogin_save();
			fgcams.forgetToken();
			printf("LOGIN %s host=%s port=%u user=%s pass_len=%d\n", ok ? "saved" : "NOT-saved", camlogin.host, camlogin.port,
			       camlogin.user, pl);
		}
	}
	else if(rx[0] == 'X')
	{
		int orb = rx[1], secs = rx[2] | rx[3] << 8;
		lcd_test_invoff(orb);
		refresh_hold_until = orbs_millis() + (uint32_t)secs * 1000u;
		printf("TEST invoff orb=%d refresh_held_s=%d\n", orb, secs);
	}
	(void)n;
}

/* the page's orb pictures on the rig's sets: the last JPEG each orb got (copied only while a page is looking). Two
   copies per orb, and a new picture goes into the one not being sent (2026-11-17: a picture that came in while
   the page was still being sent the last one overwrote it half way, so the page's orb showed its bottom garbled; the
   screens decode their own copy and never were) */
static uint8_t orb_jpeg[2][5][PICS_MAX] __uninitialized_psram("orbjpg");
static uint32_t orb_jpeg_len[2][5];
static int orb_jpeg_set[2][5] = { { -1, -1, -1, -1, -1 }, { -1, -1, -1, -1, -1 } };   /* the set each copy is from */
static uint32_t orb_jpeg_at[2][5];            /* when each copy was kept */
static uint32_t orb_look_start;               /* when the page started looking this time (older copies may be stale) */
static uint8_t orb_jpeg_new[5];               /* which of the two copies is the newest */
static void keep_orb_jpeg(int orb, const uint8_t *jpg, uint32_t len)
{
	if((int32_t)(orbs_millis() - orb_pics_wanted_until) >= 0 || orb < 0 || orb > 4 || len > PICS_MAX)
		return;
	int k = orb_jpeg_new[orb] ^ 1;
	if(web_sending(orb_jpeg[k][orb], PICS_MAX))
		return;                               /* (both copies still being sent: this picture skipped, the next taken) */
	memcpy(orb_jpeg[k][orb], jpg, len);
	orb_jpeg_len[k][orb] = len;
	orb_jpeg_set[k][orb] = current_set;
	orb_jpeg_at[k][orb] = orbs_millis();
	orb_jpeg_new[orb] = (uint8_t)k;
}

/* a whole picture is in: on to the second core if its set is showing */
static void picture_done(void)
{
	pic_receiving = false;
	if(set_rig[current_set] == pic_set && pic_set != RIG_SET_VIZ && !(current_set == SET_CAMS && !cams_from_rig) &&
	   !orbs_dark() && !covered())
	{
		keep_orb_jpeg(pic_orb, pic_buf, pic_len);
		rigpics.beforeSubmit();
		rig.submitted++;
		pics_submit(pic_slot, pic_orb, pic_set, pic_len, screens.getBrightness());
	}
	else
	{
		pics_give_back(pic_slot);
		pic_answers[1]++;
		printf("PN %d\n", pic_orb);
	}
	pic_slot = -1;
}

/* the second core's answers: "PK" once a picture is decoded and on its way to the glass */
static void pics_answers(void)
{
	pics_result_t r;
	while(pics_result(&r))
	{
		if(r.live_kind)
		{
			/* a live upload applied ("LK orb kind face") or refused ("LF orb kind why") */
			if(r.ok)
			{
				lu_answers[0]++;
				printf("LK %d %d %x\n", r.orb, r.live_kind, r.live_face);
			}
			else
			{
				lu_answers[1]++;
				printf("LF %d %d %s\n", r.orb, r.live_kind, r.live_err ? r.live_err : "?");
			}
			continue;
		}
		if(r.ok)
		{
			rig.drawn++;
			rig.need &= (uint8_t)~(1u << r.orb);
			frames_sent++;
			pic_answers[0]++;
			printf("PK %d\n", r.orb);
		}
		else if(r.skipped)
		{
			pic_answers[1]++;
			printf("PN %d\n", r.orb);
		}
		else
		{
			rig.bad++;
			pic_answers[2]++;
			printf("PF %d\n", r.orb);
		}
	}
}

/* waits until the second core is idle (its answers keep going out meanwhile, so it never waits on us) */
static void pics_settle_keep_live(bool keep_live);
static void pics_settle(void) { pics_settle_keep_live(false); }
static void pics_settle_keep_live(bool keep_live)
{
	if(!keep_live)
	{
		live_shown = 0;                      /* (the first core is about to draw: the live faces stop first) */
		pics_live_show(0, screens.getBrightness());
	}
	step(30);
	uint32_t t0 = orbs_millis();
	while(!pics_idle())
	{
		pics_answers();
		if(orbs_millis() - t0 > 2000)
		{
			step(37);                          /* (the picture core never finished: said, and found by the watchdog) */
			printf("EV STUCK waiting 2 s for the picture core (pics_settle)\n");
			t0 = orbs_millis();
		}
	}
	pics_answers();
}

static void save_settings(void)
{
	step(32);
	pics_settle_keep_live(true);             /* (no picture may be on its way from PSRAM while flash is written; the live
	                                            faces keep going: the flash write pauses the second core itself) */
	keep_pick();
	if(settings_save())
		saved_pick = settings.current_set;
}

/* ---- rules (version 4) ---- */
static void rule_show(int i, int set)
{
	if(rule_active < 0)
		rule_back_set = current_set;               /* (where to go back to) */
	rule_active = i;
	memcpy(&rule_shown, &settings.rules[i], sizeof rule_shown);
	if(set == 0xFE)
	{
		rule_dark = true;
		manual_dark = true;
		apply_brightness(true);
	}
	else
	{
		if(rule_dark)
		{                                          /* (a dark rule handing over to one with a set: the screens come on,
			                                          2026-11-21; before, they stayed dark until that rule ended) */
			rule_dark = false;
			manual_dark = false;
			apply_brightness(true);
		}
		if(set < N_SETS && set_available(set) && set != current_set)
		{
			current_set = set;
			set_changed();
		}
	}
	printf("EV RULE %d on: %s\n", i, set == 0xFE ? "dark" : set < N_SETS ? SET_KEYS[set] : "?");
}

static void rule_end(void)
{
	if(rule_active < 0)
		return;
	printf("EV RULE %d over\n", rule_active);
	rule_active = -1;
	rule_until = 0;
	if(rule_dark)
	{
		rule_dark = false;
		manual_dark = false;
		apply_brightness(true);
	}
	if(rig_waits >= 0 && set_available(rig_waits))
		;                                      /* (a rig set waiting for the feed, which is back: the main loop goes straight
		                                          there, no stop on the set the rule came from) */
	else if(rule_back_set >= 0 && rule_back_set != current_set && set_available(rule_back_set))
	{
		current_set = rule_back_set;
		set_changed();
	}
	else if(rule_back_set >= 0 && rule_back_set != current_set && rig_waits < 0 && set_rig[rule_back_set] >= 0 &&
	        (settings.set_enabled >> rule_back_set & 1) && !rig.alive(orbs_millis()))
	{
		rig_waits = rule_back_set;             /* (a rig set and the rig's feed stopped meanwhile: back to it when it comes) */
		rig_waits_start = false;
		printf("EV RULE back to %s when the rig's feed is\n", SET_KEYS[rule_back_set]);
	}
	rule_back_set = -1;
}

/* an event from the rig (or the rig gone: rig_sleep): its first rule shows its set, for its seconds or until it's over */
static void rule_event(int ev, bool on)
{
	for(int i = 0; i < N_RULES; i++)
	{
		const orbs_rule_t &r = settings.rules[i];
		if(r.kind != RULE_EVENT || r.event != ev)
			continue;
		if(on)
		{
			rule_show(i, r.set);
			rule_until = r.secs ? orbs_millis() + r.secs * 1000u : 0;
			if(r.secs && !rule_until) rule_until = 1;   /* (a timer that lands on 0 would never end it) */
		}
		else if(rule_active == i && !settings.rules[i].secs)
			rule_end();
		return;
	}
}

/* the time rules, every 15 s: the first whose days and window hold now; its set while it lasts */
static void rules_poll(uint32_t now)
{
	static uint32_t next;
	/* the rule showing changed or removed (the page, a preset, Defaults, a try put back): looked at again now. Before
	   2026-11-21 it was left on: a removed time rule's set stayed until a restart, and an event rule removed or moved
	   to another place in the list never ended. A changed time rule that still holds shows again (its new set); one
	   that no longer does, or a changed event rule, ends (an event rule comes on again at its next event). */
	static bool stale;
	if(rule_active < 0)
		stale = false;
	else if(!stale && memcmp(&settings.rules[rule_active], &rule_shown, sizeof rule_shown))
	{
		stale = true;
		next = now;
		printf("EV RULE %d changed or removed: looked at again\n", rule_active);
	}
	if(stale && !orbs_time_synced())
	{
		stale = false;                         /* (no clock to look again by: over) */
		rule_end();
	}
	if(rule_active >= 0 && !stale && settings.rules[rule_active].kind == RULE_EVENT && rule_until && (int32_t)(now - rule_until) >= 0)
		rule_end();
	if((int32_t)(now - next) < 0 || !orbs_time_synced())
		return;
	next = now + 15000;
	orbs_tm_t tm;
	orbs_tz_local(&tz, orbs_time_now(), &tm);
	int m = tm.hour * 60 + tm.min, wd = tm.wday;
	int hit = -1;
	for(int i = 0; i < N_RULES && hit < 0; i++)
	{
		const orbs_rule_t &r = settings.rules[i];
		if(r.kind != RULE_TIME || !(r.days >> wd & 1))
			continue;
		bool in = r.start_min <= r.end_min ? (m >= r.start_min && m < r.end_min) : (m >= r.start_min || m < r.end_min);
		if(in) hit = i;
	}
	bool time_rule_on = rule_active >= 0 && (stale || settings.rules[rule_active].kind == RULE_TIME);
	if(hit >= 0 && (rule_active != hit || stale) && (rule_active < 0 || time_rule_on))
		rule_show(hit, settings.rules[hit].set);   /* (a stale rule handing over: its way back is kept) */
	else if(hit < 0 && time_rule_on)
		rule_end();
	stale = false;
	/* the rig gone (its feed stopped: asleep or off): a rig_sleep rule acts as its event */
	static bool rig_was = true;
	bool rig_now = rig.alive(now);
	if(rig_now != rig_was)
	{
		rig_was = rig_now;
		rule_event(EVENT_RIG_SLEEP, !rig_now);
	}
}

/* the rig's catalogue: its built sets (they join the page and the rotation) and each set's faces, as JSON */
static void catalog_take(const char *json, uint32_t len)
{
	JsonDocument doc;
	if(len >= sizeof catalog_json || deserializeJson(doc, json, len))
	{
		printf("LF 0 7 bad catalog\n");
		return;
	}
	uint32_t built = 0;
	for(JsonPairConst kv : doc["sets"].as<JsonObjectConst>())
	{
		int k = set_key_index_any(kv.key().c_str());
		if(k >= 10 && k < N_SETS)
			built |= 1u << k;
	}
	memcpy(catalog_json, json, len);
	catalog_json[len] = 0;
	catalog_at = orbs_millis();
	if(built != sets_from_rig)
	{
		sets_from_rig = built;
		settings_version++;
	}
	/* a set the rig names for the first time joins the rotation (switched on once; the page's off is kept after) */
	uint32_t seen = settings.sets_seen ? settings.sets_seen : (((1u << 10) - 1) | SETS_NEW_ON);
	uint32_t fresh = built & ~seen;
	if(fresh)
	{
		settings.set_enabled |= fresh;
		settings.sets_seen = seen | fresh;
		save_settings();
		settings_version++;
		printf("EV SETS new from the rig, switched on: %lx\n", (unsigned long)fresh);
	}
	printf("LK 0 7 %lx\n", (unsigned long)built);
}

/* which orbs draw their live face (pics.h): its set on screen with the rig there, nothing over it, the rig's last word
   for that orb a live face, and its face loaded for that set */
static void live_update(uint32_t now)
{
	uint8_t mask = 0;
	int rs = set_rig[current_set];
	bool on = rs >= 0 && current_set != SET_VIZ && !(current_set == SET_CAMS && !cams_from_rig) && !orbs_dark() &&
	          !covered() && orbs_time_synced() && rig.alive(now);
	if(on)
		for(int o = 0; o < 5; o++)
		{
			int fs = -1;
			if(live_pref[o] && pics_live_face(o, &fs) && fs == rs)
				mask |= (uint8_t)(1u << o);
		}
	if(mask & ~live_shown)
	{
		rigpics.beforeSubmit();              /* ("Connecting to the rig..." goes, as for a first picture) */
		rig.submitted++;
	}
	live_shown = mask;
	pics_live_show(mask, screens.getBrightness());
	pics_live_fb((int32_t)(now - orb_pics_wanted_until) < 0);
	rig.need &= (uint8_t)~mask;              /* (those orbs want no pictures) */
}

static void usb_poll(void)
{
	if(lu_wait)
	{
		lu_buf = pics_live_buffer(lu_kind, lu_len, &lu_buf_id);
		if(!lu_buf)
		{
			if(orbs_millis() - lu_t > 2000)
			{
				lu_wait = false;             /* (no buffer for 2 s: the rig's bytes can't wait; dropped, it resyncs) */
				lu_answers[2]++;
				usb_discard = true;
				usb_discard_t = orbs_millis();
				printf("LB\n");
			}
			return;
		}
		lu_wait = false;
		lu_receiving = true;
		lu_t = orbs_millis();
	}
	if(lu_receiving)
	{
		uint32_t until = orbs_millis() + 20;
		while(lu_got < lu_len)
		{
			int n = stdio_get_until((char *)lu_buf + lu_got, (int)(lu_len - lu_got), make_timeout_time_us(2000));
			if(n > 0)
			{
				lu_got += (uint32_t)n;
				lu_t = orbs_millis();
			}
			if((int32_t)(orbs_millis() - until) >= 0 || n <= 0)
				break;
		}
		if(lu_got >= lu_len)
		{
			lu_receiving = false;
			if(lu_kind == LIVE_UP_CATALOG)
			{
				catalog_take((const char *)lu_buf, lu_len);
				pics_live_give_back(lu_kind, lu_buf_id);
			}
			else
			{
				live_pref[lu_orb] = true;
				pics_live_upload(lu_buf_id, lu_orb, lu_kind, lu_index, lu_len, lu_set);
			}
			lu_buf_id = -1;
		}
		else if(orbs_millis() - lu_t > 1000)
		{
			lu_receiving = false;            /* half an upload and then nothing for 1 s: dropped */
			pics_live_give_back(lu_kind, lu_buf_id);
			lu_buf_id = -1;
			lu_answers[2]++;
			usb_discard = true;
			usb_discard_t = orbs_millis();
			printf("LB\n");
		}
		return;
	}
	if(pic_wait_slot)
	{
		pic_buf = pics_take(&pic_slot);
		if(!pic_buf)
			return;                          /* all three busy: the bytes wait in USB until one is free */
		pic_wait_slot = false;
		pic_receiving = true;
		pic_t = orbs_millis();
	}
	if(pic_receiving)
	{
		/* the picture's bytes in bulk: up to 20 ms here, so the buttons and the rest still get their turn */
		uint32_t until = orbs_millis() + 20;
		while(pic_got < pic_len)
		{
			int n = stdio_get_until((char *)pic_buf + pic_got, (int)(pic_len - pic_got), make_timeout_time_us(2000));
			if(n > 0)
			{
				pic_got += (uint32_t)n;
				pic_t = orbs_millis();
			}
			else if(orbs_millis() - pic_t > 1000)
				break;
			if((int32_t)(orbs_millis() - until) >= 0 || n <= 0)
				break;
		}
		if(pic_got >= pic_len)
			picture_done();
		else if(orbs_millis() - pic_t > 1000)
		{
			pic_receiving = false;           /* half a picture and then nothing for 1 s: dropped */
			pics_give_back(pic_slot);
			pic_slot = -1;
			rig.bad++;
			pic_answers[3]++;
			usb_discard = true;
			usb_discard_t = orbs_millis();
			printf("PB\n");
		}
		return;
	}
	/* after a dropped transfer: whatever is still coming is thrown away until the line has been quiet for 100 ms, so
	   the rest of it can't be read as commands (2026-11-17: a face pack's bytes were, see below) */
	if(usb_discard)
	{
		int c;
		while((c = getchar_timeout_us(0)) >= 0)
			usb_discard_t = orbs_millis();
		if(orbs_millis() - usb_discard_t > 100)
			usb_discard = false;
		return;
	}
	for(;;)
	{
		int c = getchar_timeout_us(0);
		if(c < 0)
			break;
		if(rx_n == 0)
			rx_t = orbs_millis();
		if(rx_n < (int)sizeof rx)
			rx[rx_n++] = (uint8_t)c;
		/* commands that change something (Wi-Fi, key, camera login, hotspot, set, test presses and tests) carry two
		   code bytes after their letter (A5 5A): stray bytes can't make one by chance (2026-11-17: a face
		   pack read as commands opened the hotspot, restarted the XL and wiped the camera login) */
		if(!usb_guard_ok && rx_n <= 3 && rx[0] && strchr("WKLHXBSFGE", rx[0]))   /* (strchr finds a 0 byte too) */
		{
			bool bad = (rx_n >= 2 && rx[1] != 0xA5) || (rx_n >= 3 && rx[2] != 0x5A);
			if(bad)
			{
				usb_guard_drops++;
				memset(rx, 0, sizeof rx);
				rx_n = 0;
				continue;
			}
			if(rx_n < 3)
				continue;
			usb_guard_ok = true;
			rx_n = 1;                          /* (the code bytes off: the command as it always was) */
		}
		int need = cmd_len();
		if(need > (int)sizeof rx)
		{
			rx_n = 0;
			continue;
		}
		if(need && rx_n >= need)
		{
			cmd_run(need);
			memset(rx, 0, sizeof rx);          /* (a password doesn't stay in the buffer) */
			rx_n = 0;
			usb_guard_ok = false;
			if(pic_wait_slot || lu_wait)
				return;                        /* (the picture's or upload's bytes come next: read in bulk above) */
		}
	}
	if(rx_n && orbs_millis() - rx_t > 1000)    /* half a command, nothing more for 1 s: dropped */
	{
		memset(rx, 0, sizeof rx);
		rx_n = 0;
		usb_guard_ok = false;
	}
}

/* ===================== the settings page (web.cpp asks; 2026-11-17) ===================== */
static bool in_night(void)
{
	if(settings.night_mode == 0 || !orbs_time_synced())
		return false;
	orbs_tm_t tm;
	orbs_tz_local(&tz, orbs_time_now(), &tm);
	int m = tm.hour * 60 + tm.min, a = settings.night_start_min, b = settings.night_end_min;
	return a <= b ? (m >= a && m < b) : (m >= a || m < b);        /* (22:00-07:00 runs over midnight) */
}

static bool orbs_dark(void)
{
	return manual_dark || (in_night() && settings.night_mode == 2 && (int32_t)(orbs_millis() - night_wake_until) >= 0);
}

/* the brightness for now (day, night dimmer, or dark); redrawn if it changed */
/* the auto-cycle's turn for a set: its own seconds, else the global ones (0 = it stays) */
static uint16_t cycle_for(int s)
{
	uint16_t own = s >= 0 && s < N_SETS ? settings.set_cycle_s[s] : 0;
	return own ? own : settings.cycle_s;
}

/* the brightness now: dark, night-dimmed, or the set's own (the page's per-set brightness) else the global one */
static uint8_t target_brightness(void)
{
	if(orbs_dark())
		return 0;
	if(in_night() && settings.night_mode == 1)
		return settings.night_brightness;
	uint8_t sb = current_set < N_SETS ? settings.set_brightness[current_set] : 0;
	return sb ? sb : settings.brightness;
}

static void apply_brightness(bool redraw)
{
	uint8_t b = target_brightness();
	if(b == shown_brightness && !redraw)
		return;
	bool was_dark = shown_brightness == 0;
	shown_brightness = b;
	screens.setBrightness(b ? b : 255);
	(void)was_dark;
	if(orbs_time_synced())
		set_changed();                                 /* (everything redrawn at the new brightness) */
}

/* the page's test picture: the orb's name over colour bars, 5 s */
static void show_test(int orb)
{
	pics_cancel();
	pics_settle();
	screens.setMirror(false);
	screens.selectScreen(orb);
	screens.fillScreen(TFT_BLACK);
	static const uint16_t bars[6] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0xFFE0, 0xF81F };   /* red, green, blue, white, yellow, magenta */
	for(int i = 0; i < 6; i++)
		screens.fillRect(30 + i * 30, 120, 30, 70, bars[i]);
	screens.setFont(ROBOTO_REGULAR);
	char name[8];
	snprintf(name, sizeof name, "D%d", orb + 1);
	screens.drawString(name, 120, 80, 40, Align::MiddleCenter, TFT_WHITE, TFT_BLACK);
	screens.reset();
	screens.flush();
	overlay_kind = 1;
	overlay_until = orbs_millis() + 5000;
}

/* a QR code of `text` on one orb, on a white square (the light border a reader needs); false if it doesn't fit */
static bool draw_qr(int orb, const char *text, int *top, int *bottom)
{
	static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(5)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
	if(!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 5, qrcodegen_Mask_AUTO, true))
		return false;
	screens.selectScreen(orb);
	int n = qrcodegen_getSize(qr), px = 150 / (n + 4), side = px * (n + 4), x0 = 120 - side / 2, y0 = 120 - side / 2;
	screens.fillRect(x0, y0, side, side, TFT_WHITE);
	for(int y = 0; y < n; y++)
		for(int x = 0; x < n; x++)
			if(qrcodegen_getModule(qr, x, y))
				screens.fillRect(x0 + (x + 2) * px, y0 + (y + 2) * px, px, px, TFT_BLACK);
	*top = y0;
	*bottom = y0 + side;
	return true;
}

/* the setup hotspot is open: how to join it and where the page is, on all five orbs, until it closes */
static void show_setup(void)
{
	pics_cancel();
	pics_settle();
	screens.setMirror(false);
	screens.clearAllScreens();
	screens.setFont(ROBOTO_REGULAR);
	char wifi[96];
	snprintf(wifi, sizeof wifi, "WIFI:T:WPA;S:%s;P:%s;;", HOTSPOT_SSID, hotspot_password());
	int t, b;
	screens.selectScreen(0);
	screens.drawString("Wi-Fi setup", 120, 92, 20, Align::MiddleCenter, TFT_WHITE, TFT_BLACK);
	screens.drawString("The orbs can't join", 120, 128, 12, Align::MiddleCenter, 0x8410, TFT_BLACK);
	screens.drawString(settings.wifi_ssid[0] ? settings.wifi_ssid : "(no Wi-Fi set yet)", 120, 150, 13, Align::MiddleCenter, 0xAD55, TFT_BLACK);
	if(draw_qr(1, wifi, &t, &b))
	{
		screens.drawString("1. Scan to join", 120, t - 14, 13, Align::MiddleCenter, TFT_WHITE, TFT_BLACK);
		memset(wifi, 0, sizeof wifi);
	}
	screens.selectScreen(2);
	screens.drawString("or join this Wi-Fi", 120, 62, 12, Align::MiddleCenter, 0x8410, TFT_BLACK);
	screens.drawString(HOTSPOT_SSID, 120, 92, 16, Align::MiddleCenter, TFT_WHITE, TFT_BLACK);
	screens.drawString("password", 120, 132, 12, Align::MiddleCenter, 0x8410, TFT_BLACK);
	screens.drawString(hotspot_password(), 120, 162, 22, Align::MiddleCenter, 0x4FE0, TFT_BLACK);
	if(draw_qr(3, "http://192.168.4.1/", &t, &b))
	{
		screens.drawString("2. Then open", 120, t - 14, 13, Align::MiddleCenter, TFT_WHITE, TFT_BLACK);
		screens.drawString("192.168.4.1", 120, b + 14, 12, Align::MiddleCenter, 0xAD55, TFT_BLACK);
	}
	screens.selectScreen(4);
	screens.drawString("3. Pick your Wi-Fi", 120, 98, 15, Align::MiddleCenter, TFT_WHITE, TFT_BLACK);
	screens.drawString("and type its password", 120, 124, 12, Align::MiddleCenter, 0x8410, TFT_BLACK);
	screens.drawString("(an iPhone opens the page", 120, 152, 11, Align::MiddleCenter, 0x6B4D, TFT_BLACK);
	screens.drawString("by itself)", 120, 170, 11, Align::MiddleCenter, 0x6B4D, TFT_BLACK);
	screens.reset();
	screens.flush();
	setup_shown = true;
	overlay_kind = 3;
	overlay_until = 0;                                   /* (no timer: they go when the hotspot closes) */
}

/* middle held 2 s: the settings page's address as a QR code on the middle orb, 30 s (a press ends it); after the setup
   hotspot, "On <network>" with it for 20 s */
static void show_qr_titled(const char *title, uint32_t ms);
static void show_qr(void)
{
	if(net_hotspot())
	{
		show_setup();                              /* (no home Wi-Fi: how to set it up instead) */
		return;
	}
	show_qr_titled("Settings", 30000);
}

static void show_qr_titled(const char *title, uint32_t ms)
{
	char url[40];
	const char *ip = net_ip();
	snprintf(url, sizeof url, "http://%s/", ip && ip[0] && ip[0] != '-' ? ip : "info-orbs.local");
	static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(4)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
	if(!qrcodegen_encodeText(url, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 4, qrcodegen_Mask_AUTO, true))
		return;
	pics_cancel();
	pics_settle();
	screens.setMirror(false);
	screens.clearAllScreens();
	screens.selectScreen(2);
	int n = qrcodegen_getSize(qr), px = 150 / (n + 4), side = px * (n + 4), x0 = 120 - side / 2, y0 = 120 - side / 2;
	screens.fillRect(x0, y0, side, side, TFT_WHITE);           /* (the light border a QR reader needs) */
	for(int y = 0; y < n; y++)
		for(int x = 0; x < n; x++)
			if(qrcodegen_getModule(qr, x, y))
				screens.fillRect(x0 + (x + 2) * px, y0 + (y + 2) * px, px, px, TFT_BLACK);
	screens.setFont(ROBOTO_REGULAR);
	screens.drawString(title, 120, y0 - 14, 14, Align::MiddleCenter, TFT_WHITE, TFT_BLACK);
	screens.drawString(url + 7, 120, y0 + side + 14, 12, Align::MiddleCenter, 0xAD55, TFT_BLACK);
	for(int o = 0; o < 5; o++)
		if(o != 2)
		{
			screens.selectScreen(o);
			screens.drawString(o < 2 ? "Scan the middle orb" : "info-orbs.local", 120, 120, 13, Align::MiddleCenter, 0x8410, TFT_BLACK);
		}
	screens.reset();
	screens.flush();
	overlay_kind = 2;
	overlay_until = orbs_millis() + ms;
	printf("EV QR %s\n", url);
}

void web_status(JsonObject st)
{
	struct mallinfo mi = mallinfo();
	st["build"] = ORBS_BUILD;
	st["up_s"] = orbs_millis() / 1000;
	st["temp_c"] = (int)(chip_temp_c() * 10) / 10.0;
	st["clk_mhz"] = clock_get_hz(clk_sys) / 1000000;
	st["heap_kb"] = mi.uordblks / 1024;
	char t[12] = "–";
	if(orbs_time_synced())
	{
		orbs_tm_t tm;
		orbs_tz_local(&tz, orbs_time_now(), &tm);
		snprintf(t, sizeof t, "%d:%02d %s", tm.hour % 12 ? tm.hour % 12 : 12, tm.min, tm.hour >= 12 ? "PM" : "AM");
	}
	st["time"] = t;
	st["showing"] = SET_KEYS[current_set];
	st["dark"] = orbs_dark();
	JsonObject tr = st["try"].to<JsonObject>();
	tr["active"] = try_active;
	if(try_active)
		tr["left_s"] = (int32_t)(try_until - orbs_millis()) > 0 ? (try_until - orbs_millis()) / 1000 : 0;
	if(rule_active >= 0)
		st["rule"] = rule_active;
	if(catalog_json[0])
		st["catalog"] = serialized((const char *)catalog_json);
	JsonObject w = st["wifi"].to<JsonObject>();
	w["ssid"] = net_up() ? settings.wifi_ssid : "";
	w["ip"] = net_up() ? net_ip() : "";
	w["rssi"] = net_rssi();
	w["mac"] = net_mac();
	w["host"] = "info-orbs";
	st["rig"]["connected"] = rig.alive(orbs_millis());
	st["frigate"]["state"] = fgcams.state();
	st["frigate"]["error"] = strcmp(fgcams.error(), "none") ? fgcams.error() : "";
	JsonObject we = st["weather"].to<JsonObject>();
	we["ok"] = wx_ok > 0;
	if(wx_ok)
		we["age_s"] = (orbs_millis() - wx_last_ok_ms) / 1000;
	we["error"] = strcmp(wx_err, "none") ? wx_err : "";
	JsonObject lr = st["last_restart"].to<JsonObject>();
	lr["watchdog"] = wd_restart;
	char b[24];
	snprintf(b, sizeof b, "%04lx", (unsigned long)boot_step);
	lr["step"] = b;
	snprintf(b, sizeof b, "%lu:%08lx", (unsigned long)boot_crash_core, (unsigned long)boot_crash_pc);
	lr["crash"] = boot_crash_core ? b : "0";
}

bool web_apply(const orbs_settings_t &next)
{
	orbs_settings_t old = settings;
	settings = next;                                   /* (the page never carries the Wi-Fi password or the key) */
	bool redraw = false;
	auto changed = [&](const void *a, const void *b, size_t n) { return memcmp(a, b, n) != 0; };
	if(changed(&old.clock_type, &next.clock_type, 4) || old.clock_color != next.clock_color || old.clock_shadow_color != next.clock_shadow_color ||
	   old.clock_nixie_color != next.clock_nixie_color || strcmp(old.tz_name, next.tz_name))
	{
		load_clock_settings();
		redraw |= current_set == SET_CLOCK || current_set == SET_WEATHER || current_set == SET_HOURLY || current_set == SET_SKY;
	}
	if(strcmp(old.ntp_server, next.ntp_server))
		net_reconnect_time();
	if(strcmp(old.weather_lat, next.weather_lat) || strcmp(old.weather_lon, next.weather_lon) || old.weather_units != next.weather_units ||
	   (!old.weather_enabled && next.weather_enabled))
	{
		wx_next_ms = orbs_millis();                    /* (a new place or units: fetched now) */
		air_next_ms = orbs_millis();
	}
	if(strcmp(old.weather_city, next.weather_city) || old.weather_dark != next.weather_dark || old.weather_hl_cycle_s != next.weather_hl_cycle_s)
		redraw |= current_set == SET_WEATHER || current_set == SET_HOURLY || current_set == SET_SKY;
	if(changed(&old.matrix_big_font, &next.matrix_big_font, 8) || old.matrix_color != next.matrix_color || old.matrix_head_color != next.matrix_head_color)
		redraw |= current_set == SET_MATRIX;
	for(int o = 0; o < 5; o++)
		if(old.rotation[o] != next.rotation[o])
		{
			lcd_set_rotation(o, next.rotation[o]);
			redraw = true;
		}
	if(old.link_speed != next.link_speed)
	{
		lcd_set_link(next.link_speed);                 /* (the screens' picture speed: everything drawn again at it) */
		redraw = true;
	}
	if(old.brightness != next.brightness || changed(&old.night_mode, &next.night_mode, 6))
		apply_brightness(false);
	if((current_set == SET_WEATHER || current_set == SET_HOURLY || current_set == SET_SKY) &&
	   changed(old.set_faces[current_set], next.set_faces[current_set], 5))
		redraw = true;                                 /* (its screens moved to other orbs: drawn again where they go now) */
	if(current_set == SET_MESSAGES && (changed(old.set_faces[SET_MESSAGES], next.set_faces[SET_MESSAGES], 5) ||
	                                   changed(old.custom, next.custom, sizeof old.custom)))
		redraw = true;                                 /* (a message placed or changed: it showed only once the set came up again) */
	if(!set_available(current_set))
	{
		current_set = next_set(current_set, +1);       /* (the set showing was switched off) */
		redraw = true;
	}
	keep_pick();                                       /* (the start-up pick, not whatever shows: 2026-11-19) */
	settings_version++;
	if(try_active)
		try_until = orbs_millis() + 30000;             /* (trying: shown, not saved; 30 s after the last change it goes back) */
	else
		save_due_ms = orbs_millis() + 3000;
	if(memcmp(old.orb_bright, next.orb_bright, 5) || memcmp(old.orb_gain, next.orb_gain, 15))
	{
		for(int o = 0; o < 5; o++)
			lcd_set_trim(o, next.orb_bright[o], next.orb_gain[o][0], next.orb_gain[o][1], next.orb_gain[o][2]);
		redraw = true;
	}
	if(memcmp(old.set_brightness, next.set_brightness, sizeof old.set_brightness))
		apply_brightness(false);
	if(redraw)
		set_changed();
	return true;
}

bool web_action(JsonObjectConst a, char *why, size_t n)
{
	const char *d = a["do"] | "";
	if(!strcmp(d, "show"))
	{
		int k = set_key_index(a["set"] | "");
		if(k < 0) { snprintf(why, n, "no such set"); return false; }
		if(!set_available(k)) { snprintf(why, n, "%s isn't available now (switched off, or the rig is away)", SET_TITLES[k]); return false; }
		manual_dark = false;
		current_set = k;
		set_changed();
		set_picked();
		return true;
	}
	if(!strcmp(d, "dark"))
	{
		manual_dark = a["on"] | false;
		night_wake_until = manual_dark ? 0 : orbs_millis() + 60000;
		apply_brightness(true);
		return true;
	}
	if(!strcmp(d, "press"))
	{
		/* a button press sent from the settings page (2026-11-17): done by the main loop with the same code
		   as the real buttons (button()), so it does exactly what that press does on the set showing. Short, held
		   1/2 s or held 2 s only: the 15 s hold (wipe everything) can't be sent from here. */
		static const char *const B[3] = { "left", "middle", "right" }, *const K[3] = { "short", "medium", "long" };
		const char *b = a["button"] | "", *k = a["kind"] | "";
		int bi = -1, ki = -1;
		for(int i = 0; i < 3; i++)
		{
			if(!strcmp(b, B[i])) bi = i;
			if(!strcmp(k, K[i])) ki = i;
		}
		if(bi < 0 || ki < 0) { snprintf(why, n, "button left, middle or right; kind short, medium or long"); return false; }
		if(page_press_b >= 0) { snprintf(why, n, "the last press is still being done"); return false; }
		page_press_k = (int8_t)(BTN_SHORT + ki);
		page_press_b = (int8_t)bi;
		return true;
	}
	if(!strcmp(d, "test"))
	{
		int orb = a["orb"] | -1;
		if(orb < 0 || orb > 4) { snprintf(why, n, "orb 0-4"); return false; }
		show_test(orb);
		return true;
	}
	if(!strcmp(d, "try"))
	{
		/* try before saving: from here changes show but aren't kept until "keep" (or 30 s after the last: back) */
		if(!try_active)
		{
			if(save_due_ms) { save_due_ms = 0; save_settings(); }
			try_saved = settings;
			try_active = true;
		}
		try_until = orbs_millis() + 30000;
		printf("EV TRY started from the settings page\n");
		return true;
	}
	if(!strcmp(d, "keep"))
	{
		if(!try_active) { snprintf(why, n, "nothing being tried"); return false; }
		try_active = false;
		save_due_ms = orbs_millis();                     /* (saved now) */
		printf("EV TRY kept\n");
		return true;
	}
	if(!strcmp(d, "revert"))
	{
		if(!try_active) { snprintf(why, n, "nothing being tried"); return false; }
		try_active = false;
		web_apply(try_saved);
		save_due_ms = 0;
		printf("EV TRY put back by the settings page\n");
		return true;
	}
	if(!strcmp(d, "two_cores"))
	{
		pics_help_set(a["on"] | true);               /* (tests: the first core helps build the live faces or not; not saved) */
		return true;
	}
	if(!strcmp(d, "bench"))
	{
		pics_live_bench();                               /* (the "?" BENCH line has the result a moment later) */
		return true;
	}
	if(!strcmp(d, "restart"))
	{
		restart_at = orbs_millis() + 800;               /* (after this answer has gone) */
		printf("EV RESTART asked by the settings page\n");
		return true;
	}
	if(!strcmp(d, "defaults"))
	{
		orbs_settings_t keep = settings;
		settings_defaults_keep_secrets();              /* (Wi-Fi and the key stay) */
		orbs_settings_t next = settings;
		/* the rig's sets on and marked as met, as on a new unit (2026-11-17: the defaults' "met" list was empty, so at
		   the next restart the rig's catalogue switched them on by itself; the page test found it) */
		next.set_enabled |= sets_from_rig;
		next.sets_seen = (((1u << 10) - 1) | SETS_NEW_ON) | sets_from_rig;
		settings = keep;
		web_apply(next);
		load_clock_settings();
		set_changed();
		return true;
	}
	snprintf(why, n, "unknown action");
	return false;
}

/* ---- presets (version 4, presets.c) ---- */
void web_presets(JsonArray out)
{
	for(int i = 0; i < N_PRESETS; i++)
	{
		char name[24];
		if(preset_name(i, name, sizeof name))
		{
			JsonObject p = out.add<JsonObject>();
			p["slot"] = i;
			p["name"] = name;
		}
		else
			out.add(nullptr);
	}
}

bool web_preset(JsonObjectConst a, char *why, size_t n)
{
	const char *d = a["do"] | "";
	int slot = a["slot"] | -1;
	if(slot < 0 || slot >= N_PRESETS) { snprintf(why, n, "slot 0-%d", N_PRESETS - 1); return false; }
	if(!strcmp(d, "save"))
	{
		const char *name = a["name"] | "";
		if(!*name || strlen(name) > 23) { snprintf(why, n, "a name of 1 to 23 characters"); return false; }
		if(save_due_ms) { save_due_ms = 0; save_settings(); }
		pics_settle_keep_live(true);
		bool ok = preset_save(slot, name, try_active ? &try_saved : &settings);
		if(!ok) snprintf(why, n, "the flash write didn't take");
		return ok;
	}
	if(!strcmp(d, "apply"))
	{
		orbs_settings_t next = settings;
		if(!preset_load(slot, &next)) { snprintf(why, n, "that preset is empty (or from other firmware)"); return false; }
		web_apply(next);
		load_clock_settings();
		set_changed();
		return true;
	}
	if(!strcmp(d, "delete"))
	{
		pics_settle_keep_live(true);
		bool ok = preset_delete(slot);
		if(!ok) snprintf(why, n, "the flash write didn't take");
		return ok;
	}
	snprintf(why, n, "save, apply or delete");
	return false;
}

/* what an orb shows, for the page: the rig's last JPEG on its sets, else the XL's own picture at half size (a BMP) */
size_t web_orb_picture(int orb, uint8_t *buf, size_t cap, const uint8_t **ext, const char **ctype)
{
	uint32_t now = orbs_millis();
	bool rig_pics = set_rig[current_set] >= 0 && current_set != SET_VIZ && !(current_set == SET_CAMS && !cams_from_rig);
	if((int32_t)(now - orb_pics_wanted_until) >= 0)
	{
		/* the page starts looking: the copies from its last look may be old (they're kept only while it looks, and a
		   set whose pictures don't change gets none sent again), so the rig sends every orb's picture again
		   (2026-11-17: an idle printer's faces stayed black on the page) */
		orb_look_start = now;
		if(rig_pics)
			rig.need |= (uint8_t)(0x1F & ~live_shown);
	}
	orb_pics_wanted_until = now + 10000;
	bool live_now = (live_shown & (1u << orb)) != 0;
	int k = orb_jpeg_new[orb];
	if(rig_pics && !live_now)
	{
		if(orb_jpeg_set[k][orb] == current_set && orb_jpeg_len[k][orb] && (int32_t)(orb_jpeg_at[k][orb] - orb_look_start) >= 0)
		{
			*ext = orb_jpeg[k][orb];
			*ctype = "image/jpeg";
			return orb_jpeg_len[k][orb];
		}
		rig.need |= (uint8_t)(1u << orb);     /* (none yet from this set: asked for; black till it comes, never another) */
	}
	const int W = 120, H = 120;
	const size_t hdr = 14 + 40 + 12, size = hdr + (size_t)W * H * 2;
	if(cap < size)
		return 0;
	memset(buf, 0, hdr);
	auto u32 = [&](size_t at, uint32_t v) { buf[at] = (uint8_t)v; buf[at + 1] = (uint8_t)(v >> 8); buf[at + 2] = (uint8_t)(v >> 16); buf[at + 3] = (uint8_t)(v >> 24); };
	buf[0] = 'B'; buf[1] = 'M';
	u32(2, (uint32_t)size); u32(10, (uint32_t)hdr); u32(14, 40); u32(18, W); u32(22, (uint32_t)-H);   /* (top-down rows) */
	buf[26] = 1; buf[28] = 16; u32(30, 3);                                                         /* BI_BITFIELDS */
	u32(54, 0xF800); u32(58, 0x07E0); u32(62, 0x001F);
	const uint16_t *fb = orb_fb[screens.getDisplay().mirror() ? 0 : orb];
	if(live_now)          /* (the second core writes live faces there past the cache: read it the same way) */
		fb = (const uint16_t *)((uintptr_t)fb + (XIP_NOCACHE_NOALLOC_BASE - XIP_BASE));
	uint16_t *out = (uint16_t *)(buf + hdr);
	if(orbs_dark() || (rig_pics && !live_now))
		memset(out, 0, (size_t)W * H * 2);
	else
		for(int y = 0; y < H; y++)
			for(int x = 0; x < W; x++)
			{
				/* (the average of each 2 x 2; the picture memory holds byte-swapped RGB565) */
				uint32_t r = 0, g = 0, b = 0;
				for(int k = 0; k < 4; k++)
				{
					uint16_t v = fb[(y * 2 + (k >> 1)) * 240 + x * 2 + (k & 1)];
					v = (uint16_t)(v >> 8 | v << 8);
					r += v >> 11; g += v >> 5 & 63; b += v & 31;
				}
				out[y * W + x] = (uint16_t)((r / 4) << 11 | (g / 4) << 5 | (b / 4));
			}
	*ext = nullptr;
	*ctype = "image/bmp";
	return size;
}

static uint32_t wifi_switch_at;                /* the hotspot page saved a network: closed and joined after the answer */
static uint32_t after_setup_until;             /* the hotspot just closed: show the new address once joined */
void web_wifi_save(const char *ssid, const char *pass)
{
	pics_settle();
	memset(settings.wifi_ssid, 0, sizeof settings.wifi_ssid);
	memset(settings.wifi_pass, 0, sizeof settings.wifi_pass);
	strncpy(settings.wifi_ssid, ssid, sizeof settings.wifi_ssid - 1);
	strncpy(settings.wifi_pass, pass, sizeof settings.wifi_pass - 1);
	settings_save();
	wifi_switch_at = orbs_millis() + 1500;
}

bool web_https_free(void)
{
	return !wx_fetching && !fetch_test && !fgcams.busy();
}

int main(void)
{
	/* a real watchdog restart only (a picotool reboot goes through the watchdog too, and watchdog_caused_reboot()
	   counted that: the false "wd_restart=1" of 2026-11-16) */
	wd_restart = watchdog_enable_caused_reboot();
	if(wd_restart)
	{
		boot_step = (watchdog_hw->scratch[0] & 0xFFFF0000u) == STEP_MAGIC ? watchdog_hw->scratch[0] & 0xFFFF : 0xFFFF;
		if((watchdog_hw->scratch[3] & 0xFFFF0000u) == CRASH_MAGIC)
		{
			boot_crash_pc = watchdog_hw->scratch[1];
			boot_crash_lr = watchdog_hw->scratch[2];
			boot_crash_core = (watchdog_hw->scratch[3] & 0xFF) + 1;
		}
	}
	for(int i = 0; i < 4; i++)
		watchdog_hw->scratch[i] = 0;
	stdio_init_all();
	for(int i = 0; i < 3; i++)
	{
		gpio_init(btn_pin[i]);
		gpio_set_dir(btn_pin[i], GPIO_IN);
		gpio_pull_down(btn_pin[i]);
	}
	static repeating_timer_t timer;
	add_repeating_timer_ms(-1, btn_tick, NULL, &timer);

	for(int o = 0; o < N_ORB_FB; o++)
		orb_fb[o] = fb_psram[o];
	settings_load();
	if(settings.current_set < N_SETS)
		picked_set = saved_pick = settings.current_set;   /* (the start-up pick, 2026-11-19) */
	camlogin_load();
	load_clock_settings();
	frigate_tz = &tz;
	lcd_init();
	lcd_set_link(settings.link_speed);       /* (the page's Screens > Screen link; the start-up itself ran at 5 MHz) */
	for(int o = 0; o < 5; o++)
	{
		lcd_set_rotation(o, settings.rotation[o]);   /* each orb turned by its own screen (MADCTL), nothing redrawn */
		lcd_set_trim(o, settings.orb_bright[o], settings.orb_gain[o][0], settings.orb_gain[o][1], settings.orb_gain[o][2]);
	}
	screens.setBrightness(settings.brightness);
	screens.begin();
	screens.clearAllScreens();
	screens.flush();
	adc_init();
	adc_set_temp_sensor_enabled(true);
	catalog_json[0] = 0;                     /* (PSRAM isn't cleared at start-up: no catalogue until the rig sends one) */
	pics_init();                             /* the second core: the rig's pictures (pics.cpp) */
	net_init();
	watchdog_enable(4000, true);
	printf("EV START build=%s wd_restart=%d psram=%u settings=%s last_step=%04lx crash=%lu:%08lx:%08lx\n", ORBS_BUILD, wd_restart,
	       (unsigned)psram_get_size(), settings_loaded_from_flash() ? "flash" : "defaults", (unsigned long)boot_step,
	       (unsigned long)boot_crash_core, (unsigned long)boot_crash_pc, (unsigned long)boot_crash_lr);

	uint32_t next_net = 0, next_refresh = 0;
	bool shown_set = false;
	bool c1_reported = false;
	for(;;)
	{
		/* the second core stuck on one picture for 3 s: said, and the watchdog restarts the XL (seen as wd_restart=1) */
		if(pics_stuck(orbs_millis()))
		{
			if(!c1_reported)
				printf("EV STUCK the picture core has been on one picture for 3 s (its step %u, main loop's %04lx): the "
				       "watchdog restarts the XL if it stays stuck\n",
				       (unsigned)pics_core1_step, (unsigned long)(watchdog_hw->scratch[0] & 0xFFFF));
			c1_reported = true;
		}
		else
		{
			if(c1_reported)
				printf("EV STUCK over: the picture core moved on\n");   /* (and a later one is said again) */
			c1_reported = false;
			watchdog_update();
		}
		step(1);
		pics_help();                               /* (the live faces' strips the second core has waiting: built here too) */
		usb_poll();
		step(2);
		pics_help();
		pics_answers();
		step(3);
		uint32_t now = orbs_millis();
		live_update(now);
		if(try_active && (int32_t)(now - try_until) >= 0)
		{
			try_active = false;                        /* (tried and not kept: back to the saved settings) */
			web_apply(try_saved);
			save_due_ms = 0;
			printf("EV TRY not kept in 30 s: put back\n");
		}
		rules_poll(now);
		if((int32_t)(now - next_net) >= 0)
		{
			net_poll();
			next_net = now + 100;
			/* (once: the settings page on port 80. Also when the setup hotspot is open: with no home Wi-Fi joined since
			   power-on, nothing answered at 192.168.4.1 — 2026-11-17, found when a phone joined it) */
			if(net_up() || net_hotspot())
				web_init();
		}
		pics_help();
		web_poll(now);
		pics_help();
		if(wifi_switch_at && (int32_t)(now - wifi_switch_at) >= 0)
		{
			wifi_switch_at = 0;
			status_shown[0] = 0;
			net_wifi_saved();                  /* (the hotspot closes, the new network is tried) */
		}
		static bool hotspot_was;
		if(net_hotspot() != hotspot_was)
		{
			hotspot_was = net_hotspot();
			if(hotspot_was)
				show_setup();
			else
			{
				setup_shown = false;
				status_shown[0] = 0;
				after_setup_until = now + 120000;      /* (joined within 2 minutes: say so with the new address) */
				if(orbs_time_synced())
					set_changed();
			}
		}
		if(after_setup_until && net_up() && orbs_time_synced())
		{
			after_setup_until = 0;
			char t[48];
			snprintf(t, sizeof t, "On %s", settings.wifi_ssid);
			show_qr_titled(t, 20000);              /* (the Wi-Fi page promised it: where the orbs are now) */
		}
		else if(after_setup_until && (int32_t)(now - after_setup_until) >= 0)
			after_setup_until = 0;
		if(save_due_ms && (int32_t)(now - save_due_ms) >= 0)
		{
			save_due_ms = 0;
			save_settings();                   /* (the page's changes, 3 s after the last one) */
		}
		if(pick_save_due_ms && (int32_t)(now - pick_save_due_ms) >= 0 && !try_active)
		{
			pick_save_due_ms = 0;              /* (a picked set, 20 s after the last pick; only if the flash holds another) */
			if(picked_set >= 0 && picked_set != saved_pick)
			{
				printf("EV PICK saved: %s opens at start-up\n", SET_KEYS[picked_set]);
				save_settings();
			}
		}
		if(restart_at && (int32_t)(now - restart_at) >= 0)
		{
			pics_settle();
			if(save_due_ms || (pick_save_due_ms && !try_active && picked_set != saved_pick))
				save_settings();
			watchdog_reboot(0, 0, 10);
			for(;;)
				tight_loop_contents();
		}
		step(4);
		/* the weather */
		frigate_12h = clockw.m_format != CLOCK_FORMAT_24_HOUR;
		/* (Frigate's pictures come back to back: the weather, when due, goes first) */
		bool wx_due = settings.owm_key[0] && settings.weather_enabled &&
		              ((int32_t)(now - wx_next_ms) >= 0 || (int32_t)(now - air_next_ms) >= 0);
		if(net_up() && orbs_time_synced())
			fgcams.poll(now, !fetch_test && !wx_fetching && !wx_due && !web_geocode_busy(), current_set == SET_CAMS && !cams_from_rig);
		bool can_fetch = !fgcams.busy() && !web_geocode_busy() && !fetch_test && !wx_fetching && net_up() && orbs_time_synced() && settings.owm_key[0] && settings.weather_enabled;
		if(can_fetch && (int32_t)(now - wx_next_ms) >= 0)
		{
			char path[256];
			weather_path(path, sizeof path);
			wx_fetching = https_start("api.openweathermap.org", path, http_buf, sizeof http_buf);
			wx_kind = 0;
			memset(path, 0, sizeof path);
			wx_next_ms = now + 60000;          /* (until it's known how it went) */
		}
		else if(can_fetch && (int32_t)(now - air_next_ms) >= 0)
		{
			/* the Sky set's air quality: OpenWeatherMap's air pollution service, the same key, once an hour */
			char path[256];
			snprintf(path, sizeof path, "/data/2.5/air_pollution?lat=%s&lon=%s&appid=%s", settings.weather_lat, settings.weather_lon,
			         settings.owm_key);
			wx_fetching = https_start("api.openweathermap.org", path, http_buf, sizeof http_buf);
			wx_kind = 1;
			memset(path, 0, sizeof path);
			air_next_ms = now + 60000;
		}
		step(5);
		if(wx_fetching)
		{
			https_state_t hs = https_poll();
			if(hs == HTTPS_DONE || hs == HTTPS_FAILED)
			{
				size_t bl = 0;
				const char *body = https_body(&bl);
				wx_last_ms = https_ms();
				if(wx_kind == 1)
				{
					int aqi = 0;
					float pm = 0;
					if(hs == HTTPS_DONE && https_status() == 200 && air_parse(body, bl, aqi, pm))
					{
						skyx.aqi = aqi;
						skyx.pm25 = pm;
						air_ok++;
						air_next_ms = now + 3600000;
						printf("EV AIR ok aqi=%d pm25=%.1f\n", aqi, (double)pm);
					}
					else
					{
						air_fails++;
						air_next_ms = now + 300000;
						printf("EV AIR failed: %s (http %d)\n", hs == HTTPS_FAILED ? https_error() : "bad answer", https_status());
					}
				}
				else if(hs == HTTPS_DONE && https_status() == 200 && weather_parse(body, bl, settings.weather_city, wxd))
				{
					wx_ok++;
					wx_last_ok_ms = now;
					wx_next_ms = now + (uint32_t)settings.weather_refresh_min * 60000u;
					snprintf(wx_err, sizeof wx_err, "none");
					printf("EV WEATHER ok bytes=%u ms=%u temp=%.1f\n", (unsigned)bl, https_ms(), (double)wxd.currentTemp);
				}
				else
				{
					wx_fails++;
					wx_next_ms = now + 60000;
					if(hs == HTTPS_FAILED)
						snprintf(wx_err, sizeof wx_err, "%s", https_error());
					else if(https_status() != 200)
						snprintf(wx_err, sizeof wx_err, "OpenWeatherMap said HTTP %d", https_status());
					else
						snprintf(wx_err, sizeof wx_err, "the answer didn't parse (%u bytes)", (unsigned)bl);
					printf("EV WEATHER failed: %s\n", wx_err);
				}
				https_reset();
				wx_fetching = false;
			}
		}
		if(fetch_test)
		{
			https_state_t hs = https_poll();
			if(hs == HTTPS_DONE || hs == HTTPS_FAILED)
			{
				struct mallinfo mi = mallinfo();
				size_t bl = 0;
				https_body(&bl);
				if(hs == HTTPS_DONE)
					printf("FETCH status=%d bytes=%u ms=%u heap_used=%d\n", https_status(), (unsigned)bl, https_ms(), mi.uordblks);
				else
					printf("FETCH failed: %s\n", https_error());
				https_reset();
				fetch_test = false;
			}
		}
		step(6);
		if((int32_t)(now - next_refresh) >= 0 && (int32_t)(now - refresh_hold_until) >= 0)
		{
			lcd_refresh();                 /* every screen's settings again (lcd.c) */
			next_refresh = now + 5000;
		}
		step(7);
		while(bq_out != bq_in)
		{
			__dmb();
			bev_t e = bq[bq_out % BQ];
			bq_out++;
			if(e.down)
			{
				presses[e.btn]++;
				down_at[e.btn] = e.t;
				printf("EV BTN %s DOWN t=%lu\n", btn_name[e.btn], (unsigned long)e.t);
			}
			else
			{
				ButtonState st = press_state(e.t - down_at[e.btn]);
				printf("EV BTN %s UP t=%lu held_ms=%lu state=%d\n", btn_name[e.btn], (unsigned long)e.t,
				       (unsigned long)(e.t - down_at[e.btn]), (int)st);
				button(e.btn, st);
			}
		}
		if(page_press_b >= 0)
		{
			static const char *const kind[] = { "", "short", "medium", "long" };
			int b = page_press_b;
			ButtonState st = (ButtonState)page_press_k;
			page_press_b = -1;
			printf("EV PAGE PRESS %s %s (done like the real button)\n", btn_name[b], kind[st]);
			button(b, st);
		}
		step(8);
		if(!orbs_time_synced())
		{
			show_status();
			shown_set = false;
		}
		else
		{
			int64_t s = orbs_time_now();
			if(rig_waits >= 0 && !(settings.set_enabled >> rig_waits & 1))
				rig_waits = -1;                    /* (its set was switched off meanwhile: nothing to go back to) */
			if(!shown_set)
			{
				shown_set = true;
				status_shown[0] = 0;
				int want = settings.start_set < N_SETS ? settings.start_set : settings.current_set;   /* (the page's "at start-up") */
				current_set = want < N_SETS && set_available(want) ? want : next_set(want < N_SETS ? want : SET_CLOCK, +1);
				rig_waits = -1;
				if(want < N_SETS && current_set != want && set_rig[want] >= 0 && (settings.set_enabled >> want & 1))
				{
					rig_waits = want;              /* (a rig set before the rig's feed is there: it comes up when the feed does) */
					rig_waits_start = true;
					printf("EV FIRST SET %s waits for the rig: %s meanwhile\n", SET_KEYS[want], SET_KEYS[current_set]);
				}
				else
					printf("EV FIRST SET %s\n", SET_KEYS[current_set]);
				shown_brightness = 255;
				apply_brightness(true);            /* (night or day brightness; draws the set) */
				last_shown_s = s;
			}
			else if(net_hotspot() && !overlay_until)
			{
				if(!setup_shown && !orbs_dark())
					show_setup();                  /* (the setup screens, the whole time the hotspot is open) */
			}
			else if(current_set == SET_CAMS && cams_from_rig && !rig.alive(now) && camlogin_set())
			{
				printf("EV RIG gone: the cameras from Frigate\n");
				set_changed();
			}
			else if(rig_waits >= 0 && set_available(rig_waits) && rule_active < 0 && !overlay_until)
			{                                      /* (before the cameras' own "back" below: straight to the waiting set) */
				printf(rig_waits_start ? "EV RIG here: the first set %s\n" : "EV RIG here: back to %s\n", SET_KEYS[rig_waits]);
				if(current_set != rig_waits)
				{
					current_set = rig_waits;
					set_changed();
				}
				rig_waits = -1;
			}
			else if(current_set == SET_CAMS && !cams_from_rig && rig.alive(now))
			{
				printf("EV RIG back: the cameras from the rig\n");
				set_changed();
			}
			else if(set_rig[current_set] >= 0 && !rig.alive(now) && !(current_set == SET_CAMS && !cams_from_rig))
			{
				int left = current_set;
				current_set = next_set(current_set, +1);
				if(rig_waits < 0 && rule_active < 0)
				{                                  /* (a rule's set: the rule's own way back covers it, see rule_end) */
					rig_waits = left;
					rig_waits_start = false;
					printf("EV RIG gone: on to %s, back to %s when the feed is\n", SET_KEYS[current_set], SET_KEYS[left]);
				}
				else
					printf("EV RIG gone: on to %s\n", SET_KEYS[current_set]);
				set_changed();
			}
			else if(overlay_until && (int32_t)(now - overlay_until) >= 0)
			{
				overlay_until = 0;                 /* (the test picture or the QR code is over) */
				set_changed();
			}
			else if(overlay_until || orbs_dark())
				;                                  /* (nothing drawn under the test picture / QR code, or dark) */
			else if(cycle_for(current_set) && now - set_since_ms >= (uint32_t)cycle_for(current_set) * 1000u)
			{
				int n = next_set(current_set, +1); /* (the page's auto-cycle) */
				set_since_ms = now;
				if(n != current_set)
				{
					current_set = n;
					set_changed();
				}
			}
			else if(current_set == SET_MATRIX || set_rig[current_set] >= 0)
				draw_set(false);
			else if(s != last_shown_s)
			{
				last_shown_s = s;
				draw_set(false);
			}
			static uint32_t next_night;
			if((int32_t)(now - next_night) >= 0)
			{
				next_night = now + 5000;
				apply_brightness(false);           /* (night dimming starts and ends by itself) */
			}
		}
		step(9);
		sleep_us(500);
	}
}
