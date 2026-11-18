/* Pictures from the rig on the XL's second core (pics.h). */
#include "pics.h"
#include <cstring>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/flash.h"
#include "hardware/sync.h"
#include "tft.h"
#include "screen.h"
extern "C" {
#include "lcd.h"
}
#include "jpegdec_glue.h"
extern "C" {
#include "live.h"
}
#include "hardware/xip_cache.h"
#include "hardware/sync.h"

/* in PSRAM (2026-11-16: in SRAM they left the heap 131 KB, and the weather's HTTPS peaked at 110 KB) */
static uint8_t slot_buf[PICS_SLOTS][PICS_MAX] __uninitialized_psram("pics");
static volatile bool slot_busy[PICS_SLOTS];

typedef struct {
	int slot, orb, set;
	uint32_t len, gen;
	uint8_t brightness;
	uint8_t live_kind, live_index;         /* 0 = a picture; else a live upload (pics.h LIVE_UP_...) into buffer slot */
} job_t;
static queue_t jobs, results;
static volatile uint32_t submitted, finished, gen;
static volatile uint32_t job_start_ms;        /* 0 = the second core isn't on a picture */
/* where the second core is (said with "EV STUCK"): 1 waiting for a picture, 2 opening it, 3 waiting for the screens' bus,
   4 decoding and sending, 5 finishing the send, 6 handing back its answer (the answers' queue full) */
volatile uint8_t pics_core1_step;
static volatile uint32_t n_decoded, n_failed, sum_us, max_us, n_timed;

/* ---- the second core ----
   Each picture streams straight to its screen: the decoder (JPEGDEC since 2026-11-16: 60 -> about 30 ms a picture,
   jpegdec_glue.c) fills one strip of rows (one row of JPEG blocks, 8 or 16 pixels high) in SRAM, the strip goes out by DMA while the next one decodes. Nothing goes through the PSRAM: written
   there, the pictures pushed the decoder's own code out of the chip's 16 KB cache (a test: 78-95 ms a picture
   against 49 ms on the bench). The orbs' PSRAM pictures (orb_fb) aren't kept on the rig's picture sets: nothing reads
   them there, and leaving one of those sets clears every orb. */
static uint16_t strip[2][ORB_W * 16] __attribute__((aligned(4)));
struct dec_ctx {
	uint8_t brightness;
	int orb;
	uint32_t gen;
	int w, h, cur;                                /* picture size, the strip being filled */
};

/* one block of decoded rows from JPEGDEC (native RGB565): into the strip, bytes swapped for the screens; a finished
   row of blocks goes out by DMA */
static int __not_in_flash_func(dec_draw)(int x, int y, int bw, int bh, uint16_t *px, void *user)
{
	dec_ctx *c = (dec_ctx *)user;
	if(c->gen != gen)
		return 0;                              /* the set changed: stop here */
	if(x >= c->w || y >= ORB_H)
		return 1;
	int cw = x + bw > c->w ? c->w - x : bw;
	int rows = y + bh > ORB_H ? ORB_H - y : bh;
	uint16_t *dst = strip[c->cur];
	for(int j = 0; j < rows; j++)
	{
		uint16_t *d = dst + j * c->w + x;
		const uint16_t *p = px + j * bw;
		for(int i = 0; i < cw; i++)
			d[i] = __builtin_bswap16(p[i]);
		if(c->brightness != 255)
			Utils::rgb565dimBitmap(d, (size_t)cw, c->brightness, true);   /* the original's tftOutput dimming */
	}
	if(x + bw >= c->w)                          /* the row of blocks is complete: out it goes */
	{
		lcd_trim(c->orb, dst, (uint32_t)(rows * c->w));   /* (the orb's own trim, settings version 4) */
		lcd_stream_rows(dst, (uint32_t)(rows * c->w));
		c->cur ^= 1;                           /* (lcd_stream_rows waited for the other strip's DMA first) */
	}
	return 1;
}

