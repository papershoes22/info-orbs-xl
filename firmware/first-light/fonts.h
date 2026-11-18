/* made by tools/make_font.py (Barlow, the Info Orbs' font); do not edit */
#pragma once
#include <stdint.h>
typedef struct { uint32_t off; uint8_t w, h; int8_t x, y; uint8_t adv; } glyph_t;
typedef struct { const uint8_t *bits; const glyph_t *glyphs; uint8_t line_h, ascent; } font_t;
extern const font_t font_name;
extern const font_t font_lab;
extern const font_t font_info;
