/* Drawing into one orb's picture (240 x 240, RGB565), 2026-11-16. Plain C with no Pico calls, so tools/preview.c draws
   the same pictures on the PC into a PNG before anything goes onto the XL. Made from another project's gfx.c (the same
   blending, ring and text code, cut down to what the orbs use so far). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "fonts.h"

#define SCR_W 240
#define SCR_H 240
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* The picture keeps each pixel byte-swapped (high byte first), the order the screens take over SPI. */
extern uint16_t gfx_fb[SCR_W * SCR_H];

void gfx_clear(uint16_t c);
void gfx_backdrop(void);                                             /* the Info Orbs' dark radial background */
void gfx_rect(int x, int y, int w, int h, uint16_t c);
void gfx_ring(float cx, float cy, float r_in, float r_out, uint16_t c);   /* anti-aliased edges */
void gfx_disc(float cx, float cy, float r, uint16_t c);
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c);   /* smooth edges */
int gfx_text_w(const font_t *f, const char *s);
void gfx_text(const font_t *f, int x, int y, const char *s, uint16_t c);      /* y = the line's top */
void gfx_text_c(const font_t *f, int cx, int y, const char *s, uint16_t c);   /* centred on cx */
void gfx_text_mid(const font_t *f, int cx, int cy, const char *s, uint16_t c, char ref);  /* ref glyph's middle at cy */