/* ---- live faces on the second core (pics.h) ---- */
static uint8_t live_packs[LIVE_PACKS][LIVE_PACK_MAX] __uninitialized_psram("livepack");
static uint8_t live_small[LIVE_SMALLS][LIVE_SMALL_MAX] __uninitialized_psram("livesmall");
static uint8_t live_masks[5][LIVE_MAX_MASKS * LIVE_MASK_MAX] __uninitialized_psram("livemask");
static volatile bool pack_busy[LIVE_PACKS], small_busy[LIVE_SMALLS];
static live_face_t lv[5];
static volatile int lv_pack[5] = { -1, -1, -1, -1, -1 }, lv_set[5] = { -1, -1, -1, -1, -1 };
static volatile uint16_t lv_face[5];
static volatile uint8_t live_show_mask, live_brightness = 255, live_drawn_pub;
static volatile bool live_fb_wanted;                /* a settings page is watching: keep the orbs' picture memory up */
static bool live_fb_had;
static volatile bool live_busy;
static uint8_t live_drawn_mask;                     /* (the second core's: drawn since shown) */
static spin_lock_t *pend_lock;
static uint16_t pend_val[5][LIVE_MAX_SLOTS];
static uint32_t pend_bits[5][(LIVE_MAX_SLOTS + 31) / 32];
static volatile uint32_t lv_frames, lv_sum_us, lv_max_us, lv_sum_px;

/* how long the live faces spend copying from PSRAM (measured: the "?" LIVE line's fetch_us) */
extern "C" void (*live_fetch)(void *dst, const void *src, size_t n);
static volatile uint32_t lv_fetch_us, lv_fetch_bytes;
static void __not_in_flash_func(fetch_timed)(void *d, const void *s, size_t n)
{
	uint32_t t0 = time_us_32();
	memcpy(d, s, n);
	lv_fetch_us = lv_fetch_us + (time_us_32() - t0);
	lv_fetch_bytes = lv_fetch_bytes + (uint32_t)n;
}

extern "C" uint32_t (*live_now_us)(void);
extern "C" uint32_t live_prof[12];
static uint32_t __not_in_flash_func(time_us_32_fn)(void) { return time_us_32(); }
void pics_live_prof(uint32_t out[12]) { for(int i = 0; i < 12; i++) { out[i] = live_prof[i]; live_prof[i] = 0; } }

/* a fixed measure of the live faces' drawing (2026-11-18, for speeding it up): each loaded face built whole, 240 x 240,
   by the second core into a strip that's never sent (nothing on the screens changes), timed whole and by layer kind */
static volatile bool bench_want;
static volatile uint32_t bench_runs;
static uint32_t bench_us[5], bench_prof[12], bench_fetch_us;
void pics_live_bench(void) { bench_want = true; __sev(); }
uint32_t pics_live_bench_fetch_us(void) { return bench_fetch_us; }
uint32_t pics_live_bench_result(uint32_t us[5], uint32_t prof[12])
{
	for(int o = 0; o < 5; o++) us[o] = bench_us[o];
	for(int i = 0; i < 12; i++) prof[i] = bench_prof[i];
	return bench_runs;
}

/* PSRAM through the window that neither uses nor fills the 16 KB cache (the code running from flash keeps its place) */
static inline uint8_t *nc(const void *p) { return (uint8_t *)((uintptr_t)p + (XIP_NOCACHE_NOALLOC_BASE - XIP_BASE)); }

static void fb_cache(int orb, bool clean)
{
	uintptr_t a = (uintptr_t)orb_fb[orb] - XIP_BASE, e = a + (uintptr_t)ORB_W * ORB_H * 2;
	a &= ~(uintptr_t)(XIP_CACHE_LINE_SIZE - 1);
	e = (e + XIP_CACHE_LINE_SIZE - 1) & ~(uintptr_t)(XIP_CACHE_LINE_SIZE - 1);
	if(clean)
		xip_cache_clean_range(a, e - a);           /* (anything the first core left in the cache goes out first) */
	else
		xip_cache_invalidate_range(a, e - a);      /* (and it reads what the second core wrote after) */
}

