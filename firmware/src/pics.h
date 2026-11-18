/* Pictures from the rig, build step 4 (2026-11-16): the XL's second core decodes each picture and sends it to its
   screen by DMA, while the first core takes in the next one over USB. Before this one core did the three one after
   another (receive, decode, send). Three receive buffers (PSRAM); each picture streams to its screen strip by strip
   as it decodes (pics.cpp).

   Rules for the first core: before it draws anything itself, uses the screens or writes flash, it waits until
   pics_idle(), taking the results meanwhile (main.cpp pics_settle: the second core stops when 16 results wait).
   Pictures still queued for a set that has gone are skipped, not drawn (pics_cancel()). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PICS_SLOTS 3
#define PICS_MAX (64 * 1024)                /* the largest picture the rig may send */

typedef struct {
	int orb, set;
	bool ok;                                 /* decoded and on its way to the glass (a live upload: applied) */
	bool skipped;                            /* its set had gone (pics_cancel) */
	uint32_t decode_us;
	uint8_t live_kind;                       /* 0 = a picture; else a live upload's kind (LIVE_UP_...) */
	uint16_t live_face;                      /* (a face pack: its id) */
	const char *live_err;                    /* (a live upload that failed: why) */
} pics_result_t;

void pics_init(void);                        /* after lcd_init and orb_fb[]: starts the second core */
uint8_t *pics_take(int *slot);               /* a free buffer to receive into, NULL if all are busy */
void pics_give_back(int slot);               /* a buffer that wasn't used after all (a dropped picture) */
void pics_submit(int slot, int orb, int set, uint32_t len, uint8_t brightness);
bool pics_result(pics_result_t *r);          /* a finished one, for the "PK" answer */
bool pics_idle(void);
void pics_wait_idle(void);
void pics_cancel(void);                      /* queued pictures are skipped (the set changed) */
bool pics_stuck(uint32_t now_ms);            /* the second core has been on one picture for over 3 s */
/* for "?" and "Q": pictures decoded, failed, decode time (average and slowest since the last call, microseconds) */
void pics_stats(uint32_t *decoded, uint32_t *failed, uint32_t *avg_us, uint32_t *max_us);

/* ---- live faces (live.h) on the second core, 2026-11-17 ----
   The rig uploads a face pack per orb ("U" kind FACE, ~300 KB, kept in PSRAM, so going back to the set draws at once),
   then text masks, patches, colour tables and arrays ("U", small) and slot values ("Y", ~40 a second). The second core
   applies uploads in order with the pictures and rebuilds only what changed, straight to the screen, with a copy in
   the orb's picture memory (the settings page). Which orbs draw their face is the first core's call (pics_live_show). */
#define LIVE_PACK_MAX (640 * 1024)
#define LIVE_PACKS 6                         /* 5 in use + 1 to receive the next into (no copying; the old one frees as it loads) */
#define LIVE_SMALL_MAX (64 * 1024)
#define LIVE_SMALLS 2
enum { LIVE_UP_FACE = 1, LIVE_UP_MASK = 2, LIVE_UP_PATCH = 3, LIVE_UP_LUT = 4, LIVE_UP_ARRAY = 5, LIVE_UP_IMAGE = 6,
       LIVE_UP_CATALOG = 7 };   /* (CATALOG: the rig's sets and faces as JSON, kept by the first core for the page) */
/* a buffer to receive an upload of that kind into (no-cache PSRAM), NULL if none is free; its id in *buf */
uint8_t *pics_live_buffer(int kind, uint32_t len, int *buf);
void pics_live_give_back(int kind, int buf);
void pics_live_upload(int buf, int orb, int kind, int index, uint32_t len, int set);
void pics_live_slots(int orb, int slot, uint16_t value);    /* (pending until the second core takes them) */
void pics_live_show(uint8_t mask, uint8_t brightness);       /* which orbs draw their live face now, at what brightness */
void pics_live_fb(bool wanted);                              /* a settings page watches: keep the picture memory */
uint16_t pics_live_face(int orb, int *set);                  /* the face loaded for that orb (0 = none) and its set */
bool pics_live_drawing(uint8_t mask);                        /* any of those orbs has been drawn since it was shown */
/* drawn updates, their average and slowest time (us) and pixels, since the last call */
void pics_live_stats(uint32_t *frames, uint32_t *avg_us, uint32_t *max_us, uint32_t *avg_px);
uint32_t pics_live_fetch_us(uint32_t *bytes);                /* time spent copying from PSRAM since the last call */
void pics_live_prof(uint32_t out[12]);
void pics_live_bench(void);                                  /* each loaded live face built whole once, nothing sent */
uint32_t pics_live_bench_result(uint32_t us[5], uint32_t prof[12]);
uint32_t pics_live_bench_fetch_us(void);                     /* of that, reading PSRAM */
void pics_live_bench_two(uint32_t us1[5], uint32_t crc[2], uint32_t *strips0);   /* one core's times; both ways' sums */
void pics_help(void);                                        /* the first core builds live strips if any are free */
void pics_help_set(bool on);                                 /* (tests: the first core helps or not; not saved) */
bool pics_help_get(void);   /* returns how many runs so far */

#ifdef __cplusplus
}
#endif
