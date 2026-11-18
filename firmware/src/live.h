/* Live faces (2026-11-17): the rig paints a face's still parts once
   (the base) and its moving parts as lit layers; the XL then shows each layer up to the value the rig sends ~40 times a
   second, rebuilding only the rectangles that changed. Plain C with no hardware in it: the same file is built for the
   PC (tools/live_host.c) so its pictures can be checked against the rig's own faces pixel by pixel.

   A face pack (little-endian), uploaded once per face design:
     header (32 bytes)  u32 'LIVE', u16 version 1, u16 face_id, u16 n_layers, u16 reserved, u32 base_off,
                        u32 layers_off, u32 total_len, u32 reserved x2
     base               240 x 240 x RGB888 (the still face, full colour; dithered to the screen's 565 on the way out)
     layers             n_layers x 64-byte records (live_layer_rec_t), their data after
   Layer kinds:
     SPAN    lit pixels as row spans, each pixel (solid RGB premultiplied, glow RGB, alpha), drawn as the rig paints:
             out = (out + glow) * (1 - alpha) + solid (a layer's glow sits under its own shape); shown where its clip says
             (ARC: up to the value's angle, with the round cap and the glow fading past the tip like a blur; BAR_X/BAR_Y:
             up to a position; NONE: by a visibility slot)
     SPRITE  a small (solid, glow, alpha) picture placed at x/y slots in 1/16 px (bilinear), visibility slot; its own
             size in r_mid (width) and width (height), its box = everywhere it can go
     CELLS   segments (a map of segment + alpha) each coloured from its value through a colour table, with its own glow
     CHART   a filled line graph drawn here from an array (scrolled by a slot), the rig's area_chart's look
     NEEDLE  a needle drawn here: from the centre out at the value's angle, soft edges and glow
     GAUGE   a gauge's dim track and its lit value band together: eight bytes a pixel (track alpha, value alpha, glow,
             track x value, glow x value: the products made at the rig's double size and shrunk, so the edges come out as
             the rig's own; then its place along the arc as u16 of 65534 (65535 = just before the start) and its radius
             from r_in in 0.25 px steps (0.5 with LIVE_F_R_HALF), worked out once by the rig) and two colour tables
             (track, value). Lit = track, then glow, then band, term for term; unlit = the track with the glow's tail;
             mixed at the moving edge
     SCROLL  a picture the rig paints (premultiplied RGB + alpha, 4 bytes a pixel) wider than its box, slid left by a
             slot (1/256 px) with 4-tap interpolation: a graph painted exactly by the rig once a second, scrolling
             smoothly between its points. aux: u16 width, u16 height, u16 room (the widest it may be replaced with),
             u16 0, then room x height x 4 bytes, each byte v = 40 + 0.78 x (x in 0..255 units; the shrink's rims under 0
             and over full kept). Replaced whole by an IMAGE upload (u16 width, then its pixels)
     DOT     a round glowing dot drawn here from its measured shape (alpha and glow by distance from its centre, in
             steps of a fraction of a pixel), at x/y slots in 1/16 px with a visibility slot: exact at any position
     TINT    like SPAN but each pixel is only (alpha, glow): its colour comes from a 256-step colour table by its place
             along the arc (or across/down the layer), so a gradient's colours can change with a 768-byte upload (the
             temperature arcs' colours follow the lights' heat scale; colour themes)
   Text and other slow parts come as uploads: MASK (an alpha mask + colour in a text slot, replaced whole) and PATCH (a
   rectangle written into the base). A mask byte v means coverage (v - 40) / 200 (-0.2 .. 1.075): the rig's
   downscaling leaves faint dark rims and bright cores around text, and they're kept so the text looks the same. */
#define LIVE_MASK_ZERO 40
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define LIVE_W 240
#define LIVE_MAGIC 0x4556494Cu           /* "LIVE" */
#define LIVE_MAX_LAYERS 16
#define LIVE_MAX_SLOTS 96
#define LIVE_MAX_MASKS 8
#define LIVE_MAX_ARRAYS 4
#define LIVE_ARRAY_MAX 128
#define LIVE_MASK_MAX (16 * 1024)        /* the largest text mask (a big number is ~110 x 70) */
#ifndef LIVE_MAX_RECTS
#define LIVE_MAX_RECTS 32                /* (16 before 2026-11-18: see LIVE_MERGE_SLACK in live.c) */
#endif