static void do_upload(const job_t &j, pics_result_t &res)
{
	res.live_kind = j.live_kind;
	live_face_t *f = &lv[j.orb];
	switch(j.live_kind)
	{
	case LIVE_UP_FACE:
	{
		/* checked into a spare first: a bad pack leaves the old face as it was */
		static live_face_t tmp;
		const char *err = "?";
		tmp.brightness = f->brightness;
		/* the pack came in past the cache: anything the cache still holds of this buffer is old */
		uintptr_t a = (uintptr_t)live_packs[j.slot] - XIP_BASE, e = a + j.len;
		a &= ~(uintptr_t)(XIP_CACHE_LINE_SIZE - 1);
		e = (e + XIP_CACHE_LINE_SIZE - 1) & ~(uintptr_t)(XIP_CACHE_LINE_SIZE - 1);
		xip_cache_invalidate_range(a, e - a);
		/* read (and changed: masks, patches, colour tables) through the cache from here: measured 40 % less
		   time reading than past it, the first core's answers no slower; only the second core touches it */
		if(live_load(&tmp, live_packs[j.slot], j.len, live_masks[j.orb], &err))
		{
			int old = lv_pack[j.orb];
			*f = tmp;
			lv_pack[j.orb] = j.slot;
			lv_set[j.orb] = j.set;
			lv_face[j.orb] = f->face_id;
			if(old >= 0 && old != j.slot)
				pack_busy[old] = false;
			live_drawn_mask &= (uint8_t)~(1u << j.orb);
			res.ok = true;
			res.live_face = f->face_id;
		}
		else
		{
			pack_busy[j.slot] = false;
			res.live_err = err;
		}
		return;
	}
	case LIVE_UP_MASK:
	{
		const uint8_t *p = nc(live_small[j.slot]);
		uint8_t h[12];
		memcpy(h, p, 12);
		int16_t x, y, w, hh;
		memcpy(&x, h + 1, 2); memcpy(&y, h + 3, 2); memcpy(&w, h + 5, 2); memcpy(&hh, h + 7, 2);
		res.ok = f->pack && j.len >= 12 && (uint32_t)w * (uint32_t)(hh > 0 ? hh : 0) <= j.len - 12 &&
		         live_set_mask(f, h[0], x, y, w, hh, h[9], h[10], h[11], p + 12);
		if(!res.ok) res.live_err = "bad mask";
		break;
	}
	case LIVE_UP_PATCH:
	{
		const uint8_t *p = nc(live_small[j.slot]);
		int16_t v[4];
		memcpy(v, p, 8);
		res.ok = f->pack && j.len >= 8 && v[2] > 0 && v[3] > 0 && (uint32_t)v[2] * v[3] * 3 <= j.len - 8 &&
		         live_patch(f, v[0], v[1], v[2], v[3], p + 8);
		if(!res.ok) res.live_err = "bad patch";
		break;
	}
	case LIVE_UP_LUT:
		res.ok = f->pack && j.len >= 768 && live_set_lut(f, j.live_index, nc(live_small[j.slot]));
		if(!res.ok) res.live_err = "bad colour table";
		break;
	case LIVE_UP_ARRAY:
	{
		uint8_t tmp[LIVE_ARRAY_MAX];
		uint32_t n = j.len > LIVE_ARRAY_MAX ? LIVE_ARRAY_MAX : j.len;
		memcpy(tmp, nc(live_small[j.slot]), n);
		live_set_array(f, j.live_index, tmp, (int)n);
		res.ok = f->pack != nullptr;
		break;
	}
	case LIVE_UP_IMAGE:
	{
		const uint8_t *p = nc(live_small[j.slot]);
		uint16_t w;
		memcpy(&w, p, 2);
		res.ok = f->pack && j.len >= 2 && live_set_image(f, j.live_index, w, p + 2, j.len - 2);
		if(!res.ok) res.live_err = "bad scroll picture";
		break;
	}
	default:
		res.live_err = "unknown upload";
	}
	small_busy[j.slot] = false;
}

/* ---- both cores build the live faces (2026-11-18).
   A rectangle is cut into strips; whichever core is free takes the next one (the first core between its own jobs, in
   its main loop: pics_help), and only this core sends them, in order. Four strip buffers (the picture strips' memory,
   unused meanwhile): a strip may be built up to three ahead of the one going out by DMA. live.c gives each core its
   own working memory, so both build rows of the same face at once; the face itself isn't changed while they do. */
