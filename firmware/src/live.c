/* Live faces: see live.h. Everything here works on one orb's face; nothing touches hardware (the XL hands in a PSRAM
   pointer through its no-cache window and a fetch that copies from it; the PC hands in plain memory). */
#include "live.h"
#include <string.h>
#include <math.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define LIVE_RAM(f) __not_in_flash_func(f)   /* (on the XL the hot functions run from SRAM: PSRAM traffic would push
                                               them out of the 16 KB cache, as it did the picture decoder) */
#else
#define LIVE_RAM(f) f
#endif

/* copies from the pack (and the mask store) into a small local buffer: memcpy here, the XL may give its own */
static void fetch_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }

/* where an update's time goes (us per part: base row, each layer kind, masks, output), for the "?" LIVEPROF line */
static uint32_t prof_dummy(void) { return 0; }
uint32_t (*live_now_us)(void) = prof_dummy;
uint32_t live_prof[12];
void (*live_fetch)(void *dst, const void *src, size_t n) = fetch_memcpy;

static inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* the layers' small tables (colour tables, the glow fall-off, a dot's shape) kept in SRAM after the first row needs
   them: fetched per row from PSRAM they cost most of an update (2026-11-17: 26 ms for 5,200 pixels). Keyed by
   address and size; live_gen goes up whenever a table may have changed (a face loaded, a colour table replaced). */
#define AUX_MAX (1536 + 20 + 4096)
#define AUX_N 3
static uint32_t live_gen = 1;
typedef struct { float r, g, b; } px3;

/* each core's own working memory (2026-11-18: both cores build rows of the same face at once, the first core helping
   the second; nothing here may be shared between them). The XL tells which core runs it; the PC is always core 0. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/platform.h"
#define LIVE_CORE() ((int)get_core_num())
#define LIVE_CORES 2
#else
#define LIVE_CORE() 0
#define LIVE_CORES 1
#endif
typedef struct {
	uint8_t rowbuf[LIVE_W * 7 + 8];        /* one row of a layer's pixels, fetched */
	uint8_t rowbuf2[LIVE_W * 7 + 8];
	uint8_t scrollbuf[(240 + 8) * 4];
	uint8_t sega[LIVE_W], segid[LIVE_W];   /* CELLS: this row's segment map */
	px3 acc[LIVE_W];                       /* the row being built */
	uint8_t basebuf[LIVE_W * 3];
	uint8_t aux_mem[AUX_N][AUX_MAX];       /* the small tables kept in SRAM (aux_get) */
	struct { const uint8_t *addr; uint32_t n, gen, used; } aux_key[AUX_N];
	uint32_t aux_tick;
	float glut[2][1536];                   /* GAUGE colour tables as floats (gauge_lut) */
	struct { const uint8_t *addr; uint32_t gen; } glut_key[2];
	int glut_next;
} live_scratch_t;
static live_scratch_t live_scratch[LIVE_CORES];
#define rowbuf (S_->rowbuf)
#define rowbuf2 (S_->rowbuf2)
#define scrollbuf (S_->scrollbuf)
#define sega (S_->sega)
#define segid (S_->segid)
#define acc_rows (S_->acc)
#define basebuf (S_->basebuf)

static const uint8_t *aux_get(const uint8_t *addr, uint32_t n)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	uint8_t (*aux_mem)[AUX_MAX] = S_->aux_mem;
	__typeof__(S_->aux_key[0]) *aux_key = S_->aux_key;
	if(n > AUX_MAX)
		n = AUX_MAX;
	int lru = 0;
	for(int i = 0; i < AUX_N; i++)
	{
		if(aux_key[i].addr == addr && aux_key[i].n >= n && aux_key[i].gen == live_gen)
		{
			aux_key[i].used = ++S_->aux_tick;
			return aux_mem[i];
		}
		if(aux_key[i].used < aux_key[lru].used)
			lru = i;
	}
	live_fetch(aux_mem[lru], addr, n);
	aux_key[lru].addr = addr;
	aux_key[lru].n = n;
	aux_key[lru].gen = live_gen;
	aux_key[lru].used = ++S_->aux_tick;
	return aux_mem[lru];
}
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

#define DEG2RAD 0.017453292519943295f

/* NEEDLE parameters (aux): the segment from len_in to len_out px along the value's angle, its width, glow strength
   (spread: the layer's sigma), the hub's radius; the needle's and the hub's colours */
typedef struct __attribute__((packed)) {
	float len_in, len_out, width, glow, hub;
	uint8_t r, g, b, hr, hg, hb, pad[2];
} needle_par_t;

/* atan2 in degrees 0..360 (0 = 3 o'clock, clockwise with y down), ~1e-5 rad: the arcs' soft edges need it fine */
static float LIVE_RAM(atan2_deg)(float y, float x)
{
	float ax = fabsf(x), ay = fabsf(y);
	float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
	if(mx < 1e-9f)
		return 0.0f;
	float a = mn / mx, s = a * a;
	float r = ((((-0.0040540580f * s + 0.0218612288f) * s - 0.0559098861f) * s + 0.0964200441f) * s - 0.1390853351f) * s;
	r = ((r + 0.1994653599f) * s - 0.3332985605f) * s * a + a;
	if(ay > ax) r = 1.57079637f - r;
	if(x < 0) r = 3.14159274f - r;
	if(y < 0) r = -r;
	float d = r * 57.29577951f;
	return d < 0 ? d + 360.0f : d;
}

/* the normal distribution's cumulative (how much of a blurred edge is past a point), by a table over -4..4 sigma */
static float phi_tab[129];
static bool phi_ready;
static void phi_init(void)
{
	for(int i = 0; i <= 128; i++)
	{
		float x = -4.0f + 8.0f * (float)i / 128.0f;
		phi_tab[i] = 0.5f * erfcf(-x / 1.41421356f);
	}
	phi_ready = true;
}
static inline float LIVE_RAM(phi)(float x)
{
	if(x <= -4.0f) return 0.0f;
	if(x >= 4.0f) return 1.0f;
	float t = (x + 4.0f) * 16.0f;
	int i = (int)t;
	float fr = t - (float)i;
	return phi_tab[i] + (phi_tab[i + 1] - phi_tab[i]) * fr;
}

