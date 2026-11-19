#include "settings.h"
#include <string.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"

#define SETTINGS_MAGIC 0x4F524253u      /* "ORBS" */
#define SETTINGS_VERSION 4u
_Static_assert(sizeof(orbs_settings_t) <= 4096, "settings fit the flash sector");
/* version 4's layout is fixed: its size and the crc's place may never move (a field from the spare bytes must keep both;
   2026-11-17 one didn't, and the XL started on its defaults and saved them over the saved settings) */
_Static_assert(sizeof(orbs_settings_t) == 1968, "settings version 4 keeps its size: take new fields from spare[], padded");
_Static_assert(offsetof(orbs_settings_t, crc) == 1964, "settings version 4 keeps the crc where it is");
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

orbs_settings_t settings;
static bool from_flash;

static uint32_t crc32(const void *p, size_t n)
{
	const uint8_t *b = p;
	uint32_t c = 0xFFFFFFFFu;
	while(n--)
	{
		c ^= *b++;
		for(int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

uint16_t rgb888_to_565(uint32_t c)
{
	return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

static uint32_t rgb565_to_888(uint16_t c)
{
	uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
	return (r * 255 / 31) << 16 | (g * 255 / 63) << 8 | (b * 255 / 31);
}

/* every setting but Wi-Fi and the key: the ones an ESP32 unit had on its old settings page (2026-11-04), and the XL's own sets */
static void defaults_but_secrets(void)
{
	strcpy(settings.tz_name, "America/New_York");
	strcpy(settings.ntp_server, "pool.ntp.org");
	settings.clock_type = 0;                 /* Normal */
	settings.clock_format = 2;               /* 12 h with AM/PM */
	settings.show_second_ticks = 1;
	settings.clock_shadowing = 1;
	settings.clock_color = 0x4AFF00;
	settings.clock_shadow_color = 0x201408;  /* (#201408: RGB565 0x20A1) */
	settings.clock_nixie_color = 0;
	settings.brightness = 255;
	memset(settings.rotation, 0, sizeof settings.rotation);
	settings.night_mode = 0;
	settings.night_brightness = 40;
	settings.night_start_min = 22 * 60;
	settings.night_end_min = 7 * 60;
	settings.current_set = 0;
	for(int i = 0; i < N_SETS_SAVED; i++)
		settings.set_order[i] = (uint8_t)i;
	settings.start_set = 0xFF;
	settings.set_enabled = ((1u << 10) - 1) | SETS_NEW_ON;  /* (today's ten; the new sets as each is built: SETS_NEW_ON) */
	settings.cycle_s = 0;
	strcpy(settings.weather_lat, "40.7128");        /* New York City until you pick your place on the settings page */
	strcpy(settings.weather_lon, "-74.0060");
	strcpy(settings.weather_city, "New York");
	settings.weather_enabled = 1;
	settings.weather_units = 1;              /* Fahrenheit */
	settings.weather_dark = 1;
	settings.weather_refresh_min = 10;       /* (the original's WEATHER_UPDATE_DELAY) */
	settings.weather_hl_cycle_s = 20;
	settings.matrix_big_font = 0;            /* Matrix (matrix_widget.h): small font, these colours */
	settings.matrix_color = rgb565_to_888(0x3502);
	settings.matrix_head_color = rgb565_to_888(0x6E2F);
	settings.matrix_line_min = 3;
	settings.matrix_line_max = 15;
	settings.matrix_speed_min = 3;
	settings.matrix_speed_max = 15;
	settings.matrix_frame_ms = 100;
	settings.rig_hz = 4;
	settings.rig_middle = 0;
	static const uint8_t p1[5] = { 0, 1, 2, 3, 4 }, p2[5] = { 5, 6, 0, 1, 2 };
	memcpy(settings.cams_page[0], p1, 5);
	memcpy(settings.cams_page[1], p2, 5);
	settings.cam_quality = 86;
	settings.cam_fps = 8;
	settings.viz_color = VIZ_ALBUM;
	/* version 4 (2026-11-17): every set on the global cycle and brightness, its own colours, faces 1-5 on orbs 1-5,
	   each face's own gauge style, each button as it always was; no trims; Celsius, month/day, the faces' decimals; no
	   rules; warnings at CPU 90, GPU 85, SSD 70, RAM 80 C as a colour change; no own faces */
	for(int s = 0; s < N_SETS_SAVED; s++)
		for(int o = 0; o < 5; o++)
			settings.set_faces[s][o] = (uint8_t)o;
	memset(settings.orb_bright, 100, sizeof settings.orb_bright);
	memset(settings.orb_gain, 100, sizeof settings.orb_gain);
	settings.temp_unit = 0;
	settings.date_order = 0;
	settings.decimals = 1;
	static const uint8_t lim[N_WARN] = { 90, 85, 70, 80 };
	for(int i = 0; i < N_WARN; i++)
	{
		settings.warn[i].on = 1;
		settings.warn[i].limit_c = lim[i];
		settings.warn[i].how = 0;
	}
}

static void defaults(void)
{
	memset(&settings, 0, sizeof settings);
	settings.magic = SETTINGS_MAGIC;
	settings.version = SETTINGS_VERSION;
	defaults_but_secrets();
}

void settings_defaults_keep_secrets(void)
{
	char ssid[33], pass[65], key[48];
	memcpy(ssid, settings.wifi_ssid, sizeof ssid);
	memcpy(pass, settings.wifi_pass, sizeof pass);
	memcpy(key, settings.owm_key, sizeof key);
	defaults();
	memcpy(settings.wifi_ssid, ssid, sizeof ssid);
	memcpy(settings.wifi_pass, pass, sizeof pass);
	memcpy(settings.owm_key, key, sizeof key);
	memset(pass, 0, sizeof pass);
	memset(key, 0, sizeof key);
}

/* version 1 exactly as it was stored (2026-11-16): its crc sits after 2 bytes of padding, so the layout is
   copied here rather than worked out */
typedef struct {
	uint32_t magic, version;
	char wifi_ssid[33];
	char wifi_pass[65];
	char owm_key[48];
	char tz_name[40];
	char ntp_server[40];
	uint8_t clock_type, clock_format, show_second_ticks, clock_shadowing;
	uint16_t clock_color, clock_shadow_color, clock_nixie_color;
	uint8_t orb_rotation, brightness, current_set;
	uint8_t pad[3];
	uint32_t crc;
} settings_v1_t;

/* version 2 exactly as it was stored (2026-11-16 to the settings page's firmware) */
typedef struct {
	uint32_t magic, version;
	char wifi_ssid[33];
	char wifi_pass[65];
	char owm_key[48];
	char tz_name[40];
	char ntp_server[40];
	uint8_t clock_type, clock_format, show_second_ticks, clock_shadowing;
	uint16_t clock_color, clock_shadow_color, clock_nixie_color;
	uint8_t orb_rotation, brightness, current_set;
	uint8_t pad[3];
	char weather_lat[16];
	char weather_lon[16];
	char weather_city[24];
	uint8_t weather_enabled, weather_units, weather_dark, weather_hl_cycle_s;
	uint32_t crc;
} settings_v2_t;
_Static_assert(offsetof(settings_v1_t, pad) + 3 == offsetof(settings_v2_t, weather_lat), "v1 fields = v2's first part");

/* version 3 exactly as it was stored (2026-11-17, the settings page's first firmware) */
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
} settings_v3_t;

/* version 3 into 4: every field it had, kept; its ten sets first in the order as they were, the new ones after */
static void from_v3(const settings_v3_t *o)
{
	defaults();
	memcpy(settings.wifi_ssid, o->wifi_ssid, sizeof settings.wifi_ssid);
	memcpy(settings.wifi_pass, o->wifi_pass, sizeof settings.wifi_pass);
	memcpy(settings.owm_key, o->owm_key, sizeof settings.owm_key);
	memcpy(settings.tz_name, o->tz_name, sizeof settings.tz_name);
	memcpy(settings.ntp_server, o->ntp_server, sizeof settings.ntp_server);
	settings.clock_type = o->clock_type;
	settings.clock_format = o->clock_format;
	settings.show_second_ticks = o->show_second_ticks;
	settings.clock_shadowing = o->clock_shadowing;
	settings.clock_color = o->clock_color;
	settings.clock_shadow_color = o->clock_shadow_color;
	settings.clock_nixie_color = o->clock_nixie_color;
	settings.brightness = o->brightness;
	memcpy(settings.rotation, o->rotation, sizeof settings.rotation);
	settings.night_mode = o->night_mode;
	settings.night_brightness = o->night_brightness;
	settings.night_start_min = o->night_start_min;
	settings.night_end_min = o->night_end_min;
	settings.current_set = o->current_set;
	/* the old order of the ten, then the new sets in their own order */
	int n = 0;
	for(int i = 0; i < 10; i++)
		settings.set_order[n++] = o->set_order[i] < 10 ? o->set_order[i] : (uint8_t)i;
	for(int i = 10; i < N_SETS_SAVED; i++)
		settings.set_order[n++] = (uint8_t)i;
	settings.start_set = o->start_set;
	settings.set_enabled = (uint32_t)(o->set_enabled & 0x3FF) | SETS_NEW_ON;
	settings.cycle_s = o->cycle_s;
	memcpy(settings.weather_lat, o->weather_lat, sizeof settings.weather_lat);
	memcpy(settings.weather_lon, o->weather_lon, sizeof settings.weather_lon);
	memcpy(settings.weather_city, o->weather_city, sizeof settings.weather_city);
	settings.weather_enabled = o->weather_enabled;
	settings.weather_units = o->weather_units;
	settings.weather_dark = o->weather_dark;
	settings.weather_refresh_min = o->weather_refresh_min;
	settings.weather_hl_cycle_s = o->weather_hl_cycle_s;
	settings.matrix_big_font = o->matrix_big_font;
	settings.matrix_line_min = o->matrix_line_min;
	settings.matrix_line_max = o->matrix_line_max;
	settings.matrix_speed_min = o->matrix_speed_min;
	settings.matrix_speed_max = o->matrix_speed_max;
	settings.matrix_frame_ms = o->matrix_frame_ms;
	settings.matrix_color = o->matrix_color;
	settings.matrix_head_color = o->matrix_head_color;
	settings.rig_hz = o->rig_hz;
	settings.rig_middle = o->rig_middle;
	memcpy(settings.cams_page, o->cams_page, sizeof settings.cams_page);
	settings.cam_quality = o->cam_quality;
	settings.cam_fps = o->cam_fps;
	settings.viz_color = o->viz_color;
}

/* the fields both old versions had, into version 3 (the rest at their defaults) */
static void from_old(const settings_v2_t *o, bool has_weather)
{
	defaults();
	memcpy(settings.wifi_ssid, o->wifi_ssid, sizeof settings.wifi_ssid);
	memcpy(settings.wifi_pass, o->wifi_pass, sizeof settings.wifi_pass);
	memcpy(settings.owm_key, o->owm_key, sizeof settings.owm_key);
	memcpy(settings.tz_name, o->tz_name, sizeof settings.tz_name);
	memcpy(settings.ntp_server, o->ntp_server, sizeof settings.ntp_server);
	settings.clock_type = o->clock_type;
	settings.clock_format = o->clock_format;
	settings.show_second_ticks = o->show_second_ticks;
	settings.clock_shadowing = o->clock_shadowing;
	settings.clock_color = rgb565_to_888(o->clock_color);
	settings.clock_shadow_color = rgb565_to_888(o->clock_shadow_color);
	settings.clock_nixie_color = o->clock_nixie_color ? rgb565_to_888(o->clock_nixie_color) : 0;
	settings.brightness = o->brightness < 10 ? 10 : o->brightness;
	memset(settings.rotation, o->orb_rotation & 3, sizeof settings.rotation);
	settings.current_set = o->current_set < N_SETS_SAVED ? o->current_set : 0;
	if(has_weather)
	{
		memcpy(settings.weather_lat, o->weather_lat, sizeof settings.weather_lat);
		memcpy(settings.weather_lon, o->weather_lon, sizeof settings.weather_lon);
		memcpy(settings.weather_city, o->weather_city, sizeof settings.weather_city);
		settings.weather_enabled = o->weather_enabled;
		settings.weather_units = o->weather_units;
		settings.weather_dark = o->weather_dark;
		settings.weather_hl_cycle_s = o->weather_hl_cycle_s;
	}
}

static void terminate_strings(void)
{
	settings.wifi_ssid[sizeof settings.wifi_ssid - 1] = 0;
	settings.wifi_pass[sizeof settings.wifi_pass - 1] = 0;
	settings.owm_key[sizeof settings.owm_key - 1] = 0;
	settings.tz_name[sizeof settings.tz_name - 1] = 0;
	settings.ntp_server[sizeof settings.ntp_server - 1] = 0;
	settings.weather_lat[sizeof settings.weather_lat - 1] = 0;
	settings.weather_lon[sizeof settings.weather_lon - 1] = 0;
	settings.weather_city[sizeof settings.weather_city - 1] = 0;
	for(int i = 0; i < N_CUSTOM; i++)
	{
		settings.custom[i].title[sizeof settings.custom[i].title - 1] = 0;
		settings.custom[i].text[sizeof settings.custom[i].text - 1] = 0;
	}
	settings.try_mode = 0;                   /* (never comes back on from flash) */
}

void settings_load(void)
{
	from_flash = false;
	const uint8_t *raw = (const uint8_t *)(XIP_BASE + SETTINGS_OFFSET);
	const settings_v1_t *v1 = (const settings_v1_t *)raw;
	const settings_v2_t *v2 = (const settings_v2_t *)raw;
	const settings_v3_t *v3 = (const settings_v3_t *)raw;
	const orbs_settings_t *f = (const orbs_settings_t *)raw;
	if(v1->magic == SETTINGS_MAGIC && v1->version == 1u && v1->crc == crc32(v1, offsetof(settings_v1_t, crc)))
	{
		/* version 1: Wi-Fi, key and clock kept, the rest at their defaults, saved as version 4 */
		settings_v2_t tmp;
		memset(&tmp, 0, sizeof tmp);
		memcpy(&tmp, v1, offsetof(settings_v1_t, pad) + 3);
		from_old(&tmp, false);
		memset(&tmp, 0, sizeof tmp);
	}
	else if(v2->magic == SETTINGS_MAGIC && v2->version == 2u && v2->crc == crc32(v2, offsetof(settings_v2_t, crc)))
		from_old(v2, true);                  /* version 2: everything it had kept, saved as version 4 */
	else if(v3->magic == SETTINGS_MAGIC && v3->version == 3u && v3->crc == crc32(v3, offsetof(settings_v3_t, crc)))
		from_v3(v3);                         /* version 3: everything it had kept, saved as version 4 */
	else if(f->magic == SETTINGS_MAGIC && f->version == SETTINGS_VERSION &&
	        f->crc == crc32(f, offsetof(orbs_settings_t, crc)))
	{
		memcpy(&settings, f, sizeof settings);
		terminate_strings();
		if(settings.link_speed > 1)
			settings.link_speed = 0;         /* (the removed "fastest" screen link, 2026-11-19: back to normal) */
		from_flash = true;
		return;
	}
	else
	{
		defaults();
		return;
	}
	terminate_strings();
	from_flash = true;
	settings_save();
}

bool settings_loaded_from_flash(void) { return from_flash; }

static uint8_t page[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

static void write_sector(void *arg)
{
	(void)arg;
	flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
	flash_range_program(SETTINGS_OFFSET, page, FLASH_SECTOR_SIZE);
}

bool settings_save(void)
{
	settings.magic = SETTINGS_MAGIC;
	settings.version = SETTINGS_VERSION;
	settings.crc = crc32(&settings, offsetof(orbs_settings_t, crc));
	const uint8_t *f = (const uint8_t *)(XIP_BASE + SETTINGS_OFFSET);
	if(memcmp(f, &settings, sizeof settings) == 0)
		return true;
	memset(page, 0xFF, sizeof page);
	memcpy(page, &settings, sizeof settings);
	/* the radio's interrupts and the other core held off while the sector is rewritten (about 50 ms) */
	int r = flash_safe_execute(write_sector, NULL, 500);
	memset(page, 0xFF, sizeof page);         /* (no copy of the password left in RAM beyond the settings) */
	from_flash = r == PICO_OK && memcmp(f, &settings, sizeof settings) == 0;
	return from_flash;
}

void settings_wipe(void)
{
	defaults();
	settings_save();
}