#define HB 4
#define HSTRIPS 512                              /* a frame's strips at most (each at least ~1,000 pixels) */
static spin_lock_t *help_lock;
static struct {
	live_face_t *f;
	int orb, n;
	live_rect_t rect[LIVE_MAX_RECTS];
	uint8_t s_rect[HSTRIPS];                     /* each strip: its rectangle, first row, rows */
	uint8_t s_y[HSTRIPS], s_n[HSTRIPS];
	volatile int next, limit, active, busy0;
	volatile int ready[HB];                      /* the strip each buffer holds (-1: none yet) */
} hj;
static volatile bool help_on = true;             /* the first core may help (the benchmark compares with it off) */
static volatile uint32_t help_strips[2];         /* strips built by each core */
static inline uint16_t *hbuf(int k) { return &strip[0][0] + (size_t)(k % HB) * (ORB_W * 8); }

static int help_claim(bool core0)
{
	uint32_t save = spin_lock_blocking(help_lock);
	int j = -1;
	if(hj.active && hj.next < hj.n && hj.next <= hj.limit && (!core0 || help_on))
	{
		j = hj.next++;
		if(core0)
			hj.busy0 = 1;
	}
	spin_unlock(help_lock, save);
	return j;
}

static void __not_in_flash_func(help_build)(int j, int core)
{
	const live_rect_t *r = &hj.rect[hj.s_rect[j]];
	int y = hj.s_y[j], n = hj.s_n[j], w = r->x1 - r->x0;
	uint16_t *b = hbuf(j);
	live_compose(hj.f, r->x0, r->x1, y, n, b, true);
	lcd_trim(hj.orb, b, (uint32_t)(n * w));          /* (the orb's own trim, settings version 4) */
	__dmb();
	hj.ready[j % HB] = j;
	help_strips[core] = help_strips[core] + 1;
}

/* the first core, from its main loop: builds strips of the rectangle being drawn, if any are free to take */
void __not_in_flash_func(pics_help)(void)
{
	if(!hj.active)
		return;
	int j;
	while((j = help_claim(true)) >= 0)
	{
		help_build(j, 0);
		__dmb();
		hj.busy0 = 0;
	}
}

/* a frame of face f (its rectangles), both cores building, every strip in order to the sink: begin(ctx, rect) before a
   rectangle's first strip, strip(ctx, pixels, y, rows, width), end(ctx) after its last. Strips of about 1,000 pixels
   (up to 16 rows, never more than a buffer), so both cores get work even from the small boxes of the CPU's cells. */
struct strip_sink { void (*begin)(void *, const live_rect_t *); void (*strip)(void *, const uint16_t *, int, int, int);
                    void (*end)(void *); void *ctx; };
static void __not_in_flash_func(build_frame)(live_face_t *f, int orb, const live_rect_t *rects, int nr, const strip_sink &sink)
{
	uint32_t save = spin_lock_blocking(help_lock);
	hj.f = f; hj.orb = orb;
	int n = 0;
	for(int i = 0; i < nr && i < LIVE_MAX_RECTS; i++)
	{
		live_rect_t r = rects[i];
		int w = r.x1 - r.x0, h = r.y1 - r.y0;
		if(w <= 0 || h <= 0)
			continue;
		hj.rect[i] = r;
		int rows = (1000 + w - 1) / w;
		if(rows > 16) rows = 16;
		if(rows > (ORB_W * 8) / w) rows = (ORB_W * 8) / w;
		for(int y = r.y0; y < r.y1 && n < HSTRIPS; y += rows)
		{
			hj.s_rect[n] = (uint8_t)i;
			hj.s_y[n] = (uint8_t)y;
			hj.s_n[n] = (uint8_t)(r.y1 - y < rows ? r.y1 - y : rows);
			n++;
		}
	}
	hj.n = n;
	hj.next = 0;
	hj.limit = HB - 1;                           /* (nothing going out yet: all four buffers free) */
	for(int i = 0; i < HB; i++)
		hj.ready[i] = -1;
	hj.active = 1;
	spin_unlock(help_lock, save);
	__sev();
	int cur = -1;
	for(int k = 0; k < n; k++)
	{
		while(hj.ready[k % HB] != k)
		{
			int j = help_claim(false);
			if(j >= 0)
				help_build(j, 1);                /* (else the first core is building strip k: a moment) */
		}
		__dmb();
		int ri = hj.s_rect[k];
		if(ri != cur)
		{
			if(cur >= 0 && sink.end)
				sink.end(sink.ctx);              /* (waits for that rectangle's last strip to have gone) */
			if(sink.begin)
				sink.begin(sink.ctx, &hj.rect[ri]);
			cur = ri;
		}
		const live_rect_t *r = &hj.rect[ri];
		sink.strip(sink.ctx, hbuf(k), hj.s_y[k], hj.s_n[k], r->x1 - r->x0);
		save = spin_lock_blocking(help_lock);
		hj.limit = k + HB - 1;                   /* (strip k-1's buffer: its DMA ended when strip k's began) */
		spin_unlock(help_lock, save);
	}
	if(cur >= 0 && sink.end)
		sink.end(sink.ctx);
	save = spin_lock_blocking(help_lock);
	hj.active = 0;
	spin_unlock(help_lock, save);
	while(hj.busy0)
		tight_loop_contents();
}