/* ---------------------------------------------------------------------------------------------- loading */
bool live_load(live_face_t *f, const uint8_t *pack, uint32_t len, uint8_t *mask_store, const char **err)
{
	if(!phi_ready)
		phi_init();
	f->pack = NULL;
	if(len < 32 || rd32(pack) != LIVE_MAGIC) { *err = "not a face pack"; return false; }
	uint16_t ver = (uint16_t)(pack[4] | pack[5] << 8);
	if(ver != 1) { *err = "face pack version"; return false; }
	int n = pack[8] | pack[9] << 8;
	uint32_t base_off = rd32(pack + 12), layers_off = rd32(pack + 16), total = rd32(pack + 20);
	if(n > LIVE_MAX_LAYERS || total != len || base_off + LIVE_W * LIVE_W * 3 > len ||
	   layers_off + (uint32_t)n * sizeof(live_layer_rec_t) > len)
	{
		*err = "face pack sizes";
		return false;
	}
	for(int i = 0; i < n; i++)
	{
		live_layer_rec_t *l = &f->layer[i];
		memcpy(l, pack + layers_off + (uint32_t)i * sizeof(live_layer_rec_t), sizeof *l);
		if(l->x < 0 || l->y < 0 || l->w < 0 || l->h < 0 || l->x + l->w > LIVE_W || l->y + l->h > LIVE_W ||
		   l->data_off > len || l->data_len > len - l->data_off || l->aux_off > len ||
		   (l->s0 >= LIVE_MAX_SLOTS && l->s0 != LIVE_NO_SLOT) || (l->s1 >= LIVE_MAX_SLOTS && l->s1 != LIVE_NO_SLOT) ||
		   (l->s2 >= LIVE_MAX_SLOTS && l->s2 != LIVE_NO_SLOT) || (l->s3 >= LIVE_MAX_SLOTS && l->s3 != LIVE_NO_SLOT) ||
		   (l->kind == LIVE_CELLS && (l->s0 == LIVE_NO_SLOT || l->s0 + l->aux8 > LIVE_MAX_SLOTS)) ||
		   (l->kind == LIVE_SPRITE && (l->s0 == LIVE_NO_SLOT || l->s1 == LIVE_NO_SLOT)) ||
		   (l->kind == LIVE_CHART && (l->s0 >= LIVE_MAX_ARRAYS || l->s1 == LIVE_NO_SLOT)) ||
		   (l->kind == LIVE_DOT && (l->s0 == LIVE_NO_SLOT || l->s1 == LIVE_NO_SLOT || l->aux_off + 12 > len)) ||
		   ((l->kind == LIVE_TINT || l->kind == LIVE_CELLS || l->kind == LIVE_CHART) && l->aux_off + 768 > len) ||
		   (l->kind == LIVE_NEEDLE && (l->s0 == LIVE_NO_SLOT || !l->aux_off || l->aux_off + sizeof(needle_par_t) > len)) ||
		   (l->kind == LIVE_GAUGE && l->aux_off + 1536 > len) ||
		   (l->kind == LIVE_SCROLL && (l->s0 == LIVE_NO_SLOT || l->aux_off + 8 > len)))
		{
			*err = "face pack layer out of range";
			return false;
		}
		if((l->kind == LIVE_SPAN || l->kind == LIVE_CELLS || l->kind == LIVE_TINT || l->kind == LIVE_GAUGE) &&
		   l->data_len < (uint32_t)l->h * 4)
		{
			*err = "face pack row table";
			return false;
		}
		if(l->kind == LIVE_SCROLL)
		{
			uint16_t sw, sh, room;
			memcpy(&sw, pack + l->aux_off, 2); memcpy(&sh, pack + l->aux_off + 2, 2); memcpy(&room, pack + l->aux_off + 4, 2);
			if(sw > room || sh != l->h || room > 512 || l->aux_off + 8 + (uint32_t)room * sh * 4 > len)
			{
				*err = "face pack scroll picture";
				return false;
			}
		}
		if(l->kind == LIVE_SPRITE && (l->r_mid < 1 || l->width < 1 || l->r_mid > 128 || l->width > 128 ||
		                              l->data_len < (uint32_t)l->r_mid * (uint32_t)l->width * 7))
		{
			*err = "face pack sprite";
			return false;
		}
	}
	f->n_layers = n;
	/* for each arc: how far before and past its tip a change can show. With a measured glow fall-off: where it differs
	   from full (before) and from nothing (past) by more than 1.5 %, plus a pixel; else the cap and 3.2 blurs */
	for(int i = 0; i < n; i++)
	{
		const live_layer_rec_t *l = &f->layer[i];
		float cap = (l->flags & LIVE_F_ROUND_CAP) ? l->width * 0.5f : 0.0f;
		f->m_in[i] = cap + 3.2f * l->sigma + 1.0f;
		f->m_out[i] = cap + 3.2f * l->sigma + 1.0f;
		if(l->kind == LIVE_GAUGE && l->aux_off + 1556 <= len)
		{
			uint16_t nd, nr;
			float d0, ds;
			memcpy(&nd, pack + l->aux_off + 1536, 2); memcpy(&nr, pack + l->aux_off + 1538, 2);
			memcpy(&d0, pack + l->aux_off + 1540, 4); memcpy(&ds, pack + l->aux_off + 1544, 4);
			if(nd >= 2 && nr >= 2 && (uint32_t)nd * nr <= 4096 && l->aux_off + 1556 + (uint32_t)nd * nr <= len && ds > 0)
			{
				const uint8_t *t = pack + l->aux_off + 1556;
				int first = nd, last = -1;
				for(int u = 0; u < nd; u++)
					for(int w = 0; w < nr; w++)
					{
						uint8_t v = t[w * nd + u];
						if(v < 251 && u < first) first = u;
						if(v > 3 && u > last) last = u;
					}
				/* "past" counts from the cap's far edge: the tip is at past = -cap */
				float din = d0 + ds * (float)first, dout = d0 + ds * (float)(last < 0 ? 0 : last);
				f->m_in[i] = fmaxf(-din - cap, 0.0f) + 1.5f;
				f->m_out[i] = fmaxf(dout + cap, cap) + 1.5f;
			}
		}
	}
	f->face_id = (uint16_t)(pack[6] | pack[7] << 8);
	f->pack_len = len;
	f->base_rw = (uint8_t *)pack + base_off;
	f->mask_store = mask_store;
	memset(f->slot, 0, sizeof f->slot);
	memset(f->array_n, 0, sizeof f->array_n);
	memset(f->mask, 0, sizeof f->mask);
	if(!f->brightness)
		f->brightness = 255;
	f->pack = pack;
	live_gen++;
	live_mark_all(f);
	return true;
}

void live_unload(live_face_t *f)
{
	f->pack = NULL;
	f->n_dirty = 0;
}

/* ---------------------------------------------------------------------------------------------- dirty rectangles */
void live_mark_all(live_face_t *f)
{
	f->dirty[0] = (live_rect_t){ 0, 0, LIVE_W, LIVE_W };
	f->n_dirty = 1;
}

void live_mark(live_face_t *f, int x0, int y0, int x1, int y1)
{
	x0 = imax(0, x0); y0 = imax(0, y0); x1 = imin(LIVE_W, x1); y1 = imin(LIVE_W, y1);
	if(x1 <= x0 || y1 <= y0)
		return;
	/* joined to one it overlaps or nearly touches when the union costs little more than the two */
	for(int i = 0; i < f->n_dirty; i++)
	{
		live_rect_t *r = &f->dirty[i];
		int ux0 = imin(r->x0, x0), uy0 = imin(r->y0, y0), ux1 = imax(r->x1, x1), uy1 = imax(r->y1, y1);
		long ua = (long)(ux1 - ux0) * (uy1 - uy0);
		long sa = (long)(r->x1 - r->x0) * (r->y1 - r->y0) + (long)(x1 - x0) * (y1 - y0);
#ifndef LIVE_MERGE_SLACK
/* two changed boxes are drawn as one only when that adds at most 64 pixels (2026-11-18; was a quarter of their area
   plus 256): the CPU LOAD face's 24 cells drew 20 % fewer pixels a frame, measured by tools/live_bench.py on this
   PC's readings and a busy made-up load, every frame still the same as a full redraw */
#define LIVE_MERGE_SLACK(sa) (64)
#endif
		if(ua <= sa + LIVE_MERGE_SLACK(sa))
		{
			*r = (live_rect_t){ (int16_t)ux0, (int16_t)uy0, (int16_t)ux1, (int16_t)uy1 };
			/* (it may now overlap another: those are joined on the next mark or drawn twice, both fine) */
			return;
		}
	}
	if(f->n_dirty < LIVE_MAX_RECTS)
	{
		f->dirty[f->n_dirty++] = (live_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1 };
		return;
	}
	/* full: into the one that grows least */
	int best = 0;
	long best_g = -1;
	for(int i = 0; i < f->n_dirty; i++)
	{
		live_rect_t *r = &f->dirty[i];
		long g = (long)(imax(r->x1, x1) - imin(r->x0, x0)) * (imax(r->y1, y1) - imin(r->y0, y0)) -
		         (long)(r->x1 - r->x0) * (r->y1 - r->y0);
		if(best_g < 0 || g < best_g) { best_g = g; best = i; }
	}
	live_rect_t *r = &f->dirty[best];
	*r = (live_rect_t){ (int16_t)imin(r->x0, x0), (int16_t)imin(r->y0, y0), (int16_t)imax(r->x1, x1), (int16_t)imax(r->y1, y1) };
}

/* the box around a ring's piece between two angles (degrees, any order), clipped to the layer */
static void mark_sector(live_face_t *f, const live_layer_rec_t *l, float a1, float a2, float r_in, float r_out)
{
	if(a2 < a1) { float t = a1; a1 = a2; a2 = t; }
	float bx0, by0, bx1, by1;
	if(a2 - a1 >= 360.0f)
	{
		bx0 = l->cx - r_out; bx1 = l->cx + r_out; by0 = l->cy - r_out; by1 = l->cy + r_out;
	}
	else
	{
		bx0 = by0 = 1e9f; bx1 = by1 = -1e9f;
		float angs[8];
		int na = 0;
		angs[na++] = a1;
		angs[na++] = a2;
		for(int k = (int)floorf(a1 / 90.0f) + 1; (float)k * 90.0f < a2 && na < 8; k++)
			angs[na++] = (float)k * 90.0f;
		for(int i = 0; i < na; i++)
		{
			float c = cosf(angs[i] * DEG2RAD), s = sinf(angs[i] * DEG2RAD);
			for(int j = 0; j < 2; j++)
			{
				float r = j ? r_out : r_in;
				float x = l->cx + r * c, y = l->cy + r * s;
				if(i >= 2 && !j) continue;              /* (the axis points only matter on the outside) */
				bx0 = fminf(bx0, x); bx1 = fmaxf(bx1, x); by0 = fminf(by0, y); by1 = fmaxf(by1, y);
			}
		}
	}
	int x0 = imax(l->x, (int)floorf(bx0) - 2), y0 = imax(l->y, (int)floorf(by0) - 2);
	int x1 = imin(l->x + l->w, (int)ceilf(bx1) + 3), y1 = imin(l->y + l->h, (int)ceilf(by1) + 3);
	live_mark(f, x0, y0, x1, y1);
}

static void sprite_box(const live_face_t *f, const live_layer_rec_t *l, const uint16_t *slot, int *x0, int *y0, int *x1, int *y1)
{
	(void)f;
	float sw = l->r_mid, sh = l->width;            /* (a sprite's own size; its box is where it can go) */
	float tx = (float)slot[l->s0] / 16.0f - sw / 2.0f, ty = (float)slot[l->s1] / 16.0f - sh / 2.0f;
	*x0 = (int)floorf(tx) - 1; *y0 = (int)floorf(ty) - 1;
	*x1 = (int)ceilf(tx + sw) + 2; *y1 = (int)ceilf(ty + sh) + 2;
}

