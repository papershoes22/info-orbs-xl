#include "gfx.h"
#include <math.h>

uint16_t gfx_fb[SCR_W * SCR_H];

static inline uint16_t sw(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

/* x / 255 rounded, for x up to 255 * 255, without a division */
static inline int div255(int x)
{
	x += 128;
	return (x + (x >> 8)) >> 8;
}

/* a = 0..255: how much of c goes over what's there */
static inline void blend(int x, int y, uint16_t c, int a)
{
	if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H || a <= 0)
		return;
	if(a >= 255)
	{
		gfx_fb[y * SCR_W + x] = sw(c);
		return;
	}
	uint16_t d = sw(gfx_fb[y * SCR_W + x]);
	int r = div255(((c >> 11) & 31) * a + ((d >> 11) & 31) * (255 - a));
	int g = div255(((c >> 5) & 63) * a + ((d >> 5) & 63) * (255 - a));
	int b = div255((c & 31) * a + (d & 31) * (255 - a));
	gfx_fb[y * SCR_W + x] = sw((uint16_t)(r << 11 | g << 5 | b));
}

void gfx_clear(uint16_t c)
{
	uint16_t s = sw(c);
	for(int i = 0; i < SCR_W * SCR_H; i++)
		gfx_fb[i] = s;
}

/* the backdrop: dark blue-grey in the middle, darker to the rim. The colour comes
   from a table by squared distance / 16, made once, so there's no root or power per pixel. */
void gfx_backdrop(void)
{
	static uint16_t lut[1024];
	static bool made;
	if(!made)
	{
		for(int i = 0; i < 1024; i++)
		{
			float t = sqrtf(i * 16.0f + 8.0f) / 120.0f;
			if(t > 1)
				t = 1;
			t = powf(t, 1.6f);
			lut[i] = sw(RGB((int)(16 - 13 * t), (int)(21 - 17 * t), (int)(33 - 25 * t)));
		}
		made = true;
	}
	for(int y = 0; y < SCR_H; y++)
	{
		int dy2 = (2 * y + 1 - SCR_H) * (2 * y + 1 - SCR_H);     /* (2*dy)^2, dy from the centre between pixels */
		uint16_t *row = gfx_fb + y * SCR_W;
		for(int x = 0; x < SCR_W; x++)
		{
			int d2 = ((2 * x + 1 - SCR_W) * (2 * x + 1 - SCR_W) + dy2) >> 2;   /* dx^2 + dy^2 */
			int i = d2 >> 4;
			row[x] = lut[i > 1023 ? 1023 : i];
		}
	}
}

void gfx_rect(int x, int y, int w, int h, uint16_t c)
{
	for(int j = y; j < y + h; j++)
		for(int i = x; i < x + w; i++)
			blend(i, j, c, 255);
}

/* every pixel of the square round the ring, covered by how far its middle is inside (over one pixel of edge) */
void gfx_ring(float cx, float cy, float r_in, float r_out, uint16_t c)
{
	if(r_out <= r_in)
		return;
	int x0 = (int)floorf(cx - r_out - 1), x1 = (int)ceilf(cx + r_out + 1);
	int y0 = (int)floorf(cy - r_out - 1), y1 = (int)ceilf(cy + r_out + 1);
	for(int y = y0; y <= y1; y++)
		for(int x = x0; x <= x1; x++)
		{
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
			float cov = fminf(r_out - d + 0.5f, d - r_in + 0.5f);
			if(cov > 0)
				blend(x, y, c, cov >= 1 ? 255 : (int)(cov * 255));
		}
}

void gfx_disc(float cx, float cy, float r, uint16_t c)
{
	gfx_ring(cx, cy, -1.0f, r, c);
}

/* gfx_tri: a filled triangle with smooth edges (any winding) */
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c)
{
	float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
	if(area == 0)
		return;
	float s = area > 0 ? 1.0f : -1.0f;
	float ex[3][3];   /* each edge as a*x + b*y + c >= 0 inside, normalised so the value is a distance */
	float px[3] = { x0, x1, x2 }, py[3] = { y0, y1, y2 };
	for(int i = 0; i < 3; i++)
	{
		int j = (i + 1) % 3;
		float a = -(py[j] - py[i]) * s, b = (px[j] - px[i]) * s;
		float l = sqrtf(a * a + b * b);
		ex[i][0] = a / l;
		ex[i][1] = b / l;
		ex[i][2] = -(ex[i][0] * px[i] + ex[i][1] * py[i]);
	}
	int xa = (int)floorf(fminf(x0, fminf(x1, x2))) - 1, xb = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))) + 1;
	int ya = (int)floorf(fminf(y0, fminf(y1, y2))) - 1, yb = (int)ceilf(fmaxf(y0, fmaxf(y1, y2))) + 1;
	for(int y = ya; y <= yb; y++)
		for(int x = xa; x <= xb; x++)
		{
			float cov = 1;
			for(int i = 0; i < 3; i++)
			{
				float d = ex[i][0] * (x + 0.5f) + ex[i][1] * (y + 0.5f) + ex[i][2] + 0.5f;
				if(d < cov)
					cov = d;
			}
			if(cov > 0)
				blend(x, y, c, (int)(cov * 255));
		}
}

int gfx_text_w(const font_t *f, const char *s)
{
	int w = 0;
	for(; *s; s++)
		if(*s >= 32 && *s < 127)
			w += f->glyphs[*s - 32].adv;
	return w;
}

void gfx_text(const font_t *f, int x, int y, const char *s, uint16_t c)
{
	for(; *s; s++)
	{
		if(*s < 32 || *s >= 127)
			continue;
		const glyph_t *g = &f->glyphs[*s - 32];
		const uint8_t *p = f->bits + g->off;
		int row_bytes = (g->w + 1) / 2;
		for(int j = 0; j < g->h; j++)
			for(int i = 0; i < g->w; i++)
			{
				uint8_t v = p[j * row_bytes + i / 2];
				int a4 = (i & 1) ? (v & 15) : (v >> 4);
				blend(x + g->x + i, y + g->y + j, c, a4 * 17);
			}
		x += g->adv;
	}
}

void gfx_text_c(const font_t *f, int cx, int y, const char *s, uint16_t c)
{
	gfx_text(f, cx - gfx_text_w(f, s) / 2, y, s, c);
}

void gfx_text_mid(const font_t *f, int cx, int cy, const char *s, uint16_t c, char ref)
{
	const glyph_t *g = &f->glyphs[ref - 32];
	gfx_text_c(f, cx, cy - (g->y + g->h / 2), s, c);
}
