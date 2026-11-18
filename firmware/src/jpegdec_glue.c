/* JPEGDEC (third_party/JPEGDEC, Larry Bank, Apache-2.0) for the rig's pictures on the XL's second core (pics.cpp),
   2026-11-16. Measured on the PC with the rig's own pictures: 2.1x TJpgDec's speed, and as close to libjpeg's decode
   (0.81 vs 1.03 levels of 255 on average, one screen colour step is ~8). Only the second core uses it. */
#define __LINUX__ 1                  /* (only picks the plain C headers instead of Arduino's) */
#define JD_RAM __attribute__((section(".time_critical.jpegdec")))   /* its decoding loops run from SRAM */
#include "../third_party/JPEGDEC/JPEGDEC.h"
#include "../third_party/JPEGDEC/jpeg.inl"
#include "jpegdec_glue.h"

static JPEGIMAGE img;
static orbs_jd_draw_fn user_draw;

static int draw(JPEGDRAW *d)
{
	return user_draw(d->x, d->y, d->iWidth, d->iHeight, d->pPixels, d->pUser);
}

int orbs_jd_open(const uint8_t *jpg, uint32_t len, orbs_jd_draw_fn fn, void *user, int *w, int *h, int *mcu_h)
{
	user_draw = fn;
	if(!JPEG_openRAM(&img, (uint8_t *)jpg, (int)len, draw))
		return 0;
	img.pUser = user;
	JPEG_setPixelType(&img, RGB565_LITTLE_ENDIAN);   /* (pics.cpp swaps the bytes itself for the screens) */
	*w = JPEG_getWidth(&img);
	*h = JPEG_getHeight(&img);
	int sub = JPEG_getSubSample(&img);
	*mcu_h = 8 * ((sub & 0x0F) ? (sub & 0x0F) : 1);
	return 1;
}

int orbs_jd_decode(void)
{
	int r = JPEG_decode(&img, 0, 0, 0);
	JPEG_close(&img);
	return r;
}

void orbs_jd_decode_abandon(void)
{
	JPEG_close(&img);
}

int orbs_jd_error(void)
{
	return JPEG_getLastError(&img);
}