/* what a slot change touches, for the layers that use it (called with the old values still in f->slot) */
static void mark_slot_change(live_face_t *f, int s, uint16_t nv)
{
	for(int i = 0; i < f->n_layers; i++)
	{
		const live_layer_rec_t *l = &f->layer[i];
		switch(l->kind)
		{
		case LIVE_SPAN:
		case LIVE_TINT:
		case LIVE_GAUGE:
			if(l->s0 != s)
				break;
			if(l->clip == LIVE_CLIP_ARC)
			{
				float ov = (float)f->slot[s] / 65535.0f, vv = (float)nv / 65535.0f;
				float k = 57.29578f / fmaxf(l->r_mid, 1.0f);           /* px along the arc -> degrees */
				float mi = f->m_in[i] * k, mo = f->m_out[i] * k;
				float lo = fminf(ov, vv), hi = fmaxf(ov, vv);
				if(ov == 0.0f || vv == 0.0f)
				{
					/* the arc appears or goes: the start cap's side too */
					mark_sector(f, l, l->a0 - (l->width * 0.5f + 3.2f * l->sigma + 1.0f) * k, l->a0 + l->sweep * hi + mo,
					            l->r_in, l->r_out);
				}
				else
					mark_sector(f, l, l->a0 + l->sweep * lo - mi, l->a0 + l->sweep * hi + mo, l->r_in, l->r_out);
			}
			else if(l->clip == LIVE_CLIP_BAR_X || l->clip == LIVE_CLIP_BAR_Y)
			{
				float ov = (float)f->slot[s] / 65535.0f, vv = (float)nv / 65535.0f;
				float m = 3.2f * l->sigma + 2.0f;
				if(l->clip == LIVE_CLIP_BAR_X)
				{
					float e1 = l->x + l->w * ov, e2 = l->x + l->w * vv;
					if(l->flags & LIVE_F_BAR_REVERSE) { e1 = l->x + l->w * (1 - ov); e2 = l->x + l->w * (1 - vv); }
					live_mark(f, (int)(fminf(e1, e2) - m), l->y, (int)(fmaxf(e1, e2) + m) + 1, l->y + l->h);
				}
				else
				{
					float e1 = l->y + l->h * (1 - ov), e2 = l->y + l->h * (1 - vv);
					live_mark(f, l->x, (int)(fminf(e1, e2) - m), l->x + l->w, (int)(fmaxf(e1, e2) + m) + 1);
				}
			}
			else
				live_mark(f, l->x, l->y, l->x + l->w, l->y + l->h);
			break;
		case LIVE_SPRITE:
			if(l->s0 == s || l->s1 == s || l->s2 == s)
			{
				int x0, y0, x1, y1;
				sprite_box(f, l, f->slot, &x0, &y0, &x1, &y1);
				live_mark(f, x0, y0, x1, y1);
				uint16_t tmp[LIVE_MAX_SLOTS];
				memcpy(tmp, f->slot, sizeof tmp);
				tmp[s] = nv;
				sprite_box(f, l, tmp, &x0, &y0, &x1, &y1);
				live_mark(f, x0, y0, x1, y1);
			}
			break;
		case LIVE_CELLS:
		{
			int nseg = l->aux8;
			if(s < l->s0 || s >= l->s0 + nseg)
				break;
			if((f->slot[s] >> 8) == (nv >> 8))
				break;                          /* (same colour: the tables have 256 steps) */
			const uint8_t *aux = f->pack + l->aux_off;
			int ng = aux[0];
			const uint8_t *seg = aux + 4 + ng * 1536 + (s - l->s0) * 16;
			uint8_t sb[16];
			live_fetch(sb, seg, 16);
			int16_t gx, gy, gw, gh;
			memcpy(&gx, sb + 2, 2); memcpy(&gy, sb + 4, 2); memcpy(&gw, sb + 6, 2); memcpy(&gh, sb + 8, 2);
			live_mark(f, gx, gy, gx + gw, gy + gh);
			break;
		}
		case LIVE_DOT:
			if(l->s0 == s || l->s1 == s || l->s2 == s)
			{
				uint8_t dh[12];
				live_fetch(dh, f->pack + l->aux_off, 12);
				uint16_t nb;
				float bin;
				memcpy(&nb, dh + 6, 2);
				memcpy(&bin, dh + 8, 4);
				float rad = (float)nb * bin + 1.0f;
				for(int k = 0; k < 2; k++)
				{
					float x = (float)(k ? (l->s0 == s ? nv : f->slot[l->s0]) : f->slot[l->s0]) / 16.0f;
					float y = (float)(k ? (l->s1 == s ? nv : f->slot[l->s1]) : f->slot[l->s1]) / 16.0f;
					live_mark(f, (int)floorf(x - rad), (int)floorf(y - rad), (int)ceilf(x + rad) + 1, (int)ceilf(y + rad) + 1);
				}
			}
			break;
		case LIVE_SCROLL:
			if(l->s0 == s)
				live_mark(f, l->x, l->y, l->x + l->w, l->y + l->h);
			break;
		case LIVE_CHART:
			if(l->s1 == s || l->s2 == s)
				live_mark(f, l->x, l->y, l->x + l->w, l->y + l->h);
			break;
		case LIVE_NEEDLE:
			if(l->s0 == s)
			{
				/* where the needle was and where it goes (its segment, width, glow and hub), not the whole dial
				   (2026-11-17: the gauge styles' needle, 40 times a second) */
				needle_par_t np;
				live_fetch(&np, f->pack + l->aux_off, sizeof np);
				float m = np.width * 0.5f + (np.glow > 0 ? 3.2f * l->sigma : 0.0f) + 2.0f;
				for(int w = 0; w < 2; w++)
				{
					float v = (float)(w ? nv : f->slot[s]) / 65535.0f;
					float a = (l->a0 + l->sweep * v) * DEG2RAD, ux = cosf(a), uy = sinf(a);
					float xa = l->cx + ux * np.len_in, ya = l->cy + uy * np.len_in;
					float xb = l->cx + ux * np.len_out, yb = l->cy + uy * np.len_out;
					live_mark(f, (int)(fminf(xa, xb) - m), (int)(fminf(ya, yb) - m), (int)(fmaxf(xa, xb) + m) + 1,
					          (int)(fmaxf(ya, yb) + m) + 1);
				}
				if(np.hub > 0)
					live_mark(f, (int)(l->cx - np.hub - 2), (int)(l->cy - np.hub - 2), (int)(l->cx + np.hub + 3),
					          (int)(l->cy + np.hub + 3));
			}
			break;
		}
	}
}

void live_set_slots(live_face_t *f, int first, int n, const uint16_t *v)
{
	for(int i = 0; i < n; i++)
	{
		int s = first + i;
		if(s < 0 || s >= LIVE_MAX_SLOTS || f->slot[s] == v[i])
			continue;
		if(f->pack)
			mark_slot_change(f, s, v[i]);
		f->slot[s] = v[i];
	}
}

void live_set_array(live_face_t *f, int idx, const uint8_t *v, int n)
{
	if(idx < 0 || idx >= LIVE_MAX_ARRAYS || n < 0)
		return;
	if(n > LIVE_ARRAY_MAX)
		n = LIVE_ARRAY_MAX;
	memcpy(f->array[idx], v, (size_t)n);
	f->array_n[idx] = (uint8_t)n;
	for(int i = 0; f->pack && i < f->n_layers; i++)
		if(f->layer[i].kind == LIVE_CHART && f->layer[i].s0 == idx)
			live_mark(f, f->layer[i].x, f->layer[i].y, f->layer[i].x + f->layer[i].w, f->layer[i].y + f->layer[i].h);
}

bool live_set_mask(live_face_t *f, int slot, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, const uint8_t *alpha)
{
	if(slot < 0 || slot >= LIVE_MAX_MASKS || w < 0 || h < 0 || (long)w * h > LIVE_MASK_MAX ||
	   (w && (x < 0 || y < 0 || x + w > LIVE_W || y + h > LIVE_W)))
		return false;
	live_mask_t *m = &f->mask[slot];
	if(m->w)
		live_mark(f, m->x, m->y, m->x + m->w, m->y + m->h);
	m->w = 0;
	if(!w)
		return true;
	m->off = (uint32_t)slot * LIVE_MASK_MAX;
	memcpy(f->mask_store + m->off, alpha, (size_t)w * h);
	m->x = (int16_t)x; m->y = (int16_t)y; m->h = (int16_t)h; m->r = r; m->g = g; m->b = b;
	m->w = (int16_t)w;
	live_mark(f, x, y, x + w, y + h);
	return true;
}

