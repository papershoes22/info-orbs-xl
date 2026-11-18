#include "tft.h"

uint16_t *orb_fb[N_ORB_FB];
OrbDirty orb_dirty[N_ORB_FB] = { { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 } };

static inline uint16_t sw(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFF) | ((c & 0xFF) << 8)); }

template <typename T> static inline void transpose(T &a, T &b) { T t = a; a = b; b = t; }

/* TFT_eSPI's GC9A01 path: no windowed pushes, every pixel drawn with drawPixel */
#define GC9A01_DRIVER
#include "../third_party/tft_espi_smooth.inc"

void TFT_eSPI::mark(int orb, int y0, int y1)
{
	OrbDirty &d = orb_dirty[orb];
	if(d.y0 > d.y1)
	{
		d.y0 = (int16_t)y0;
		d.y1 = (int16_t)y1;
		return;
	}
	if(y0 < d.y0)
		d.y0 = (int16_t)y0;
	if(y1 > d.y1)
		d.y1 = (int16_t)y1;
}

void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t color)
{
	if((uint32_t)x >= ORB_W || (uint32_t)y >= ORB_H)
		return;
	uint16_t s = sw(color);
	for(int o = 0; o < N_ORB_FB; o++)
		if(targets() & (1u << o))
		{
			orb_fb[o][y * ORB_W + x] = s;
			mark(o, y, y);
		}
}

void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
	if(x < 0) { w += x; x = 0; }
	if(y < 0) { h += y; y = 0; }
	if(x + w > ORB_W) w = ORB_W - x;
	if(y + h > ORB_H) h = ORB_H - y;
	if(w <= 0 || h <= 0)
		return;
	uint16_t s = sw(color);
	for(int o = 0; o < N_ORB_FB; o++)
		if(targets() & (1u << o))
		{
			for(int j = y; j < y + h; j++)
			{
				uint16_t *p = orb_fb[o] + j * ORB_W + x;
				for(int i = 0; i < w; i++)
					p[i] = s;
			}
			mark(o, y, y + h - 1);
		}
}

void TFT_eSPI::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color)
{
	fillRect(x, y, w, 1, color);
}

void TFT_eSPI::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color)
{
	fillRect(x, y, 1, h, color);
}

uint16_t TFT_eSPI::readPixel(int32_t x, int32_t y)
{
	if((uint32_t)x >= ORB_W || (uint32_t)y >= ORB_H)
		return 0;
	for(int o = 0; o < N_ORB_FB; o++)
		if(targets() & (1u << o))
			return sw(orb_fb[o][y * ORB_W + x]);
	return 0;
}

void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data)
{
	for(int o = 0; o < N_ORB_FB; o++)
	{
		if(!(targets() & (1u << o)))
			continue;
		int ya = -1, yb = -1;
		for(int j = 0; j < h; j++)
		{
			int yy = y + j;
			if((uint32_t)yy >= ORB_H)
				continue;
			if(ya < 0)
				ya = yy;
			yb = yy;
			for(int i = 0; i < w; i++)
			{
				int xx = x + i;
				if((uint32_t)xx < ORB_W)
					orb_fb[o][yy * ORB_W + xx] = data[j * w + i];
			}
		}
		if(ya >= 0)
			mark(o, ya, yb);
	}
}

/* the viewport is always the whole screen here */
bool TFT_eSPI::clipWindow(int32_t *xs, int32_t *ys, int32_t *xe, int32_t *ye)
{
	if(*xs < 0) *xs = 0;
	if(*ys < 0) *ys = 0;
	if(*xe > ORB_W - 1) *xe = ORB_W - 1;
	if(*ye > ORB_H - 1) *ye = ORB_H - 1;
	return *xe >= *xs && *ye >= *ys;
}

/* ---- TFT_eSPI's classic font, its write() and drawChar() paths for font 1 with LOAD_GFXFF and no GFX font set
   (the orbs' build), transcribed: cursor wrap at the right edge (textwrapX on, textwrapY off), each dot a size x size
   square, the background filled when it differs from the text colour, the right-hand blank column included. */
#include "../third_party/glcdfont.c"

size_t TFT_eSPI::write(uint8_t utf8)
{
	uint16_t uniCode = utf8;                  /* (decodeUTF8: plain ASCII passes as itself) */
	if(!uniCode || utf8 == '\r')
		return 1;
	if(uniCode == '\n')
		uniCode += 22;
	uint16_t cwidth = 6, cheight = (uint16_t)(8 * textsize);
	if(utf8 == '\n')
	{
		cursor_y += cheight;
		cursor_x = 0;
	}
	else
	{
		if(cursor_x + cwidth * textsize > width())
		{
			cursor_y += cheight;
			cursor_x = 0;
		}
		drawChar(cursor_x, cursor_y, uniCode, textcolor, textbgcolor, textsize);
		cursor_x += 6 * textsize;
	}
	return 1;
}

void TFT_eSPI::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size)
{
	if(x >= ORB_W || y >= ORB_H || (x + 6 * size - 1) < 0 || (y + 8 * size - 1) < 0)
		return;
	if(c > 255)
		return;
	if(c > 175)
		c++;                                  /* (_cp437 off: the original's character set) */
	bool fillbg = (bg != color);
	for(int8_t i = 0; i < 6; i++)
	{
		uint8_t line = i == 5 ? 0x0 : font[(c * 5) + i];
		if(size == 1 && !fillbg)
		{
			for(int8_t j = 0; j < 8; j++)
			{
				if(line & 0x1)
					drawPixel(x + i, y + j, color);
				line >>= 1;
			}
		}
		else
		{
			for(int8_t j = 0; j < 8; j++)
			{
				if(line & 0x1)
					fillRect(x + (i * size), y + (j * size), size, size, color);
				else if(fillbg)
					fillRect(x + i * size, y + j * size, size, size, bg);
				line >>= 1;
			}
		}
	}
}
