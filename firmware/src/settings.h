/* The orbs' settings, kept in the XL's flash (its last 4 KB sector, which loading new firmware never touches),
   2026-11-16. The defaults are the settings an ESP32 unit had on its old settings page
   (2026-11-04). The Wi-Fi password and the weather key live only here: they come over USB from the PC
   (tools/orbs_xl.py wifi / key; the Wi-Fi also from the setup hotspot) and are never sent back, printed or logged. Anyone holding the XL could read them out
   of its flash (as with the ESP32 before). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* version 4 (2026-11-17): room for 24 sets; per set its cycle time,
   brightness, colour theme, which face on which orb, gauge styles and what each button press does; per-orb brightness
   and colour trim; formats; rules; temperature warnings; own faces (messages and countdowns). Version 3 copies upgrade
   with everything kept (settings.c), the new settings at their defaults. Colours as 0xRRGGBB. */
enum { N_SETS_SAVED = 24, N_RULES = 12, N_CUSTOM = 8, N_WARN = 4 };
/* the new sets switched on as each is built (part 3): bit per set number from 10 */
#define SETS_NEW_ON (1u << 21)      /* Messages (2026-11-17) */

/* a rule (the page's Rules): a time window on some days -> a set; or an event from the rig -> a set (or dark) */
enum { RULE_NONE = 0, RULE_TIME = 1, RULE_EVENT = 2 };
enum { EVENT_MUSIC = 1, EVENT_GAME = 2, EVENT_RIG_SLEEP = 3, EVENT_PERSON_DOOR = 4, EVENT_PRINT_DONE = 5 };
typedef struct {
	uint8_t kind;                /* RULE_... */
	uint8_t set;                 /* the set it shows (0xFE = screens dark) */
	uint8_t days;                /* RULE_TIME: bit 0 Sunday .. bit 6 Saturday */
	uint8_t event;               /* RULE_EVENT: EVENT_... */
	uint16_t start_min, end_min; /* RULE_TIME: minutes after midnight (end < start: past midnight) */
	uint16_t secs;               /* RULE_EVENT: how long it stays, then back (0 = while the event lasts) */
	uint16_t pad;
} orbs_rule_t;

/* an own face: a message or a countdown, drawn by the XL itself (the Messages set) or by the rig (any of its sets) */
enum { CUSTOM_NONE = 0, CUSTOM_MESSAGE = 1, CUSTOM_COUNTDOWN = 2 };
typedef struct {
	uint8_t kind;                /* CUSTOM_... */
	uint8_t size;                /* 0 auto, 1 small, 2 medium, 3 big */
	uint8_t pad[2];
	uint32_t color, bg;          /* 0xRRGGBB */
	uint32_t target;             /* CUSTOM_COUNTDOWN: Unix time it counts down to */
	char title[24];              /* small line above */
	char text[48];               /* the message (a countdown: what happens then) */
} orbs_custom_t;

/* a temperature warning (CPU, GPU, SSD, RAM): past its limit the face's colour changes or pulses */
enum { WARN_CPU, WARN_GPU, WARN_SSD, WARN_RAM };
typedef struct {
	uint8_t on, limit_c, how, pad;   /* how: 0 colour, 1 pulse */
} orbs_warn_t;
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
	uint8_t set_order[N_SETS_SAVED];   /* set numbers in the rotation's order */
	uint8_t start_set;           /* the set at start-up, 0xFF = the last one shown */
	uint32_t set_enabled;        /* bit per set number */
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
	/* ---- version 4 ---- */
	uint16_t set_cycle_s[N_SETS_SAVED];    /* this set's turn in the auto-cycle, seconds (0 = the global cycle_s) */
	uint8_t set_brightness[N_SETS_SAVED];  /* this set's brightness 10-255 (0 = the global one) */
	uint8_t set_theme[N_SETS_SAVED];       /* colour theme (0 = the set's own colours) */
	uint8_t set_faces[N_SETS_SAVED][5];    /* which face on each orb (the set's catalogue; 0xFF = blank) */
	uint8_t set_style[N_SETS_SAVED][5];    /* gauge style on each orb: 0 the face's own, 1 ring, 2 needle, 3 bar, 4 number */
	uint8_t buttons[N_SETS_SAVED][9];      /* left/middle/right x short/medium/long -> an action (0 = the set's own) */
	uint8_t orb_bright[5];                 /* per orb, percent of the brightness (50-100) */
	uint8_t orb_gain[5][3];                /* per orb, red/green/blue percent (50-100): colour trim */
	uint8_t temp_unit;                     /* 0 Celsius, 1 Fahrenheit (the rig's faces; the weather has its own) */
	uint8_t date_order;                    /* 0 month/day, 1 day/month, 2 year-month-day */
	uint8_t decimals;                      /* 0 none, 1 as the face likes, 2 one more */
	uint8_t try_mode;                      /* (not saved as on: the page's "try before saving") */
	orbs_rule_t rules[N_RULES];
	orbs_warn_t warn[N_WARN];
	orbs_custom_t custom[N_CUSTOM];
	uint16_t kwh_price_m;                  /* electricity price per kWh in thousandths ($0.155 = 155; 0 = not set): the
	                                          rig's ENERGY face's cost an hour (2026-11-17, from the spare bytes) */
	uint16_t pad_k;                        /* (keeps sets_seen on a 4-byte boundary: no hidden padding) */
	uint32_t sets_seen;                    /* the sets the XL has met (0 = the first ten + Messages): a set the rig's
	                                          catalogue names for the first time starts switched ON, once; switching it
	                                          off on the page is kept (2026-11-17, from the spare bytes) */
	uint8_t link_speed;                    /* the screens' picture speed: 0 normal (41.7 MHz), 1 fast (62.5), 2 fastest
	                                          (125); the page's Screens > Screen link (2026-11-18, from the spare bytes) */
	uint8_t pad_l[3];                      /* (keeps the rest where it was) */
	uint8_t spare[116];          /* room for new settings without a new version (zeros = their defaults). A field taken
	                                from here must leave the struct's size and the crc's place exactly as they are
	                                (settings.c checks both at build time): 2026-11-17 a uint32 added without padding
	                                moved the crc by 2 bytes and the XL started on its defaults. */
	uint32_t crc;
} orbs_settings_t;
#define VIZ_ALBUM 0xFF000000u
#define VIZ_RAINBOW 0xFE000000u

extern orbs_settings_t settings;

void settings_load(void);            /* defaults if the flash copy is missing or damaged */
bool settings_save(void);            /* writes the flash sector (only if something changed) */
void settings_wipe(void);            /* back to the defaults, Wi-Fi and key gone (left button held 15 s) */
void settings_defaults_keep_secrets(void);   /* every setting back to its default; Wi-Fi, key kept (the page's "Defaults") */
uint16_t rgb888_to_565(uint32_t c);
bool settings_loaded_from_flash(void);

#ifdef __cplusplus
}
#endif