bool live_patch(live_face_t *f, int x, int y, int w, int h, const uint8_t *rgb)
{
	if(!f->pack || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > LIVE_W || y + h > LIVE_W)
		return false;
	for(int j = 0; j < h; j++)
		memcpy(f->base_rw + ((size_t)(y + j) * LIVE_W + x) * 3, rgb + (size_t)j * w * 3, (size_t)w * 3);
	live_mark(f, x, y, x + w, y + h);
	return true;
}

bool live_set_lut(live_face_t *f, int layer, const uint8_t *rgb768)
{
	/* (a GAUGE's two tables: layer | 0x80 is its value table, plain its track's) */
	int which = layer & 0x80 ? 1 : 0;
	layer &= 0x7F;
	if(!f->pack || layer < 0 || layer >= f->n_layers ||
	   !(f->layer[layer].kind == LIVE_TINT || f->layer[layer].kind == LIVE_GAUGE) || (which && f->layer[layer].kind != LIVE_GAUGE))
		return false;
	memcpy((uint8_t *)f->pack + f->layer[layer].aux_off + which * 768, rgb768, 768);
	live_gen++;
	const live_layer_rec_t *l = &f->layer[layer];
	live_mark(f, l->x, l->y, l->x + l->w, l->y + l->h);
	return true;
}

bool live_set_image(live_face_t *f, int layer, int w, const uint8_t *px, uint32_t len)
{
	if(!f->pack || layer < 0 || layer >= f->n_layers || f->layer[layer].kind != LIVE_SCROLL)
		return false;
	const live_layer_rec_t *l = &f->layer[layer];
	uint8_t *aux = (uint8_t *)f->pack + l->aux_off;
	uint16_t room, h = (uint16_t)l->h;
	memcpy(&room, aux + 4, 2);
	if(w < l->w || w > room || len < (uint32_t)w * h * 4)
		return false;
	uint16_t w16 = (uint16_t)w;
	memcpy(aux, &w16, 2);
	/* rows kept at the room's stride, so a narrower picture fits without moving anything else */
	for(int j = 0; j < h; j++)
		memcpy(aux + 8 + (uint32_t)j * room * 4, px + (uint32_t)j * w * 4, (size_t)w * 4);
	live_mark(f, l->x, l->y, l->x + l->w, l->y + l->h);
	return true;
}

/* ---------------------------------------------------------------------------------------------- building rows */

/* per layer, worked out once per compose call from the slots */
typedef struct {
	float v, av, tipx, tipy, edge;
	bool on;
	const uint8_t *tail;                       /* GAUGE: the measured glow fall-off (in SRAM), NULL = the formula */
	int tail_nd, tail_nr;
	float tail_d0, tail_ds, tail_r0, tail_rs, tail_ids, tail_irs;
	float isigma, isweep;
	bool sigma_off;                            /* (no glow at this pixel: the glow's coverage isn't worked out) */
} clip_t;

static void clip_prepare(const live_face_t *f, const live_layer_rec_t *l, clip_t *c)
{
	c->v = l->s0 == LIVE_NO_SLOT ? 1.0f : (float)f->slot[l->s0] / 65535.0f;
	c->on = true;
	c->tail = NULL;
	c->tail_nd = c->tail_nr = 0;
	c->tail_d0 = c->tail_ds = c->tail_r0 = c->tail_rs = c->tail_ids = c->tail_irs = 0.0f;
	c->isigma = l->sigma > 0.0f ? 1.0f / l->sigma : 0.0f;
	c->sigma_off = false;
	c->isweep = l->sweep > 0.0f ? 1.0f / l->sweep : 0.0f;
	if(l->clip == LIVE_CLIP_ARC)
	{
		c->on = c->v > 0.0f;
		c->av = l->sweep * c->v;
		float a = (l->a0 + c->av) * DEG2RAD;
		c->tipx = l->cx + l->r_mid * cosf(a);
		c->tipy = l->cy + l->r_mid * sinf(a);
	}
	else if(l->clip == LIVE_CLIP_BAR_X)
		c->edge = (l->flags & LIVE_F_BAR_REVERSE) ? l->x + l->w * (1 - c->v) : l->x + l->w * c->v;
	else if(l->clip == LIVE_CLIP_BAR_Y)
		c->edge = l->y + l->h * (1 - c->v);
}

/* an arc's coverage at a pixel whose radius and angle past the start are known */
static inline void LIVE_RAM(arc_cov)(const live_layer_rec_t *l, const clip_t *c, float x, float y, float r, float rel,
                                     bool before, float *cs, float *cg)
{
	{
		if(before)
		{
			*cs = *cg = 1.0f;                       /* (just before the start: the start cap, always there) */
			return;
		}
		float s = clampf((c->av - rel) * DEG2RAD * fmaxf(r, 1.0f) + 0.5f, 0.0f, 1.0f);
		float past = (rel - c->av) * DEG2RAD * l->r_mid;
		if(l->flags & LIVE_F_ROUND_CAP)
		{
			float hw = l->width * 0.5f;
			if(s < 1.0f && past < hw + 1.5f)          /* (the cap only matters just past the tip) */
			{
				float tx = x - c->tipx, ty = y - c->tipy;
				float d2 = tx * tx + ty * ty;
				if(d2 < (hw + 1.0f) * (hw + 1.0f))
				{
					float cap = clampf(hw - sqrtf(d2) + 0.5f, 0.0f, 1.0f);
					if(cap > s) s = cap;
				}
			}
			past -= hw;
		}
		*cs = s;
		if(c->sigma_off)
			*cg = 0.0f;
		else if(!c->tail)
			*cg = l->sigma > 0.0f ? phi(-past * c->isigma) : (past <= 0.0f ? 1.0f : 0.0f);
		else
		{
			/* the glow's real fall-off past the tip, measured by the rig: by distance past the tip and across the band */
			float u = (past - c->tail_d0) * c->tail_ids, w = (r - l->r_mid - c->tail_r0) * c->tail_irs;
			if(u <= 0.0f) { *cg = 1.0f; return; }
			if(u >= (float)(c->tail_nd - 1)) { *cg = 0.0f; return; }
			w = clampf(w, 0.0f, (float)(c->tail_nr - 1) - 0.001f);
			int iu = (int)u, iw = (int)w;
			float fu = u - (float)iu, fw = w - (float)iw;
			const uint8_t *t0 = c->tail + iw * c->tail_nd + iu, *t1 = t0 + c->tail_nd;
			float v = ((float)t0[0] * (1 - fu) + (float)t0[1] * fu) * (1 - fw) + ((float)t1[0] * (1 - fu) + (float)t1[1] * fu) * fw;
			*cg = v * (1.0f / 255.0f);
		}
	}
}

/* how much of a pixel shows: the solid part (cs) and the glow (cg) */
static inline void LIVE_RAM(clip_cov)(const live_layer_rec_t *l, const clip_t *c, float x, float y, float *cs, float *cg)
{
	switch(l->clip)
	{
	case LIVE_CLIP_ARC:
	{
		float dx = x - l->cx, dy = y - l->cy;
		float r = sqrtf(dx * dx + dy * dy);
		float rel = atan2_deg(dy, dx) - l->a0;
		while(rel < 0) rel += 360.0f;
		while(rel >= 360.0f) rel -= 360.0f;
		arc_cov(l, c, x, y, r, rel, rel > (l->sweep + 360.0f) * 0.5f, cs, cg);
		return;
	}
	case LIVE_CLIP_BAR_X:
	{
		float d = (l->flags & LIVE_F_BAR_REVERSE) ? x - c->edge : c->edge - x;
		*cs = clampf(d + 0.5f, 0.0f, 1.0f);
		*cg = l->sigma > 0.0f ? phi(d / l->sigma) : *cs;
		return;
	}
	case LIVE_CLIP_BAR_Y:
	{
		float d = y - c->edge;
		*cs = clampf(d + 0.5f, 0.0f, 1.0f);
		*cg = l->sigma > 0.0f ? phi(d / l->sigma) : *cs;
		return;
	}
	default:
		*cs = *cg = c->v;
		return;
	}
}


static void LIVE_RAM(do_span)(const live_face_t *f, const live_layer_rec_t *l, int y, int x0, int x1, px3 *acc)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	clip_t c;
	clip_prepare(f, l, &c);
	if(!c.on && l->clip == LIVE_CLIP_ARC)
		return;
	uint8_t hdr[4];
	live_fetch(hdr, f->pack + l->data_off + (uint32_t)(y - l->y) * 4, 4);
	uint32_t ro = rd32(hdr);
	if(ro == 0xFFFFFFFFu)
		return;
	const uint8_t *row = f->pack + l->data_off + ro;
	uint8_t ns;
	live_fetch(&ns, row, 1);
	row++;
	float fy = (float)y + 0.5f;
	for(int k = 0; k < ns; k++)
	{
		uint8_t sh[2];
		live_fetch(sh, row, 2);
		int sx = l->x + sh[0], sl = sh[1];
		const uint8_t *pix = row + 2;
		row += 2 + (uint32_t)sl * 7;
		int a = imax(sx, x0), b = imin(sx + sl, x1);
		if(a >= b)
			continue;
		live_fetch(rowbuf, pix + (uint32_t)(a - sx) * 7, (size_t)(b - a) * 7);
		const uint8_t *p = rowbuf;
		for(int x = a; x < b; x++, p += 7)
		{
			float cs, cg;
			clip_cov(l, &c, (float)x + 0.5f, fy, &cs, &cg);
			if(cs <= 0.0f && cg <= 0.0f)
				continue;
			/* the glow light first, then the solid over it (as the rig paints: glow, then the shape) */
			px3 *o = &acc[x - x0];
			float ka = 1.0f - (float)p[6] * (1.0f / 255.0f) * cs;
			o->r = (o->r + (float)p[3] * cg) * ka + (float)p[0] * cs;
			o->g = (o->g + (float)p[4] * cg) * ka + (float)p[1] * cs;
			o->b = (o->b + (float)p[5] * cg) * ka + (float)p[2] * cs;
		}
	}
}

