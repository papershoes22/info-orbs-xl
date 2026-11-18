#include "presets.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"

#define PRESET_MAGIC 0x54535250u          /* "PRST" */
/* the settings are the last sector, the camera login the one before; presets the eight before that */
#define PRESET_OFFSET(slot) (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE * (3 + (N_PRESETS - 1 - (slot))))

typedef struct {
	uint32_t magic, version;
	char name[24];
	orbs_settings_t s;
	uint32_t crc;
} preset_t;
_Static_assert(sizeof(preset_t) <= FLASH_SECTOR_SIZE, "a preset fits its sector");

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

static const preset_t *at(int slot)
{
	return (const preset_t *)(XIP_BASE + PRESET_OFFSET(slot));
}

static bool valid(int slot)
{
	if(slot < 0 || slot >= N_PRESETS)
		return false;
	const preset_t *p = at(slot);
	return p->magic == PRESET_MAGIC && p->version == p->s.version && p->crc == crc32(p, offsetof(preset_t, crc));
}

bool preset_name(int slot, char *out, int n)
{
	if(!valid(slot))
		return false;
	snprintf(out, (size_t)n, "%.23s", at(slot)->name);
	return true;
}

static uint8_t page[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));
static int write_slot;
static bool erase_only;
static void write_sector(void *arg)
{
	(void)arg;
	flash_range_erase(PRESET_OFFSET(write_slot), FLASH_SECTOR_SIZE);
	if(!erase_only)
		flash_range_program(PRESET_OFFSET(write_slot), page, FLASH_SECTOR_SIZE);
}

bool preset_save(int slot, const char *name, const orbs_settings_t *s)
{
	if(slot < 0 || slot >= N_PRESETS || !name || !*name || strlen(name) > 23)
		return false;
	static preset_t p;
	memset(&p, 0, sizeof p);
	p.magic = PRESET_MAGIC;
	p.s = *s;
	p.version = p.s.version;
	strcpy(p.name, name);
	/* never the secrets (nor the Wi-Fi name) in a preset */
	memset(p.s.wifi_ssid, 0, sizeof p.s.wifi_ssid);
	memset(p.s.wifi_pass, 0, sizeof p.s.wifi_pass);
	memset(p.s.owm_key, 0, sizeof p.s.owm_key);
	p.s.try_mode = 0;
	p.crc = crc32(&p, offsetof(preset_t, crc));
	memset(page, 0xFF, sizeof page);
	memcpy(page, &p, sizeof p);
	write_slot = slot;
	erase_only = false;
	int r = flash_safe_execute(write_sector, NULL, 500);
	memset(page, 0xFF, sizeof page);
	return r == PICO_OK && valid(slot) && memcmp(at(slot), &p, sizeof p) == 0;
}

bool preset_load(int slot, orbs_settings_t *into)
{
	if(!valid(slot) || at(slot)->version != into->version)
		return false;
	orbs_settings_t keep = *into;
	*into = at(slot)->s;
	/* what stays as it is: the secrets, the Wi-Fi, the time zone and server, the weather's place, the set showing */
	memcpy(into->wifi_ssid, keep.wifi_ssid, sizeof into->wifi_ssid);
	memcpy(into->wifi_pass, keep.wifi_pass, sizeof into->wifi_pass);
	memcpy(into->owm_key, keep.owm_key, sizeof into->owm_key);
	memcpy(into->tz_name, keep.tz_name, sizeof into->tz_name);
	memcpy(into->ntp_server, keep.ntp_server, sizeof into->ntp_server);
	memcpy(into->weather_lat, keep.weather_lat, sizeof into->weather_lat);
	memcpy(into->weather_lon, keep.weather_lon, sizeof into->weather_lon);
	memcpy(into->weather_city, keep.weather_city, sizeof into->weather_city);
	into->current_set = keep.current_set;
	into->try_mode = 0;
	memset(&keep, 0, sizeof keep);
	return true;
}

bool preset_delete(int slot)
{
	if(slot < 0 || slot >= N_PRESETS)
		return false;
	write_slot = slot;
	erase_only = true;
	int r = flash_safe_execute(write_sector, NULL, 500);
	return r == PICO_OK && !valid(slot);
}