struct send_ctx { int orb, x0; uint16_t *fb; uint32_t px; };
static void __not_in_flash_func(send_begin)(void *c, const live_rect_t *r)
{
	send_ctx *sc = (send_ctx *)c;
	pics_core1_step = 7;
	lcd_stream_begin_rect(sc->orb, r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
	pics_core1_step = 8;
	sc->x0 = r->x0;
	sc->px += (uint32_t)((r->x1 - r->x0) * (r->y1 - r->y0));
}
static void __not_in_flash_func(send_end)(void *c) { (void)c; lcd_stream_end(); }
static void __not_in_flash_func(send_sink)(void *c, const uint16_t *px, int y, int n, int w)
{
	send_ctx *sc = (send_ctx *)c;
	lcd_stream_rows(px, (uint32_t)(n * w));         /* (waits for the strip before it to have gone first) */
	if(live_fb_had)                                  /* (the page's copy only while a page watches) */
		for(int k = 0; k < n; k++)
			memcpy(sc->fb + (size_t)(y + k) * ORB_W + sc->x0, px + k * w, (size_t)w * 2);
}

/* the changed rectangles of one orb's face, built strip by strip straight to its screen */
static void live_render(int orb)
{
	live_face_t *f = &lv[orb];
	uint32_t t0 = time_us_32(), px = 0;
	uint16_t *fb = (uint16_t *)nc(orb_fb[orb]);
	send_ctx sc = { orb, 0, fb, 0 };
	strip_sink sink = { send_begin, send_sink, send_end, &sc };
	build_frame(f, orb, f->dirty, f->n_dirty, sink);   /* (strips built by both cores, each sent as soon as it's ready) */
	px = sc.px;
	f->n_dirty = 0;
	uint32_t us = time_us_32() - t0;
	lv_frames = lv_frames + 1;
	lv_sum_us = lv_sum_us + us;
	lv_sum_px = lv_sum_px + px;
	if(us > lv_max_us)
		lv_max_us = us;
}

/* one orb's live face, if one is shown and has something to draw (round the orbs in turn): true if it drew */
static bool live_step(void)
{
	uint8_t show = live_show_mask;
	uint8_t gone = (uint8_t)(live_drawn_mask & ~show);
	for(int orb = 0; orb < 5; orb++)
		if(gone & (1u << orb))
			fb_cache(orb, false);
	live_drawn_mask &= show;
	live_drawn_pub = live_drawn_mask;
	bool fbw = live_fb_wanted;
	if(fbw && !live_fb_had)
		live_drawn_mask = 0;                        /* (a page started watching: every live face drawn whole once, so
		                                               the picture memory has all of it) */
	live_fb_had = fbw;
	static int rr;
	for(int k = 0; k < 5; k++)
	{
		int orb = (rr + k) % 5;
		uint8_t bit = (uint8_t)(1u << orb);
		live_face_t *f = &lv[orb];
		if(!(show & bit) || !f->pack)
			continue;
		/* the slots the rig sent since */
		uint16_t vals[LIVE_MAX_SLOTS];
		uint32_t bits[(LIVE_MAX_SLOTS + 31) / 32];
		uint32_t save = spin_lock_blocking(pend_lock);
		memcpy(bits, pend_bits[orb], sizeof bits);
		memset(pend_bits[orb], 0, sizeof bits);
		memcpy(vals, pend_val[orb], sizeof vals);
		spin_unlock(pend_lock, save);
		for(int s = 0; s < LIVE_MAX_SLOTS; s++)
			if(bits[s >> 5] & (1u << (s & 31)))
				live_set_slots(f, s, 1, &vals[s]);
		if(!(live_drawn_mask & bit))
		{
			fb_cache(orb, true);
			live_mark_all(f);
		}
		uint8_t b = live_brightness;
		if(f->brightness != b)
		{
			f->brightness = b;
			live_mark_all(f);
		}
		if(!f->n_dirty)
		{
			live_drawn_mask |= bit;
			continue;
		}
		live_busy = true;
		__dmb();
		if(!(live_show_mask & bit))                 /* (the first core took it away meanwhile) */
		{
			live_busy = false;
			continue;
		}
		job_start_ms = to_ms_since_boot(get_absolute_time()) | 1u;
		live_render(orb);
		job_start_ms = 0;
		live_drawn_mask |= bit;
		live_drawn_pub = live_drawn_mask;
		__dmb();
		live_busy = false;
		rr = orb + 1;
		return true;
	}
	return false;
}

static void do_picture(const job_t &j, pics_result_t &res)
{
	if(j.gen != gen)
	{
		res.skipped = true;
		return;
	}
	uint32_t t0 = time_us_32();
	dec_ctx c = {};
	c.brightness = j.brightness;
	c.orb = j.orb;
	c.gen = j.gen;
	int mcu_h = 0;
	bool ok = orbs_jd_open(slot_buf[j.slot], j.len, dec_draw, &c, &c.w, &c.h, &mcu_h) == 1;
	if(ok && (c.w > ORB_W || c.w < 1 || c.h < 1 || mcu_h > 16))
	{
		ok = false;                       /* (bigger than an orb, or blocks taller than a strip: not ours) */
		orbs_jd_decode_abandon();
	}
	if(ok)
	{
		pics_core1_step = 3;
		lcd_stream_begin(j.orb, c.w, c.h > ORB_H ? ORB_H : c.h);
		pics_core1_step = 4;
		ok = orbs_jd_decode() == 1;
		pics_core1_step = 5;
		lcd_stream_end();
	}
	res.decode_us = time_us_32() - t0;
	if(ok)
	{
		res.ok = true;
		n_decoded = n_decoded + 1;
		sum_us = sum_us + res.decode_us;
		n_timed = n_timed + 1;
		if(res.decode_us > max_us)
			max_us = res.decode_us;
	}
	else if(j.gen != gen)
		res.skipped = true;           /* stopped part way: its set had gone */
	else
		n_failed = n_failed + 1;
	slot_busy[j.slot] = false;            /* (the JPEG bytes aren't needed any more) */
}

static uint32_t bench_us1[5], bench_crc[2], bench_strips0;
static void __not_in_flash_func(crc_sink)(void *c, const uint16_t *px, int y, int n, int w)
{
	(void)y;
	uint32_t h = *(uint32_t *)c;
	for(int i = 0; i < n * w; i++)
		h = (h ^ px[i]) * 16777619u;              /* (FNV-1a over the pixels: the two ways must give the same) */
	*(uint32_t *)c = h;
}
static void live_bench_run(void)
{
	uint32_t keep[12];
	bool was = help_on;
	for(int pass = 0; pass < 2; pass++)          /* (pass 0: this core alone; pass 1: both cores) */
	{
		help_on = pass == 1;
		for(int i = 0; i < 12; i++) { keep[i] = live_prof[i]; live_prof[i] = 0; }
		uint32_t f0 = lv_fetch_us, s0 = help_strips[0], h = 2166136261u;
		for(int o = 0; o < 5; o++)
		{
			uint32_t *dst = pass ? &bench_us[o] : &bench_us1[o];
			*dst = 0;
			if(!lv[o].pack)
				continue;
			uint32_t t0 = time_us_32();
			live_rect_t whole = { 0, 0, ORB_W, ORB_H };
			strip_sink sink = { nullptr, crc_sink, nullptr, &h };
			build_frame(&lv[o], o, &whole, 1, sink);
			*dst = time_us_32() - t0;
		}
		bench_crc[pass] = h;
		if(pass)
		{
			for(int i = 0; i < 12; i++) bench_prof[i] = live_prof[i];
			bench_fetch_us = lv_fetch_us - f0;
			bench_strips0 = help_strips[0] - s0;
		}
		for(int i = 0; i < 12; i++) live_prof[i] = keep[i];
	}
	help_on = was;
	bench_runs = bench_runs + 1;
}

void pics_live_bench_two(uint32_t us1[5], uint32_t crc[2], uint32_t *strips0)
{
	for(int o = 0; o < 5; o++) us1[o] = bench_us1[o];
	crc[0] = bench_crc[0]; crc[1] = bench_crc[1]; *strips0 = bench_strips0;
}
void pics_help_set(bool on) { help_on = on; }
bool pics_help_get(void) { return help_on; }

static void core1_main(void)
{
	flash_safe_execute_core_init();             /* so a settings save can pause this core safely */
	for(;;)
	{
		job_t j;
		if(bench_want)
		{
			bench_want = false;
			live_bench_run();
		}
		pics_core1_step = 1;
		if(queue_try_remove(&jobs, &j))
		{
			job_start_ms = to_ms_since_boot(get_absolute_time()) | 1u;
			pics_core1_step = 2;
			pics_result_t res = { j.orb, j.set, false, false, 0, 0, 0, nullptr };
			if(j.live_kind)
				do_upload(j, res);
			else
				do_picture(j, res);
			pics_core1_step = 6;
			queue_add_blocking(&results, &res);
			job_start_ms = 0;
			__dmb();
			finished = finished + 1;
			continue;
		}
		if(live_step())
			continue;
		__wfe();                                    /* (until the first core sends something: queue_add and the
		                                               live calls wake it) */
	}
}

/* ---- the first core ---- */
void pics_init(void)
{
	pend_lock = spin_lock_init(spin_lock_claim_unused(true));
	help_lock = spin_lock_init(spin_lock_claim_unused(true));
	live_fetch = fetch_timed;
	live_now_us = time_us_32_fn;
	queue_init(&jobs, sizeof(job_t), PICS_SLOTS + LIVE_SMALLS + LIVE_PACKS);
	queue_init(&results, sizeof(pics_result_t), 16);
	/* its own 16 KB stack (the SDK's default for the second core is 2 KB; the decoder wants more) */
	static uint32_t core1_stack[4096];
	multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof core1_stack);
}