/* a TINT layer: (alpha, glow) per pixel, its colour from the table by the pixel's place along the arc / across */
static void LIVE_RAM(do_tint)(const live_face_t *f, const live_layer_rec_t *l, int y, int x0, int x1, px3 *acc)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	clip_t c;
	clip_prepare(f, l, &c);
	if(!c.on && l->clip == LIVE_CLIP_ARC)
		return;
	uint8_t hdr[4];
	live_fetch(hdr, f->pack + l->data_off + (uint32_t)(y - l->y) * 4, 4);
	uint32_t ro = rd32(hdr);
	if(ro == 0xFFFFFFFFu)
		return;
	const uint8_t *lut = aux_get(f->pack + l->aux_off, 768);
	const uint8_t *row = f->pack + l->data_off + ro;
	uint8_t ns;
	live_fetch(&ns, row, 1);
	row++;
	float fy = (float)y + 0.5f;
	for(int k = 0; k < ns; k++)
	{
		uint8_t sh[2];
		live_fetch(sh, row, 2);
		int sx = l->x + sh[0], sl = sh[1];
		const uint8_t *pix = row + 2;
		row += 2 + (uint32_t)sl * 2;
		int a = imax(sx, x0), b = imin(sx + sl, x1);
		if(a >= b)
			continue;
		live_fetch(rowbuf, pix + (uint32_t)(a - sx) * 2, (size_t)(b - a) * 2);
		const uint8_t *p = rowbuf;
		for(int x = a; x < b; x++, p += 2)
		{
			float cs, cg;
			float fx = (float)x + 0.5f;
			clip_cov(l, &c, fx, fy, &cs, &cg);
			if(cs <= 0.0f && cg <= 0.0f)
				continue;
			float t;
			switch(l->aux8)
			{
			case LIVE_T_ANGLE:
			{
				float rel = atan2_deg(fy - l->cy, fx - l->cx) - l->a0;
				while(rel < 0) rel += 360.0f;
				while(rel >= 360.0f) rel -= 360.0f;
				t = rel > (l->sweep + 360.0f) * 0.5f ? 0.0f : rel / l->sweep;
				break;
			}
			case LIVE_T_X: t = (fx - (float)l->x) / (float)(l->w > 1 ? l->w : 1); break;
			case LIVE_T_Y: t = ((float)(l->y + l->h) - fy) / (float)(l->h > 1 ? l->h : 1); break;
			default: t = 0.0f;
			}
			int ti = (int)(clampf(t, 0.0f, 1.0f) * 255.0f + 0.5f);
			const uint8_t *col = lut + ti * 3;
			/* the glow (its own, not hidden yet) under the part of the solid that shows */
			float as = (float)p[0] * (1.0f / 255.0f) * cs, g = (float)p[1] * (1.0f / 255.0f) * cg;
			px3 *o = &acc[x - x0];
			float ka = 1.0f - as;
			o->r = (o->r + (float)col[0] * g) * ka + (float)col[0] * as;
			o->g = (o->g + (float)col[1] * g) * ka + (float)col[1] * as;
			o->b = (o->b + (float)col[2] * g) * ka + (float)col[2] * as;
		}
	}
}

/* a GAUGE: per pixel (track alpha, value alpha, glow); tables: track colours then value colours */
static inline float LIVE_RAM(t_of)(const live_layer_rec_t *l, float fx, float fy)
{
	switch(l->aux8)
	{
	case LIVE_T_ANGLE:
	{
		float rel = atan2_deg(fy - l->cy, fx - l->cx) - l->a0;
		while(rel < 0) rel += 360.0f;
		while(rel >= 360.0f) rel -= 360.0f;
		return rel > (l->sweep + 360.0f) * 0.5f ? 0.0f : rel / l->sweep;
	}
	case LIVE_T_X: return (fx - (float)l->x) / (float)(l->w > 1 ? l->w : 1);
	case LIVE_T_Y: return ((float)(l->y + l->h) - fy) / (float)(l->h > 1 ? l->h : 1);
	default: return 0.0f;
	}
}

static const float *gauge_lut(const uint8_t *addr, const uint8_t *aux)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	float (*glut)[1536] = S_->glut;
	__typeof__(S_->glut_key[0]) *glut_key = S_->glut_key;
	for(int i = 0; i < 2; i++)
		if(glut_key[i].addr == addr && glut_key[i].gen == live_gen)
			return glut[i];
	int i = S_->glut_next;
	S_->glut_next ^= 1;
	for(int k = 0; k < 1536; k++)
		glut[i][k] = (float)aux[k];
	glut_key[i].addr = addr;
	glut_key[i].gen = live_gen;
	return glut[i];
}

static void LIVE_RAM(do_gauge)(const live_face_t *f, const live_layer_rec_t *l, int y, int x0, int x1, px3 *acc)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	clip_t c;
	clip_prepare(f, l, &c);
	uint8_t hdr[4];
	live_fetch(hdr, f->pack + l->data_off + (uint32_t)(y - l->y) * 4, 4);
	uint32_t ro = rd32(hdr);
	if(ro == 0xFFFFFFFFu)
		return;
	/* its two colour tables and (if it has one) its measured glow fall-off, from the SRAM cache */
	uint32_t auxn = 1536;
	uint16_t nd = 0, nr = 0;
	if(l->aux_off + 1536 + 20 <= f->pack_len)
	{
		uint8_t th[4];
		live_fetch(th, f->pack + l->aux_off + 1536, 4);
		memcpy(&nd, th, 2); memcpy(&nr, th + 2, 2);
		if(nd >= 2 && nr >= 2 && (uint32_t)nd * nr <= 4096 && l->aux_off + 1556 + (uint32_t)nd * nr <= f->pack_len)
			auxn = 1556 + (uint32_t)nd * nr;
		else
			nd = nr = 0;
	}
	const uint8_t *aux = aux_get(f->pack + l->aux_off, auxn);
	if(nd)
	{
		memcpy(&c.tail_d0, aux + 1536 + 4, 4); memcpy(&c.tail_ds, aux + 1536 + 8, 4);
		memcpy(&c.tail_r0, aux + 1536 + 12, 4); memcpy(&c.tail_rs, aux + 1536 + 16, 4);
		c.tail = aux + 1556; c.tail_nd = nd; c.tail_nr = nr;
		c.tail_ids = 1.0f / c.tail_ds; c.tail_irs = 1.0f / c.tail_rs;
	}
	const uint8_t *row = f->pack + l->data_off + ro;
	uint8_t ns;
	live_fetch(&ns, row, 1);
	row++;
	float fy = (float)y + 0.5f;
	bool arc = l->clip == LIVE_CLIP_ARC, angle_t = l->aux8 == LIVE_T_ANGLE;
	float rstep = (l->flags & LIVE_F_R_HALF) ? 0.5f : 0.25f;
	/* (for pixels with no glow: the same clip without its fall-off, made once per row, not copied per pixel) */
	clip_t cng = c;
	cng.tail = NULL;
	cng.sigma_off = true;
	/* its two colour tables as floats, kept while they stay the same (the same numbers the bytes give: no pixel
	   changes; 2026-11-18, made per row they cost more than they saved) */
	const float *ctf = gauge_lut(f->pack + l->aux_off, aux), *cvf = ctf + 768;
	for(int k = 0; k < ns; k++)
	{
		uint8_t sh[2];
		live_fetch(sh, row, 2);
		int sx = l->x + sh[0], sl = sh[1];
		const uint8_t *pix = row + 2;
		row += 2 + (uint32_t)sl * 8;
		int a = imax(sx, x0), b = imin(sx + sl, x1);
		if(a >= b)
			continue;
		live_fetch(rowbuf, pix + (uint32_t)(a - sx) * 8, (size_t)(b - a) * 8);
		const uint8_t *p = rowbuf;
		for(int x = a; x < b; x++, p += 8)
		{
			float fx = (float)x + 0.5f;
			float cs = 0.0f, cg = 0.0f, t;
			if(arc && angle_t)
			{
				/* the pixel's place along the arc and its radius, worked out once by the rig */
				unsigned tq = (unsigned)p[5] | (unsigned)p[6] << 8;
				bool before = tq == 0xFFFFu;
				t = before ? 0.0f : (float)tq * (1.0f / 65534.0f);
				float rel = t * l->sweep;
				float r = l->r_in + (float)p[7] * rstep;
				if(c.on)
					arc_cov(l, (!p[2] && c.tail) ? &cng : &c, fx, fy, r, rel, before, &cs, &cg);   /* (no glow here: its fall-off isn't needed) */
			}
			else
			{
				if(c.on || !arc)
					clip_cov(l, &c, fx, fy, &cs, &cg);
				t = t_of(l, fx, fy);
			}
			int ti = (int)(clampf(t, 0.0f, 1.0f) * 255.0f + 0.5f);
			const float *ct = ctf + ti * 3, *cv = cvf + ti * 3;
			const float kk = 1.0f / 255.0f;
			float at = (float)p[0] * kk, av = (float)p[1] * kk, g = (float)p[2] * kk * cg;
			px3 *o = &acc[x - x0];
			float ptv = (float)p[3] * kk, pgv = (float)p[4] * kk * cg;
			/* lit, term for term as the rig's track-then-glow-then-band at its double size, shrunk: ptv = track x band,
			   pgv = glow x band, each shrunk from the double size (a product of two shrunk edges isn't the same) */
			float ko = 1 - at - av + ptv, kt = at - ptv, kv = g - pgv + av;
			if(cs >= 1.0f)
			{
				o->r = o->r * ko + ct[0] * kt + cv[0] * kv;
				o->g = o->g * ko + ct[1] * kt + cv[1] * kv;
				o->b = o->b * ko + ct[2] * kt + cv[2] * kv;
				continue;
			}
			/* unlit: the track, the glow's tail over it */
			float ur = o->r * (1 - at) + ct[0] * at + cv[0] * g;
			float ug = o->g * (1 - at) + ct[1] * at + cv[1] * g;
			float ub = o->b * (1 - at) + ct[2] * at + cv[2] * g;
			if(cs <= 0.0f)
			{
				o->r = ur; o->g = ug; o->b = ub;
				continue;
			}
			float lr = o->r * ko + ct[0] * kt + cv[0] * kv;
			float lg = o->g * ko + ct[1] * kt + cv[1] * kv;
			float lb = o->b * ko + ct[2] * kt + cv[2] * kv;
			o->r = ur + (lr - ur) * cs;
			o->g = ug + (lg - ug) * cs;
			o->b = ub + (lb - ub) * cs;
		}
	}
}

