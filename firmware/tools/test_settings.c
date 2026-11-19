/* The settings upgrades on the PC with made-up values (no real Wi-Fi or key): version 1 (the XL from 2026-11-16),
   version 2 (later that day) and version 3 (2026-11-17, the settings page; what the XL holds now) to version 4 (the
   big build: way more customization).
   Run: tools/test_settings.sh */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "../src/settings.h"
#include "hardware/flash.h"
uint8_t fake_flash_sector[FLASH_SECTOR_SIZE];
int flash_safe_execute(void (*fn)(void *), void *arg, uint32_t t) { (void)t; fn(arg); return 0; }
void flash_range_erase(uint32_t off, size_t n) { (void)off; memset(fake_flash_sector, 0xFF, n); }
void flash_range_program(uint32_t off, const uint8_t *d, size_t n) { (void)off; memcpy(fake_flash_sector, d, n); }
typedef struct {   /* version 1, as src/settings.h was at build 2611.24148 */
	uint32_t magic, version; char wifi_ssid[33], wifi_pass[65], owm_key[48], tz_name[40], ntp_server[40];
	uint8_t clock_type, clock_format, show_second_ticks, clock_shadowing; uint16_t clock_color, clock_shadow_color, clock_nixie_color;
	uint8_t orb_rotation, brightness, current_set; uint8_t pad[3]; uint32_t crc;
} v1_t;
typedef struct {   /* version 2, as src/settings.h was from build 2611.24253 to 2611.24550 */
	uint32_t magic, version; char wifi_ssid[33], wifi_pass[65], owm_key[48], tz_name[40], ntp_server[40];
	uint8_t clock_type, clock_format, show_second_ticks, clock_shadowing; uint16_t clock_color, clock_shadow_color, clock_nixie_color;
	uint8_t orb_rotation, brightness, current_set; uint8_t pad[3];
	char weather_lat[16], weather_lon[16], weather_city[24];
	uint8_t weather_enabled, weather_units, weather_dark, weather_hl_cycle_s; uint32_t crc;
} v2_t;
/* version 3, exactly as src/settings.h was from build 2611.24600 to 2611.24957 (copied from the commit) */
typedef struct {
	uint32_t magic, version;
	char wifi_ssid[33];
	char wifi_pass[65];
	char owm_key[48];
	char tz_name[40];            /* IANA name, e.g. America/New_York (timezoneLoc) */
	char ntp_server[40];         /* ntpServer */
	/* clock (ClockWidget) */
	uint8_t clock_type;          /* 0 Normal, 1 Nixie (defaultType) */
	uint8_t clock_format;        /* 0 24 h, 1 12 h, 2 12 h + AM/PM (clockFormat) */
	uint8_t show_second_ticks;   /* showSecondTicks */
	uint8_t clock_shadowing;     /* clkShadowing */
	uint32_t clock_color;        /* clkColor, 0xRRGGBB */
	uint32_t clock_shadow_color; /* clkShColor */
	uint32_t clock_nixie_color;  /* clkNixieColor (0 = the tubes' own orange) */
	/* screens */
	uint8_t brightness;          /* tftBrightness, 10-255 */
	uint8_t rotation[5];         /* each orb: 0-3 = 0, 90, 180, 270 degrees (the screen turns it itself) */
	uint8_t night_mode;          /* 0 off, 1 dimmer, 2 dark (nightmode) */
	uint8_t night_brightness;    /* dimBrightness */
	uint16_t night_start_min;    /* minutes after midnight (dimStartHour) */
	uint16_t night_end_min;      /* (dimEndHour) */
	/* sets */
	uint8_t current_set;         /* the set showing at power-off */
	uint8_t set_order[10];   /* set numbers in the rotation's order */
	uint8_t start_set;           /* the set at start-up, 0xFF = the last one shown */
	uint16_t set_enabled;        /* bit per set number */
	uint16_t cycle_s;            /* widgetCycDelay: next set every so many seconds, 0 = off */
	/* weather (WeatherWidget, OpenWeatherMapFeed) */
	char weather_lat[16];        /* openWeatherLat */
	char weather_lon[16];        /* openWeatherLong */
	char weather_city[24];       /* openWeatherName */
	uint8_t weather_enabled;     /* weatherEnabled */
	uint8_t weather_units;       /* weatherUnits: 0 metric, 1 imperial */
	uint8_t weather_dark;        /* weatherScrMode: 1 dark, 0 light */
	uint8_t weather_refresh_min; /* a new forecast every so many minutes (5-60) */
	uint16_t weather_hl_cycle_s; /* weatherCycleHL: seconds between highs and lows (0 = off) */
	/* the Matrix (MatrixWidget) */
	uint8_t matrix_enabled_unused; /* (the set's on/off is in set_enabled) */
	uint8_t matrix_big_font;     /* mtxBigFont */
	uint8_t matrix_line_min, matrix_line_max, matrix_speed_min, matrix_speed_max;   /* mtxLineMin.. mtxSpeedMax */
	uint16_t matrix_frame_ms;    /* mtxUpdateInt */
	uint32_t matrix_color;       /* mtxTextColor */
	uint32_t matrix_head_color;  /* mtxHeadTxColor */
	/* the rig's sets: kept here, the rig follows over USB ("R") */
	uint8_t rig_hz;              /* Rig stats pictures a second: 1, 2, 4 */
	uint8_t rig_middle;          /* the middle orb's first page: 0 memory, 1 health, 2 power, 3 cooling */
	uint8_t cams_page[2][5];     /* camera numbers (frigate_cams.cpp's order) on each orb, pages 1 and 2 */
	uint8_t cam_quality;         /* the rig's camera pictures, JPEG quality 60-95 */
	uint8_t cam_fps;             /* frames a second from each camera 1-15 */
	uint32_t viz_color;          /* 0xFF000000 album, 0xFE000000 rainbow, else 0xRRGGBB */
	uint8_t spare[64];           /* room for new settings without a new version (zeros = their defaults) */
	uint32_t crc;
} v3_t;