uint8_t *pics_take(int *slot)
{
	for(int i = 0; i < PICS_SLOTS; i++)
		if(!slot_busy[i])
		{
			slot_busy[i] = true;
			*slot = i;
			return slot_buf[i];
		}
	return nullptr;
}

void pics_give_back(int slot)
{
	if(slot >= 0 && slot < PICS_SLOTS)
		slot_busy[slot] = false;
}

void pics_submit(int slot, int orb, int set, uint32_t len, uint8_t brightness)
{
	job_t j = { slot, orb, set, len, gen, brightness, 0, 0 };
	submitted = submitted + 1;
	__dmb();
	queue_add_blocking(&jobs, &j);
}

bool pics_result(pics_result_t *r)
{
	return queue_try_remove(&results, r);
}

bool pics_idle(void)
{
	__dmb();
	return finished == submitted && !live_busy;
}

void pics_wait_idle(void)
{
	while(!pics_idle())
		tight_loop_contents();           /* (the caller drains pics_result() meanwhile if it can fill: main.cpp) */
}

void pics_cancel(void)
{
	gen = gen + 1;
	__dmb();
}

bool pics_stuck(uint32_t now_ms)
{
	uint32_t s = job_start_ms;
	/* signed: the caller reads the clock first, so a picture started just after (or in the same millisecond: the start
	   is stored |1) looks 1 ms in the future; unsigned, that wrapped to ~4 billion ms and said "stuck" (8 false reports
	   2026-11-16/17, each just after a set change, the picture core at step 2) */
	return s && (int32_t)(now_ms - s) > 3000;
}

