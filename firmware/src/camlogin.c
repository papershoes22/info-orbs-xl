#include "camlogin.h"
#include <string.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"

#define CAMLOGIN_OFFSET (PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE)   /* the sector before the settings */
#define CAMLOGIN_MAGIC 0x43414D4Cu                                         /* "CAML" */
#define CAMLOGIN_VERSION 1u

camlogin_t camlogin;
static uint8_t page[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

static uint32_t crc32(const void *data, size_t n)
{
	const uint8_t *p = data;
	uint32_t c = 0xFFFFFFFFu;
	while(n--)
	{
		c ^= *p++;
		for(int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

void camlogin_load(void)
{
	const camlogin_t *f = (const camlogin_t *)(XIP_BASE + CAMLOGIN_OFFSET);
	if(f->magic == CAMLOGIN_MAGIC && f->version == CAMLOGIN_VERSION && f->crc == crc32(f, offsetof(camlogin_t, crc)))
	{
		memcpy(&camlogin, f, sizeof camlogin);
		camlogin.host[sizeof camlogin.host - 1] = 0;
		camlogin.user[sizeof camlogin.user - 1] = 0;
		camlogin.pass[sizeof camlogin.pass - 1] = 0;
	}
	else
		memset(&camlogin, 0, sizeof camlogin);
}

bool camlogin_set(void)
{
	return camlogin.host[0] && camlogin.user[0] && camlogin.pass[0] && camlogin.port;
}

static void write_sector(void *arg)
{
	(void)arg;
	flash_range_erase(CAMLOGIN_OFFSET, FLASH_SECTOR_SIZE);
	flash_range_program(CAMLOGIN_OFFSET, page, FLASH_SECTOR_SIZE);
}

bool camlogin_save(void)
{
	camlogin.magic = CAMLOGIN_MAGIC;
	camlogin.version = CAMLOGIN_VERSION;
	camlogin.crc = crc32(&camlogin, offsetof(camlogin_t, crc));
	const uint8_t *f = (const uint8_t *)(XIP_BASE + CAMLOGIN_OFFSET);
	if(memcmp(f, &camlogin, sizeof camlogin) == 0)
		return true;
	memset(page, 0xFF, sizeof page);
	memcpy(page, &camlogin, sizeof camlogin);
	int r = flash_safe_execute(write_sector, NULL, 500);
	memset(page, 0xFF, sizeof page);          /* (no second copy of the password left in RAM) */
	return r == PICO_OK && memcmp(f, &camlogin, sizeof camlogin) == 0;
}

void camlogin_wipe(void)
{
	memset(&camlogin, 0, sizeof camlogin);
	camlogin_save();
}