static uint32_t crc32(const void *p, size_t n) { const uint8_t *b = p; uint32_t c = 0xFFFFFFFFu; while(n--) { c ^= *b++; for(int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); } return ~c; }
static int fails;
#define CHECK(c, what) do { printf("%s %s\n", (c) ? "PASS" : "FAIL", what); if(!(c)) fails++; } while(0)

static void common_v3_defaults(const char *when)
{
	char what[256];
	snprintf(what, sizeof what, "%s: new settings at their defaults (night off 22:00-07:00, all ten sets on in order, start = last, "
	         "Matrix colours, rig 4/s, cameras 0-4 / 5,6,0,1,2, album colour)", when);
	int order_ok = 1;
	for(int i = 0; i < N_SETS_SAVED; i++)
		order_ok &= settings.set_order[i] == i;
	CHECK(settings.night_mode == 0 && settings.night_start_min == 22 * 60 && settings.night_end_min == 7 * 60 && order_ok &&
	      settings.set_enabled == (0x3FFu | SETS_NEW_ON) && settings.start_set == 0xFF && settings.cycle_s == 0 && settings.weather_refresh_min == 10 &&
	      settings.matrix_line_min == 3 && settings.matrix_line_max == 15 && settings.matrix_frame_ms == 100 && settings.rig_hz == 4 &&
	      settings.cams_page[0][4] == 4 && settings.cams_page[1][0] == 5 && settings.cams_page[1][2] == 0 &&
	      settings.cam_quality == 86 && settings.viz_color == VIZ_ALBUM, what);
}