void pics_stats(uint32_t *decoded, uint32_t *failed, uint32_t *avg_us, uint32_t *max)
{
	*decoded = n_decoded;
	*failed = n_failed;
	uint32_t n = n_timed;
	*avg_us = n ? sum_us / n : 0;
	*max = max_us;
	sum_us = 0;
	n_timed = 0;
	max_us = 0;
}

/* ---- live faces: the first core's side ---- */
uint8_t *pics_live_buffer(int kind, uint32_t len, int *buf)
{
	if(kind == LIVE_UP_FACE)
	{
		if(len > LIVE_PACK_MAX)
			return nullptr;
		for(int i = 0; i < LIVE_PACKS; i++)
		{
			bool used = false;
			for(int o = 0; o < 5; o++)
				used = used || lv_pack[o] == i;
			if(!pack_busy[i] && !used)
			{
				pack_busy[i] = true;
				*buf = i;
				return nc(live_packs[i]);
			}
		}
		return nullptr;
	}
	if(len > LIVE_SMALL_MAX)
		return nullptr;
	for(int i = 0; i < LIVE_SMALLS; i++)
		if(!small_busy[i])
		{
			small_busy[i] = true;
			*buf = i;
			return nc(live_small[i]);
		}
	return nullptr;
}

void pics_live_give_back(int kind, int buf)
{
	if(kind == LIVE_UP_FACE && buf >= 0 && buf < LIVE_PACKS)
		pack_busy[buf] = false;
	else if(buf >= 0 && buf < LIVE_SMALLS)
		small_busy[buf] = false;
}

