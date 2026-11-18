/* A stand-in for TFT_eSPI (2026-11-16): the same drawing calls, drawn into each orb's picture in the XL's PSRAM
   instead of straight onto the glass. Its smooth arcs, circles and lines are TFT_eSPI's own code, copied unchanged
   (third_party/tft_espi_smooth.inc), so they come out pixel for pixel as they did on the ESP32. Which orbs a call
   draws on is set by select() (one, or all five at once, as the original's selectScreen / selectAllScreens). The rows
   each orb has had drawn on are tracked, so only those go to the glass (screen.cpp, flush()). */
#pragma once
#include "compat.h"

#define N_ORB_FB 5
#define ORB_W 240
#define ORB_H 240

extern uint16_t *orb_fb[N_ORB_FB];          /* each orb's picture, byte-swapped RGB565 (the order the screens take) */

struct OrbDirty {
	int16_t y0, y1;                        /* rows drawn on since the last flush, y0 > y1 = none */
};
extern OrbDirty orb_dirty[N_ORB_FB];

class TFT_eSPI {
public:
	void select(uint8_t mask) { m_mask = mask; }    /* bit n = orb n */
	/* mirror mode (the Matrix set): drawing on all five goes to orb 0's picture only, and screen.cpp's flush sends it
	   to all five screens at once, as the ESP32 did with every select line down */
	void setMirror(bool on) { m_mirror = on; }
	bool mirror() const { return m_mirror; }
	uint8_t selected() const { return m_mask; }

	void drawPixel(int32_t x, int32_t y, uint32_t color);
	uint16_t drawPixel(int32_t x, int32_t y, uint32_t color, uint8_t alpha, uint32_t bg_color = 0x00FFFFFF);
	void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color);
	void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color);
	void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
	void fillScreen(uint32_t color) { fillRect(0, 0, ORB_W, ORB_H, color); }
	uint16_t readPixel(int32_t x, int32_t y);
	void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *swapped);   /* already byte-swapped */
	void startWrite() {}
	void endWrite() {}
	int16_t width() const { return ORB_W; }
	int16_t height() const { return ORB_H; }

	/* TFT_eSPI's own (third_party/tft_espi_smooth.inc) */
	void drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color, bool roundEnds = false);
	void drawArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color, bool smoothArc = true);
	void drawSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t fg_color, uint32_t bg_color);
	void fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg_color = 0x00FFFFFF);
	void drawSmoothRoundRect(int32_t x, int32_t y, int32_t r, int32_t ir, int32_t w, int32_t h, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF, uint8_t quadrants = 0xF);
	void fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color, uint32_t bg_color = 0x00FFFFFF);
	void drawSpot(float ax, float ay, float r, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF);
	void drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF);
	void drawWedgeLine(float ax, float ay, float bx, float by, float aw, float bw, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF);
	uint16_t alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc);

	/* TFT_eSPI's built-in "classic" 5x7 font (font 1, Adafruit's glcdfont), as the Matrix prints with it */
	void setTextColor(uint16_t c) { textcolor = textbgcolor = c; }
	void setTextColor(uint16_t c, uint16_t b) { textcolor = c; textbgcolor = b; }
	void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
	void setTextSize(uint8_t s) { textsize = s ? s : 1; }
	uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
	void print(const String &s) { for(unsigned i = 0; i < s.length(); i++) write((uint8_t)s.charAt(i)); }
	void print(char c) { write((uint8_t)c); }
	size_t write(uint8_t c);
	void drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size);

private:
	uint8_t m_mask = 0;
	bool m_mirror = false;
	uint8_t targets() const { return m_mirror && m_mask == 0x1F ? 0x01 : m_mask; }
	uint32_t textcolor = 0xFFFF, textbgcolor = 0xFFFF;
	int32_t cursor_x = 0, cursor_y = 0;
	uint8_t textsize = 1;
	/* what the copied code expects of TFT_eSPI */
	bool inTransaction = false, lockTransaction = false, _vpOoB = false;
	void end_tft_write() {}
	void begin_nin_write() {}
	void end_nin_write() {}
	bool clipWindow(int32_t *xs, int32_t *ys, int32_t *xe, int32_t *ye);
	uint8_t sqrt_fraction(uint32_t num);
	float wedgeLineDistance(float xpax, float ypay, float bax, float bay, float dr);
	void mark(int orb, int y0, int y1);
};