int main(void)
{
	const orbs_settings_t *f = (const orbs_settings_t *)fake_flash_sector;
	/* ---- version 2 (what the XL holds now) ---- */
	v2_t w; memset(&w, 0, sizeof w);
	w.magic = 0x4F524253u; w.version = 2;
	strcpy(w.wifi_ssid, "TestNet"); strcpy(w.wifi_pass, "made-up-pass-123"); strcpy(w.owm_key, "0123456789abcdef0123456789abcdef");
	strcpy(w.tz_name, "America/Chicago"); strcpy(w.ntp_server, "time.example.org");
	w.clock_type = 1; w.clock_format = 0; w.show_second_ticks = 0; w.clock_shadowing = 1; w.clock_color = 0x4FE0; w.clock_shadow_color = 0x20A1;
	w.clock_nixie_color = 0xF800; w.orb_rotation = 2; w.brightness = 200; w.current_set = 7;
	strcpy(w.weather_lat, "41.5"); strcpy(w.weather_lon, "-81.25"); strcpy(w.weather_city, "Elsewhere");
	w.weather_enabled = 1; w.weather_units = 0; w.weather_dark = 0; w.weather_hl_cycle_s = 33;
	w.crc = crc32(&w, offsetof(v2_t, crc));
	memset(fake_flash_sector, 0xFF, sizeof fake_flash_sector); memcpy(fake_flash_sector, &w, sizeof w);
	settings_load();
	CHECK(settings_loaded_from_flash(), "v2: recognised");
	CHECK(strcmp(settings.wifi_ssid, "TestNet") == 0 && strcmp(settings.wifi_pass, "made-up-pass-123") == 0, "v2: Wi-Fi name and password kept");
	CHECK(strcmp(settings.owm_key, "0123456789abcdef0123456789abcdef") == 0, "v2: weather key kept");
	CHECK(strcmp(settings.tz_name, "America/Chicago") == 0 && strcmp(settings.ntp_server, "time.example.org") == 0, "v2: time zone and server kept");
	CHECK(settings.clock_type == 1 && settings.clock_format == 0 && settings.show_second_ticks == 0 && settings.clock_shadowing == 1, "v2: clock style, format, ticks, shadow kept");
	CHECK(settings.clock_color == 0x4AFF00 && settings.clock_shadow_color == 0x201408 && settings.clock_nixie_color == 0xFF0000,
	      "v2: colours turned into 0xRRGGBB (0x4FE0 -> #4aff00, 0x20A1 -> #201408, exactly)");
	CHECK(rgb888_to_565(settings.clock_color) == 0x4FE0 && rgb888_to_565(settings.clock_shadow_color) == 0x20A1,
	      "v2: and back to exactly the RGB565 the clock drew before");
	CHECK(settings.brightness == 200 && settings.current_set == 7, "v2: brightness and the set showing kept");
	CHECK(settings.rotation[0] == 2 && settings.rotation[4] == 2, "v2: the one rotation given to all five orbs");
	CHECK(strcmp(settings.weather_city, "Elsewhere") == 0 && strcmp(settings.weather_lat, "41.5") == 0 && strcmp(settings.weather_lon, "-81.25") == 0 &&
	      settings.weather_enabled == 1 && settings.weather_units == 0 && settings.weather_dark == 0 && settings.weather_hl_cycle_s == 33, "v2: every weather setting kept");
	common_v3_defaults("v2");
	CHECK(f->version == 4 && f->crc == crc32(f, offsetof(orbs_settings_t, crc)), "v2: saved back as version 4 with a good checksum");
	settings_load();
	CHECK(settings_loaded_from_flash() && strcmp(settings.wifi_pass, "made-up-pass-123") == 0 && settings.weather_hl_cycle_s == 33, "v4: reads back with everything");
	settings.cycle_s = 45; settings.night_mode = 1; settings.cams_page[1][4] = 6; settings.viz_color = 0x123456;
	settings_save(); memset(&settings, 0, sizeof settings); settings_load();
	CHECK(settings.cycle_s == 45 && settings.night_mode == 1 && settings.cams_page[1][4] == 6 && settings.viz_color == 0x123456, "v4: settings save and read back");
	settings_defaults_keep_secrets();
	CHECK(strcmp(settings.wifi_pass, "made-up-pass-123") == 0 && strcmp(settings.owm_key, "0123456789abcdef0123456789abcdef") == 0 &&
	      settings.cycle_s == 0 && settings.clock_type == 0 && strcmp(settings.tz_name, "America/New_York") == 0, "the page's Defaults: everything back, Wi-Fi and key kept");
	/* ---- version 1 (still upgrades) ---- */
	v1_t v; memset(&v, 0, sizeof v);
	v.magic = 0x4F524253u; v.version = 1;
	strcpy(v.wifi_ssid, "TestNet"); strcpy(v.wifi_pass, "made-up-pass-123"); strcpy(v.owm_key, "0123456789abcdef0123456789abcdef");
	strcpy(v.tz_name, "America/New_York"); strcpy(v.ntp_server, "pool.ntp.org");
	v.clock_type = 1; v.clock_format = 2; v.show_second_ticks = 1; v.clock_shadowing = 1; v.clock_color = 0x4FE0; v.clock_shadow_color = 0x20A1;
	v.brightness = 255; v.current_set = 0;
	v.crc = crc32(&v, offsetof(v1_t, crc));
	memset(fake_flash_sector, 0xFF, sizeof fake_flash_sector); memcpy(fake_flash_sector, &v, sizeof v);
	settings_load();
	CHECK(settings_loaded_from_flash() && strcmp(settings.wifi_pass, "made-up-pass-123") == 0 && settings.clock_type == 1 && settings.clock_color == 0x4AFF00, "v1: Wi-Fi, key and clock kept");
	CHECK(strcmp(settings.weather_city, "New York") == 0 && settings.weather_units == 1 && settings.weather_hl_cycle_s == 20, "v1: weather defaults added");
	common_v3_defaults("v1");
	CHECK(f->version == 4, "v1: saved back as version 4");
	/* ---- version 3 (what the XL holds now) -> 4: every field a made-up value, each one checked ---- */
	v3_t t; memset(&t, 0, sizeof t);
	t.magic = 0x4F524253u; t.version = 3;
	strcpy(t.wifi_ssid, "TestNet3"); strcpy(t.wifi_pass, "made-up-pass-333"); strcpy(t.owm_key, "fedcba9876543210fedcba9876543210");
	strcpy(t.tz_name, "Europe/Lisbon"); strcpy(t.ntp_server, "ntp.example.net");
	t.clock_type = 1; t.clock_format = 1; t.show_second_ticks = 0; t.clock_shadowing = 0;
	t.clock_color = 0x1100FF; t.clock_shadow_color = 0x223344; t.clock_nixie_color = 0x556677;
	t.brightness = 177; t.rotation[0] = 1; t.rotation[1] = 2; t.rotation[2] = 3; t.rotation[3] = 0; t.rotation[4] = 2;
	t.night_mode = 2; t.night_brightness = 33; t.night_start_min = 1300; t.night_end_min = 400;
	t.current_set = 6;
	static const uint8_t order3[10] = { 6, 5, 9, 0, 1, 2, 3, 4, 8, 7 };
	memcpy(t.set_order, order3, 10);
	t.start_set = 5; t.set_enabled = 0x2F7; t.cycle_s = 37;
	strcpy(t.weather_lat, "38.72"); strcpy(t.weather_lon, "-9.14"); strcpy(t.weather_city, "Lisbon");
	t.weather_enabled = 0; t.weather_units = 0; t.weather_dark = 0; t.weather_refresh_min = 25; t.weather_hl_cycle_s = 41;
	t.matrix_big_font = 1; t.matrix_line_min = 4; t.matrix_line_max = 11; t.matrix_speed_min = 2; t.matrix_speed_max = 9;
	t.matrix_frame_ms = 70; t.matrix_color = 0xC8840E; t.matrix_head_color = 0x6AC67B;
	t.rig_hz = 2; t.rig_middle = 3;
	static const uint8_t c1[5] = { 4, 3, 2, 1, 0 }, c2[5] = { 6, 5, 4, 3, 2 };
	memcpy(t.cams_page[0], c1, 5); memcpy(t.cams_page[1], c2, 5);
	t.cam_quality = 77; t.cam_fps = 12; t.viz_color = VIZ_RAINBOW;
	t.crc = crc32(&t, offsetof(v3_t, crc));
	memset(fake_flash_sector, 0xFF, sizeof fake_flash_sector); memcpy(fake_flash_sector, &t, sizeof t);
	settings_load();
	CHECK(settings_loaded_from_flash(), "v3: recognised");
	CHECK(strcmp(settings.wifi_ssid, "TestNet3") == 0 && strcmp(settings.wifi_pass, "made-up-pass-333") == 0 &&
	      strcmp(settings.owm_key, "fedcba9876543210fedcba9876543210") == 0, "v3: Wi-Fi and key kept");
	CHECK(strcmp(settings.tz_name, "Europe/Lisbon") == 0 && strcmp(settings.ntp_server, "ntp.example.net") == 0, "v3: time zone and server kept");
	CHECK(settings.clock_type == 1 && settings.clock_format == 1 && settings.show_second_ticks == 0 && settings.clock_shadowing == 0 &&
	      settings.clock_color == 0x1100FF && settings.clock_shadow_color == 0x223344 && settings.clock_nixie_color == 0x556677, "v3: every clock setting kept");
	CHECK(settings.brightness == 177 && settings.rotation[0] == 1 && settings.rotation[1] == 2 && settings.rotation[2] == 3 &&
	      settings.rotation[3] == 0 && settings.rotation[4] == 2, "v3: brightness and each orb's rotation kept");
	CHECK(settings.night_mode == 2 && settings.night_brightness == 33 && settings.night_start_min == 1300 && settings.night_end_min == 400, "v3: night settings kept");
	int ok3 = settings.current_set == 6 && settings.start_set == 5 && settings.cycle_s == 37 && settings.set_enabled == (0x2F7u | SETS_NEW_ON);
	for(int i = 0; i < 10; i++) ok3 &= settings.set_order[i] == order3[i];
	for(int i = 10; i < N_SETS_SAVED; i++) ok3 &= settings.set_order[i] == i;
	CHECK(ok3, "v3: the set showing, start set, cycle, which sets are on and their order kept; the new sets after them");
	CHECK(strcmp(settings.weather_city, "Lisbon") == 0 && strcmp(settings.weather_lat, "38.72") == 0 && strcmp(settings.weather_lon, "-9.14") == 0 &&
	      settings.weather_enabled == 0 && settings.weather_units == 0 && settings.weather_dark == 0 && settings.weather_refresh_min == 25 &&
	      settings.weather_hl_cycle_s == 41, "v3: every weather setting kept");
	CHECK(settings.matrix_big_font == 1 && settings.matrix_line_min == 4 && settings.matrix_line_max == 11 && settings.matrix_speed_min == 2 &&
	      settings.matrix_speed_max == 9 && settings.matrix_frame_ms == 70 && settings.matrix_color == 0xC8840E && settings.matrix_head_color == 0x6AC67B,
	      "v3: every Matrix setting kept");
	CHECK(settings.rig_hz == 2 && settings.rig_middle == 3 && memcmp(settings.cams_page[0], c1, 5) == 0 && memcmp(settings.cams_page[1], c2, 5) == 0 &&
	      settings.cam_quality == 77 && settings.cam_fps == 12 && settings.viz_color == VIZ_RAINBOW, "v3: every rig setting kept");
	int d4 = settings.temp_unit == 0 && settings.date_order == 0 && settings.decimals == 1 && settings.try_mode == 0;
	for(int s4 = 0; s4 < N_SETS_SAVED; s4++)
		for(int o = 0; o < 5; o++)
			d4 &= settings.set_faces[s4][o] == o && settings.set_style[s4][o] == 0;
	for(int s4 = 0; s4 < N_SETS_SAVED; s4++)
		d4 &= settings.set_cycle_s[s4] == 0 && settings.set_brightness[s4] == 0 && settings.set_theme[s4] == 0;
	for(int o = 0; o < 5; o++)
		d4 &= settings.orb_bright[o] == 100 && settings.orb_gain[o][0] == 100 && settings.orb_gain[o][2] == 100;
	d4 &= settings.warn[0].on == 1 && settings.warn[0].limit_c == 90 && settings.warn[3].limit_c == 80 && settings.rules[0].kind == RULE_NONE &&
	      settings.custom[0].kind == CUSTOM_NONE;
	static const uint8_t zero9[9];
	d4 &= memcmp(settings.buttons[5], zero9, 9) == 0;
	d4 &= settings.link_speed == 0;   /* (screen link: normal, 2026-11-18) */
	CHECK(d4, "v3: the new settings at their defaults (faces 1-5, own styles, global cycle and brightness, no trims, C, warnings 90/85/70/80, no rules, no own faces, buttons as always)");
	CHECK(f->version == 4 && f->crc == crc32(f, offsetof(orbs_settings_t, crc)), "v3: saved back as version 4 with a good checksum");
	settings.set_faces[11][2] = 7; settings.rules[3].kind = RULE_TIME; settings.rules[3].start_min = 600; settings.custom[1].kind = CUSTOM_COUNTDOWN;
	strcpy(settings.custom[1].text, "Trip"); settings.orb_gain[4][1] = 93; settings.link_speed = 1;
	settings_save(); memset(&settings, 0, sizeof settings); settings_load();
	CHECK(settings.link_speed == 1 && settings.spare[0] == 0, "v4: the screen link (from the spare bytes) saves and reads back");
	settings.link_speed = 2; settings_save(); memset(&settings, 0, sizeof settings); settings_load();
	CHECK(settings_loaded_from_flash() && settings.link_speed == 0, "v4: a saved \"fastest\" screen link (removed 2026-11-19) loads as normal");
	CHECK(settings_loaded_from_flash() && settings.set_faces[11][2] == 7 && settings.rules[3].start_min == 600 && settings.custom[1].kind == CUSTOM_COUNTDOWN &&
	      strcmp(settings.custom[1].text, "Trip") == 0 && settings.orb_gain[4][1] == 93 && strcmp(settings.wifi_pass, "made-up-pass-333") == 0,
	      "v4: the new settings save and read back");
	fake_flash_sector[10] ^= 1;   /* damaged copy -> defaults, never garbage */
	settings_load();
	CHECK(!settings_loaded_from_flash() && settings.wifi_ssid[0] == 0, "a damaged copy falls back to the defaults");
	printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
	return fails != 0;
}