static void LIVE_RAM(do_sprite)(const live_face_t *f, const live_layer_rec_t *l, int y, int x0, int x1, px3 *acc)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	float vis = l->s2 == LIVE_NO_SLOT ? 1.0f : (float)f->slot[l->s2] / 65535.0f;
	if(vis <= 0.0f)
		return;
	int sw = (int)l->r_mid, sh = (int)l->width;
	float tx = (float)f->slot[l->s0] / 16.0f - (float)sw / 2.0f, ty = (float)f->slot[l->s1] / 16.0f - (float)sh / 2.0f;
	/* the sprite's pixel (u, v) covers screen (tx + u .. tx + u + 1): a screen pixel's centre samples it bilinearly */
	float v = (float)y + 0.5f - ty - 0.5f;
	int v0 = (int)floorf(v);
	float fv = v - (float)v0;
	if(v0 + 1 < 0 || v0 >= sh)
		return;
	int ua = imax(x0, (int)floorf(tx) - 1), ub = imin(x1, (int)ceilf(tx + (float)sw) + 1);
	if(ua >= ub)
		return;
	const uint8_t *data = f->pack + l->data_off;
	bool r0 = v0 >= 0, r1 = v0 + 1 < sh;
	if(r0) live_fetch(rowbuf, data + (uint32_t)v0 * sw * 7, (size_t)sw * 7);
	if(r1) live_fetch(rowbuf2, data + (uint32_t)(v0 + 1) * sw * 7, (size_t)sw * 7);
	for(int x = ua; x < ub; x++)
	{
		float u = (float)x + 0.5f - tx - 0.5f;
		int u0 = (int)floorf(u);
		float fu = u - (float)u0;
		float s[7] = { 0 };
		for(int j = 0; j < 2; j++)
		{
			if(!(j ? r1 : r0)) continue;
			const uint8_t *rb = j ? rowbuf2 : rowbuf;
			float wv = j ? fv : 1.0f - fv;
			for(int i = 0; i < 2; i++)
			{
				int uu = u0 + i;
				if(uu < 0 || uu >= sw) continue;
				float w = wv * (i ? fu : 1.0f - fu);
				const uint8_t *p = rb + uu * 7;
				for(int k = 0; k < 7; k++) s[k] += w * (float)p[k];
			}
		}
		if(s[6] <= 0.0f && s[3] + s[4] + s[5] <= 0.0f)
			continue;
		px3 *o = &acc[x - x0];
		float ka = 1.0f - s[6] * (1.0f / 255.0f) * vis;
		o->r = (o->r + s[3] * vis) * ka + s[0] * vis;
		o->g = (o->g + s[4] * vis) * ka + s[1] * vis;
		o->b = (o->b + s[5] * vis) * ka + s[2] * vis;
	}
}

/* a round glowing dot from its shape tables: aux = u8 solid RGB, glow RGB, u16 n, f32 bin, n alpha, n glow */
static void LIVE_RAM(do_dot)(const live_face_t *f, const live_layer_rec_t *l, int y, int x0, int x1, px3 *acc)
{
	float vis = l->s2 == LIVE_NO_SLOT ? 1.0f : (float)f->slot[l->s2] / 65535.0f;
	if(vis <= 0.0f)
		return;
	uint8_t dh[12];
	live_fetch(dh, f->pack + l->aux_off, 12);
	uint16_t nb;
	float bin;
	memcpy(&nb, dh + 6, 2);
	memcpy(&bin, dh + 8, 4);
	if(nb < 2 || nb > 512 || bin <= 0.0f)
		return;
	float cx = (float)f->slot[l->s0] / 16.0f, cy = (float)f->slot[l->s1] / 16.0f;
	float rad = (float)(nb - 1) * bin;
	float dy = (float)y + 0.5f - cy;
	if(fabsf(dy) > rad)
		return;
	int a = imax(x0, (int)floorf(cx - rad)), b = imin(x1, (int)ceilf(cx + rad) + 1);
	if(a >= b)
		return;
	const uint8_t *ta = aux_get(f->pack + l->aux_off + 12, (uint32_t)nb * 2), *tg = ta + nb;
	float ibin = 1.0f / bin, rad2 = rad * rad, dy2 = dy * dy;
	for(int x = a; x < b; x++)
	{
		float dx = (float)x + 0.5f - cx;
		float d2 = dx * dx + dy2;
		if(d2 >= rad2)
			continue;
		float t = sqrtf(d2) * ibin;
		int i = (int)t;
		float fr = t - (float)i;
		float al = ((float)ta[i] + ((float)ta[i + 1] - (float)ta[i]) * fr) * (1.0f / 255.0f) * vis;
		float g = ((float)tg[i] + ((float)tg[i + 1] - (float)tg[i]) * fr) * (1.0f / 255.0f) * vis;
		if(al <= 0.0f && g <= 0.0f)
			continue;
		px3 *o = &acc[x - x0];
		o->r = (o->r + (float)dh[3] * g) * (1 - al) + (float)dh[0] * al;
		o->g = (o->g + (float)dh[4] * g) * (1 - al) + (float)dh[1] * al;
		o->b = (o->b + (float)dh[5] * g) * (1 - al) + (float)dh[2] * al;
	}
}