enum { LIVE_SPAN = 1, LIVE_SPRITE = 2, LIVE_CELLS = 3, LIVE_CHART = 4, LIVE_NEEDLE = 5, LIVE_TINT = 6, LIVE_DOT = 7, LIVE_GAUGE = 8, LIVE_SCROLL = 9 };
enum { LIVE_T_ANGLE = 0, LIVE_T_X = 1, LIVE_T_Y = 2, LIVE_T_ZERO = 3 };   /* TINT: where its colour comes from */
#define LIVE_NO_SLOT 0xFF
enum { LIVE_CLIP_NONE = 0, LIVE_CLIP_ARC = 1, LIVE_CLIP_BAR_X = 2, LIVE_CLIP_BAR_Y = 3 };
enum { LIVE_F_ROUND_CAP = 1, LIVE_F_BAR_REVERSE = 2, LIVE_F_R_HALF = 4 };   /* (GAUGE radius steps of 0.5 px, else 0.25) */

/* the 64-byte layer record, exactly as in the pack */
typedef struct __attribute__((packed)) {
	uint8_t kind, clip, s0, s1, s2, s3, flags, aux8;
	int16_t x, y, w, h;                    /* where its pixels can be (screen px) */
	float cx, cy, a0, sweep;               /* ARC/NEEDLE: centre, start angle and full sweep (degrees, 0 = 3 o'clock,
	                                          clockwise as on the screen) */
	float r_mid, width, sigma, r_in;       /* the band's middle radius and width (the cap), the glow's blur, the inner */
	float r_out;                           /* and outer radius any of its pixels reach */
	uint32_t data_off, data_len;           /* SPAN/CELLS: row table + rows; SPRITE: w*h pixels */
	uint32_t aux_off;                      /* CELLS: colour tables + glow fields; CHART/NEEDLE: their parameters */
} live_layer_rec_t;

typedef struct {
	int16_t x, y, w, h;                    /* w == 0: empty */
	uint8_t r, g, b;
	uint32_t off;                          /* where its alpha bytes are in the mask store */
} live_mask_t;

typedef struct { int16_t x0, y0, x1, y1; } live_rect_t;    /* x1/y1 exclusive */

/* one orb's live face: the pack (wherever it's kept: on the XL, PSRAM read through its no-cache window), the slots,
   the text masks and the rectangles waiting to be rebuilt */
typedef struct {
	const uint8_t *pack;                   /* NULL: no face */
	uint32_t pack_len;
	uint16_t face_id;
	int n_layers;
	live_layer_rec_t layer[LIVE_MAX_LAYERS];
	uint16_t slot[LIVE_MAX_SLOTS];
	uint8_t array[LIVE_MAX_ARRAYS][LIVE_ARRAY_MAX];
	uint8_t array_n[LIVE_MAX_ARRAYS];
	live_mask_t mask[LIVE_MAX_MASKS];
	uint8_t *mask_store;                   /* LIVE_MAX_MASKS * LIVE_MASK_MAX bytes (the caller's) */
	uint8_t *base_rw;                      /* the base, writable (PATCH writes here; == pack + base_off) */
	float m_in[LIVE_MAX_LAYERS], m_out[LIVE_MAX_LAYERS];   /* ARC layers: how far before / past the tip a change
	                                                          shows (px along the arc; from the measured glow fall-off) */
	live_rect_t dirty[LIVE_MAX_RECTS];
	int n_dirty;
	uint8_t brightness;                    /* 255 = full */
} live_face_t;

/* a pack is checked and its layer records copied in; everything is dirty. false: not a pack (why in *err). */
bool live_load(live_face_t *f, const uint8_t *pack, uint32_t len, uint8_t *mask_store, const char **err);
void live_unload(live_face_t *f);
/* new slot values from the rig: only what changes is marked dirty */
void live_set_slots(live_face_t *f, int first, int n, const uint16_t *v);
void live_set_array(live_face_t *f, int idx, const uint8_t *v, int n);
/* a text mask: x, y, w, h, colour, alpha bytes (w*h); w == 0 clears the slot */
bool live_set_mask(live_face_t *f, int slot, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, const uint8_t *alpha);
/* a patch: w*h RGB888 written into the base */
bool live_patch(live_face_t *f, int x, int y, int w, int h, const uint8_t *rgb);
/* a TINT layer's colour table (256 x RGB) replaced */
bool live_set_lut(live_face_t *f, int layer, const uint8_t *rgb768);
/* a SCROLL layer's picture replaced: w x its height x 4 bytes */
bool live_set_image(live_face_t *f, int layer, int w, const uint8_t *px, uint32_t len);
void live_mark_all(live_face_t *f);
void live_mark(live_face_t *f, int x0, int y0, int x1, int y1);
/* builds rows [y, y+n) of the rectangle x0..x1 into out (RGB565, byte-swapped for the screens when swap), dithered;
   row stride = x1 - x0 */
void live_compose(const live_face_t *f, int x0, int x1, int y, int n, uint16_t *out, bool swap);
/* the same as RGB888 (the host's check, the page's picture) */
void live_compose888(const live_face_t *f, int x0, int x1, int y, int n, uint8_t *out);

#ifdef __cplusplus
}
#endif
