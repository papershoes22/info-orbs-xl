/* The orbs' five round screens (GC9A01, 240 x 240) on the XL, 2026-11-16 (pins: xl-case/WIRING.md).
   They share SPI0 (clock GP34, data GP7), DC GP15 and reset GP14; each has its own select line. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void lcd_init(void);                       /* resets and starts all five at once (all selected, as the original did) */
uint32_t lcd_spi_hz(void);                 /* the SPI clock the XL really gives (the nearest at or under the asked) */
uint32_t lcd_set_link(int level);            /* pictures' speed 0 41.7 / 1 62.5 MHz (Screens > Screen link) */
/* sends rows y0..y1-1 of a whole 240x240 picture (byte-swapped RGB565) to screen `orb` (0-4 = D1-D5), by DMA;
   returns when it has all gone */
void lcd_show_rows(int orb, const uint16_t *fb, int y0, int y1);
void lcd_refresh(void);
void lcd_set_rotation(int orb, int quarter_turns);   /* 0-3: that orb's screen turns its picture 0/90/180/270 degrees */                    /* every screen's settings again (5 MHz, all at once; main loop, every 5 s) */
uint32_t lcd_refreshes(void);
void lcd_test_invoff(int orb);             /* TEST ONLY: that screen's colour inversion off */
void lcd_show_rows_all(const uint16_t *fb, int y0, int y1);   /* the same rows to all five screens at once (Matrix) */
void lcd_show_rect(int orb, const uint16_t *fb, int x, int y, int w, int h);   /* a small rectangle straight out */
/* the second core's pictures (pics.cpp): the window set once, then strips of rows by DMA as they decode (each call
   waits for the strip before), the bus taken from begin to end (every other lcd_ call waits for the bus) */
void lcd_stream_begin(int orb, int w, int h);
void lcd_stream_begin_rect(int orb, int x, int y, int w, int h);   /* (live faces: a rectangle anywhere) */
/* per-orb trim (settings version 4: each orb's brightness and red/green/blue, 50-100 %), so five screens that differ a
   little can be matched; applied to everything an orb shows. lcd_trim does it to byte-swapped RGB565 in place. */
void lcd_set_trim(int orb, int bright_pc, int r_pc, int g_pc, int b_pc);
bool lcd_trimmed(int orb);
void lcd_trim(int orb, uint16_t *px, uint32_t n);
void lcd_stream_rows(const uint16_t *px, uint32_t n_pixels);
void lcd_stream_wait(void);
void lcd_stream_end(void);