/* a SCROLL layer: its picture slid left by the slot (1/256 px), Catmull-Rom across 4 pixels so the line keeps its edge */
static void LIVE_RAM(do_scroll)(const live_face_t *f, const live_layer_rec_t *l, int y, int x0, int x1, px3 *acc)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	const uint8_t *aux = f->pack + l->aux_off;
	uint8_t hd[8];
	live_fetch(hd, aux, 8);
	uint16_t sw, room;
	memcpy(&sw, hd, 2); memcpy(&room, hd + 4, 2);
	float shift = (float)f->slot[l->s0] / 256.0f;
	if(shift > (float)(sw - l->w)) shift = (float)(sw - l->w);
	if(shift < 0) shift = 0;
	int ish = (int)floorf(shift);
	float fr = shift - (float)ish;
	/* the 4 weights for this fraction (the same all along the row) */
	float t = fr, t2 = t * t, t3 = t2 * t;
	const float wt[4] = { -0.5f * t3 + t2 - 0.5f * t, 1.5f * t3 - 2.5f * t2 + 1.0f, -1.5f * t3 + 2.0f * t2 + 0.5f * t,
	                      0.5f * t3 - 0.5f * t2 };
	int a = imax(x0, l->x), b = imin(x1, l->x + l->w);
	if(a >= b)
		return;
	/* source columns u = (x - box x) + ish + (-1 .. 2) */
	int u0 = (a - l->x) + ish - 1, u1 = (b - 1 - l->x) + ish + 2;
	int c0 = imax(0, u0), c1 = imin((int)sw - 1, u1);
	if(c0 > c1 || c1 - c0 + 1 > 248)
		return;
	live_fetch(scrollbuf, aux + 8 + ((uint32_t)(y - l->y) * room + (uint32_t)c0) * 4, (size_t)(c1 - c0 + 1) * 4);
	for(int x = a; x < b; x++)
	{
		int u = (x - l->x) + ish;
		float pr = 0, pg = 0, pb = 0, pa = 0;
		for(int k = 0; k < 4; k++)
		{
			int uu = u - 1 + k;
			if(uu < c0 || uu > c1) uu = uu < c0 ? c0 : c1;      /* (the edge pixel repeated: weights still sum to 1) */
			const uint8_t *q = scrollbuf + (uu - c0) * 4;
			pr += wt[k] * (float)q[0]; pg += wt[k] * (float)q[1]; pb += wt[k] * (float)q[2]; pa += wt[k] * (float)q[3];
		}
		/* stored with an offset (byte v = 40 + 0.78 x): the rig's shrink leaves values a little under 0 (the dark rim
		   around the line) and over full, and they're kept */
		pr = (pr - 40.0f) * (1.0f / 0.78f); pg = (pg - 40.0f) * (1.0f / 0.78f); pb = (pb - 40.0f) * (1.0f / 0.78f);
		pa = (pa - 40.0f) * (1.0f / 0.78f);
		if(fabsf(pa) < 0.3f && fabsf(pr) < 0.3f && fabsf(pg) < 0.3f && fabsf(pb) < 0.3f)
			continue;
		/* premultiplied over: out * (1 - alpha) + colour */
		float A = pa * (1.0f / 255.0f);
		px3 *o = &acc[x - x0];
		o->r = o->r * (1 - A) + pr;
		o->g = o->g * (1 - A) + pg;
		o->b = o->b * (1 - A) + pb;
	}
}

static void LIVE_RAM(do_cells)(const live_face_t *f, const live_layer_rec_t *l, int y, int x0, int x1, px3 *acc)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	const uint8_t *paux = f->pack + l->aux_off;
	uint8_t ah[4];
	live_fetch(ah, paux, 4);
	int ng = ah[0], nseg = l->aux8;
	/* its colour tables and segment table, from the SRAM cache (2026-11-17: read per pixel from PSRAM they cost a
	   fifth of an update) */
	const uint8_t *aux = aux_get(paux, 4 + (uint32_t)ng * 1536 + (uint32_t)nseg * 16);
	memset(sega + x0, 0, (size_t)(x1 - x0));
	/* this row's segment map */
	uint8_t hdr[4];
	live_fetch(hdr, f->pack + l->data_off + (uint32_t)(y - l->y) * 4, 4);
	uint32_t ro = rd32(hdr);
	if(ro != 0xFFFFFFFFu)
	{
		const uint8_t *row = f->pack + l->data_off + ro;
		uint8_t ns;
		live_fetch(&ns, row, 1);
		row++;
		for(int k = 0; k < ns; k++)
		{
			uint8_t sh[2];
			live_fetch(sh, row, 2);
			int sx = l->x + sh[0], sl = sh[1];
			const uint8_t *pix = row + 2;
			row += 2 + (uint32_t)sl * 2;
			int a = imax(sx, x0), b = imin(sx + sl, x1);
			if(a >= b) continue;
			live_fetch(rowbuf, pix + (uint32_t)(a - sx) * 2, (size_t)(b - a) * 2);
			for(int x = a; x < b; x++)
			{
				segid[x] = rowbuf[(x - a) * 2];
				sega[x] = rowbuf[(x - a) * 2 + 1];
			}
		}
	}
	/* the glows first (light added, as the rig's blur), then the segments over them */
	for(int k = 0; k < nseg; k++)
	{
		const uint8_t *sb = aux + 4 + ng * 1536 + k * 16;
		int16_t gx, gy, gw, gh;
		memcpy(&gx, sb + 2, 2); memcpy(&gy, sb + 4, 2); memcpy(&gw, sb + 6, 2); memcpy(&gh, sb + 8, 2);
		if(y < gy || y >= gy + gh) continue;
		int a = imax(gx, x0), b = imin(gx + gw, x1);
		if(a >= b) continue;
		int grp = sb[0] < ng ? sb[0] : 0;
		int v = f->slot[l->s0 + k] >> 8;
		const uint8_t *gc = aux + 4 + grp * 1536 + 768 + v * 3;
		if(!(gc[0] | gc[1] | gc[2])) continue;
		uint32_t fo = rd32(sb + 12);
		live_fetch(rowbuf2, f->pack + fo + (uint32_t)(y - gy) * gw + (a - gx), (size_t)(b - a));
		for(int x = a; x < b; x++)
		{
			float g = (float)rowbuf2[x - a] * (1.0f / 255.0f);
			if(g <= 0.0f) continue;
			px3 *o = &acc[x - x0];
			o->r += (float)gc[0] * g; o->g += (float)gc[1] * g; o->b += (float)gc[2] * g;
		}
	}
	for(int x = x0; x < x1; x++)
	{
		if(!sega[x]) continue;
		int k = segid[x];
		if(k >= nseg) continue;
		const uint8_t *sb = aux + 4 + ng * 1536 + k * 16;
		int grp = sb[0] < ng ? sb[0] : 0;
		int v = f->slot[l->s0 + k] >> 8;
		const uint8_t *sc = aux + 4 + grp * 1536 + v * 3;
		float a = (float)sega[x] * (1.0f / 255.0f);
		px3 *o = &acc[x - x0];
		/* the glow under a segment is covered by it, as the rig's paint_map does */
		o->r = o->r * (1 - a) + (float)sc[0] * a;
		o->g = o->g * (1 - a) + (float)sc[1] * a;
		o->b = o->b * (1 - a) + (float)sc[2] * a;
	}
}

typedef struct __attribute__((packed)) {
	float x0, y0, x1, y1, fill_alpha, line_w;
	uint8_t r, g, b, pad;
} chart_par_t;

/* a line graph: n points from the array, then the live point from a slot; scrolled left by a fraction of a step */
static void LIVE_RAM(do_chart)(const live_face_t *f, const live_layer_rec_t *l, int y, int x0, int x1, px3 *acc)
{
	chart_par_t cp;
	live_fetch(&cp, f->pack + l->aux_off, sizeof cp);
	int n = f->array_n[l->s0];
	if(n < 2)
		return;
	const uint8_t *vals = f->array[l->s0];
	float live = l->s2 == LIVE_NO_SLOT ? 0.0f : (float)(f->slot[l->s2] >> 8);
	float frac = (float)f->slot[l->s1] / 65535.0f;
	float sp = (cp.x1 - cp.x0) / (float)(n - 1);
	float hgt = cp.y1 - cp.y0;
	float fy = (float)y + 0.5f;
	float hw = cp.line_w * 0.5f;
	int npts = l->s2 == LIVE_NO_SLOT ? n : n + 1;
	for(int x = imax(x0, l->x); x < imin(x1, l->x + l->w); x++)
	{
		float fx = (float)x + 0.5f;
		/* the curve under this pixel (for the fill) and the nearest line piece (for the line) */
		float t = (fx - cp.x0) / sp + frac;
		int i = (int)floorf(t);
		float a = 0.0f;
		if(fx >= cp.x0 && fx <= cp.x1 && i >= 0 && i + 1 < npts)
		{
			float va = (float)(i < n ? vals[i] : live), vb = (float)(i + 1 < n ? vals[i + 1] : live);
			float ft = t - (float)i;
			float cy = cp.y1 - hgt * (va + (vb - va) * ft) / 255.0f;
			float cover = clampf(fy - cy + 0.5f, 0.0f, 1.0f);
			float fade = clampf((cp.y1 - fy) / fmaxf(hgt, 1.0f), 0.0f, 1.0f) * cp.fill_alpha;
			a = cover * fade;
		}
		/* the line: distance to the pieces either side */
		float best = 1e9f;
		for(int k = i - 1; k <= i + 1; k++)
		{
			if(k < 0 || k + 1 >= npts) continue;
			float ax = cp.x0 + ((float)k - frac) * sp, bx = cp.x0 + ((float)(k + 1) - frac) * sp;
			float ay = cp.y1 - hgt * (float)(k < n ? vals[k] : live) / 255.0f;
			float by = cp.y1 - hgt * (float)(k + 1 < n ? vals[k + 1] : live) / 255.0f;
			float vx = bx - ax, vy = by - ay, wx = fx - ax, wy = fy - ay;
			float L = vx * vx + vy * vy;
			float tt = L > 0 ? clampf((wx * vx + wy * vy) / L, 0.0f, 1.0f) : 0.0f;
			float px = ax + tt * vx, py = ay + tt * vy;
			if(px < cp.x0 - 0.01f || px > cp.x1 + 0.01f) { /* outside the box: clipped */
				float cx = clampf(px, cp.x0, cp.x1);
				px = cx; py = ay + (L > 0 ? ((cx - ax) / (vx != 0 ? vx : 1)) * vy : 0);
			}
			float d = sqrtf((fx - px) * (fx - px) + (fy - py) * (fy - py));
			if(d < best) best = d;
		}
		float la = clampf(hw - best + 0.5f, 0.0f, 1.0f);
		if(fx < cp.x0 - hw || fx > cp.x1 + hw) la = 0.0f;
		/* the fill over the background, then the line over it (alpha_composite, as the rig) */
		float A = a + la * (1 - a);
		if(A <= 0.0f) continue;
		px3 *o = &acc[x - x0];
		o->r = o->r * (1 - A) + (float)cp.r * A;
		o->g = o->g * (1 - A) + (float)cp.g * A;
		o->b = o->b * (1 - A) + (float)cp.b * A;
	}
}