void pics_live_upload(int buf, int orb, int kind, int index, uint32_t len, int set)
{
	job_t j = { buf, orb, set, len, gen, 255, (uint8_t)kind, (uint8_t)index };
	submitted = submitted + 1;
	__dmb();
	queue_add_blocking(&jobs, &j);
}

void pics_live_slots(int orb, int slot, uint16_t value)
{
	if(orb < 0 || orb >= 5 || slot < 0 || slot >= LIVE_MAX_SLOTS)
		return;
	uint32_t save = spin_lock_blocking(pend_lock);
	pend_val[orb][slot] = value;
	pend_bits[orb][slot >> 5] |= 1u << (slot & 31);
	spin_unlock(pend_lock, save);
	__sev();
}

void pics_live_fb(bool wanted)
{
	if(live_fb_wanted != wanted)
	{
		live_fb_wanted = wanted;
		__sev();
	}
}

void pics_live_show(uint8_t mask, uint8_t brightness)
{
	if(live_show_mask != mask || live_brightness != brightness)
	{
		live_show_mask = mask;
		live_brightness = brightness;
		__dmb();
		__sev();
	}
}

uint16_t pics_live_face(int orb, int *set)
{
	if(orb < 0 || orb >= 5)
		return 0;
	if(set)
		*set = lv_set[orb];
	return lv_face[orb];
}

bool pics_live_drawing(uint8_t mask)
{
	return (live_drawn_pub & mask) != 0;
}

uint32_t pics_live_fetch_us(uint32_t *bytes)
{
	uint32_t us = lv_fetch_us;
	*bytes = lv_fetch_bytes;
	lv_fetch_us = 0;
	lv_fetch_bytes = 0;
	return us;
}

void pics_live_stats(uint32_t *frames, uint32_t *avg_us, uint32_t *max, uint32_t *avg_px)
{
	uint32_t n = lv_frames;
	*frames = n;
	*avg_us = n ? lv_sum_us / n : 0;
	*avg_px = n ? lv_sum_px / n : 0;
	*max = lv_max_us;
	lv_frames = 0;
	lv_sum_us = 0;
	lv_sum_px = 0;
	lv_max_us = 0;
}