static void LIVE_RAM(do_needle)(const live_face_t *f, const live_layer_rec_t *l, int y, int x0, int x1, px3 *acc)
{
	needle_par_t np;
	live_fetch(&np, f->pack + l->aux_off, sizeof np);
	float v = (float)f->slot[l->s0] / 65535.0f;
	float a = (l->a0 + l->sweep * v) * DEG2RAD;
	float ux = cosf(a), uy = sinf(a);
	float fy = (float)y + 0.5f;
	for(int x = imax(x0, l->x); x < imin(x1, l->x + l->w); x++)
	{
		float fx = (float)x + 0.5f;
		float dx = fx - l->cx, dy = fy - l->cy;
		float along = dx * ux + dy * uy;
		float across = fabsf(-dx * uy + dy * ux);
		float t = clampf(along, np.len_in, np.len_out);
		float ex = dx - ux * t, ey = dy - uy * t;
		float d = sqrtf(ex * ex + ey * ey);
		(void)across;
		float sa = clampf(np.width * 0.5f - d + 0.5f, 0.0f, 1.0f);
		float ga = np.glow > 0 && l->sigma > 0 ? np.glow * expf(-(d * d) / (2 * l->sigma * l->sigma)) : 0.0f;
		float hd = sqrtf(dx * dx + dy * dy);
		float ha = clampf(np.hub - hd + 0.5f, 0.0f, 1.0f);
		if(sa <= 0 && ga <= 0.002f && ha <= 0) continue;
		px3 *o = &acc[x - x0];
		o->r += (float)np.r * ga; o->g += (float)np.g * ga; o->b += (float)np.b * ga;
		o->r = o->r * (1 - sa) + (float)np.r * sa;
		o->g = o->g * (1 - sa) + (float)np.g * sa;
		o->b = o->b * (1 - sa) + (float)np.b * sa;
		o->r = o->r * (1 - ha) + (float)np.hr * ha;
		o->g = o->g * (1 - ha) + (float)np.hg * ha;
		o->b = o->b * (1 - ha) + (float)np.hb * ha;
	}
}

static void LIVE_RAM(do_masks)(const live_face_t *f, int y, int x0, int x1, px3 *acc)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	for(int i = 0; i < LIVE_MAX_MASKS; i++)
	{
		const live_mask_t *m = &f->mask[i];
		if(!m->w || y < m->y || y >= m->y + m->h) continue;
		int a = imax(x0, m->x), b = imin(x1, m->x + m->w);
		if(a >= b) continue;
		live_fetch(rowbuf, f->mask_store + m->off + (uint32_t)(y - m->y) * m->w + (a - m->x), (size_t)(b - a));
		for(int x = a; x < b; x++)
		{
			uint8_t al = rowbuf[x - a];
			if(al == LIVE_MASK_ZERO) continue;
			float k = ((float)al - (float)LIVE_MASK_ZERO) * (1.0f / 200.0f);
			px3 *o = &acc[x - x0];
			o->r = o->r * (1 - k) + (float)m->r * k;
			o->g = o->g * (1 - k) + (float)m->g * k;
			o->b = o->b * (1 - k) + (float)m->b * k;
		}
	}
}


static void LIVE_RAM(build_row)(const live_face_t *f, int x0, int x1, int y)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	uint32_t t = live_now_us(), t2;
	live_fetch(basebuf, f->base_rw + ((uint32_t)y * LIVE_W + x0) * 3, (size_t)(x1 - x0) * 3);
	for(int x = 0; x < x1 - x0; x++)
	{
		acc_rows[x].r = basebuf[x * 3];
		acc_rows[x].g = basebuf[x * 3 + 1];
		acc_rows[x].b = basebuf[x * 3 + 2];
	}
	t2 = live_now_us();
	live_prof[0] += t2 - t;
	t = t2;
	for(int i = 0; i < f->n_layers; i++)
	{
		const live_layer_rec_t *l = &f->layer[i];
		if(y < l->y || y >= l->y + l->h || x1 <= l->x || x0 >= l->x + l->w)
			continue;
		switch(l->kind)
		{
		case LIVE_SPAN: do_span(f, l, y, x0, x1, acc_rows); break;
		case LIVE_TINT: do_tint(f, l, y, x0, x1, acc_rows); break;
		case LIVE_DOT: do_dot(f, l, y, x0, x1, acc_rows); break;
		case LIVE_GAUGE: do_gauge(f, l, y, x0, x1, acc_rows); break;
		case LIVE_SCROLL: do_scroll(f, l, y, x0, x1, acc_rows); break;
		case LIVE_SPRITE: do_sprite(f, l, y, x0, x1, acc_rows); break;
		case LIVE_CELLS: do_cells(f, l, y, x0, x1, acc_rows); break;
		case LIVE_CHART: do_chart(f, l, y, x0, x1, acc_rows); break;
		case LIVE_NEEDLE: do_needle(f, l, y, x0, x1, acc_rows); break;
		}
		t2 = live_now_us();
		if(l->kind < 10)
			live_prof[l->kind] += t2 - t;
		t = t2;
	}
	do_masks(f, y, x0, x1, acc_rows);
	live_prof[10] += live_now_us() - t;
}

/* 4x4 ordered dither, so the dark gradients don't band in the screen's 565 colours */
static const uint8_t bayer4[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

void LIVE_RAM(live_compose)(const live_face_t *f, int x0, int x1, int y, int n, uint16_t *out, bool swap)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	/* 8.8 fixed point: brightness and dither as integers (the float version cost a microsecond a pixel more) */
	int br = f->brightness + (f->brightness >> 7);       /* 0..256 */
	for(int j = 0; j < n; j++, y++)
	{
		build_row(f, x0, x1, y);
		uint32_t t = live_now_us();
		uint16_t *o = out + (size_t)j * (x1 - x0);
		const uint8_t *bay = bayer4[y & 3];
		for(int x = x0; x < x1; x++)
		{
			const px3 *p = &acc_rows[x - x0];
			/* 4 bits of fraction kept: the same numbers as (v * br + (d + .5) / 2) / 8 in floats */
			int ri = (int)(p->r * 16.0f), gi = (int)(p->g * 16.0f), bi = (int)(p->b * 16.0f);
			ri = ri < 0 ? 0 : ri > 4080 ? 4080 : ri;
			gi = gi < 0 ? 0 : gi > 4080 ? 4080 : gi;
			bi = bi < 0 ? 0 : bi > 4080 ? 4080 : bi;
			int d = bay[x & 3];                                 /* 0..15 */
			int r = (ri * br + (d * 8 + 4) * 256) >> 15;
			int g = (gi * br + (d * 4 + 2) * 256) >> 14;
			int b = (bi * br + (d * 8 + 4) * 256) >> 15;
			if(r > 31) r = 31;
			if(g > 63) g = 63;
			if(b > 31) b = 31;
			uint16_t v = (uint16_t)(r << 11 | g << 5 | b);
			o[x - x0] = swap ? (uint16_t)(v >> 8 | v << 8) : v;
		}
		live_prof[11] += live_now_us() - t;
	}
}

void live_compose888(const live_face_t *f, int x0, int x1, int y, int n, uint8_t *out)
{
	live_scratch_t *S_ = &live_scratch[LIVE_CORE()];
	for(int j = 0; j < n; j++, y++)
	{
		build_row(f, x0, x1, y);
		uint8_t *o = out + (size_t)j * (x1 - x0) * 3;
		for(int x = x0; x < x1; x++)
		{
			const px3 *p = &acc_rows[x - x0];
			o[(x - x0) * 3] = (uint8_t)(clampf(p->r, 0, 255) + 0.5f);
			o[(x - x0) * 3 + 1] = (uint8_t)(clampf(p->g, 0, 255) + 0.5f);
			o[(x - x0) * 3 + 2] = (uint8_t)(clampf(p->b, 0, 255) + 0.5f);
		}
	}
}
