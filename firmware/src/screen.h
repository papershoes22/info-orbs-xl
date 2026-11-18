/* The orbs' ScreenManager (2026-11-16): the same calls as the original firmware's
   (firmware/src/core/screenmanager/ScreenManager.cpp), doing the same thing, drawn into each orb's picture
   (tft.h) and sent to the glass by flush(). Text is OpenFontRender with the original's TTF fonts and its "misaligned
   Y" correction; pictures are TJpgDec (Bodmer's copy) through the original's tftOutput (colourise, dim). */
#pragma once
#include "compat.h"
#include "tft.h"
#include "../third_party/OpenFontRender/OpenFontRender.h"
extern "C" {
#include "../third_party/tjpgd_bodmer/tjpgd.h"
}

#define NUM_SCREENS 5
#define SCREEN_SIZE 240

enum TTF_Font { NONE, ROBOTO_REGULAR, FINAL_FRONTIER, DSEG7, DSEG14 };
#define DEFAULT_FONT ROBOTO_REGULAR

namespace Utils {
uint16_t rgb888ToRgb565(uint32_t rgb888, bool swapBytes = false);
uint16_t rgb565dim(uint16_t rgb565, uint8_t brightness, bool swapBytes = false);
void rgb565dimBitmap(uint16_t *pixel565, size_t length, uint8_t brightness, bool swapBytes = true);
uint32_t rgb565ToRgb888(uint16_t rgb565, bool swapBytes = false);
uint16_t grayscaleToTargetColor(uint8_t grayscale, uint8_t targetR8, uint8_t targetG8, uint8_t targetB8, float brightness, bool swapBytes = false);
void colorizeImageData(uint16_t *pixels565, size_t length, uint32_t targetColor888, float brightness, bool swapBytes = true);
}

class ScreenManager {
public:
	explicit ScreenManager(TFT_eSPI &tft);
	void begin();                                   /* fonts and the font cache, as the original's constructor */

	void selectScreen(int screen);
	void selectAllScreens();
	void reset();
	void fillAllScreens(uint32_t color);
	void clearAllScreens();
	void clearScreen(int screen = -1);
	void fillScreen(uint32_t color);

	void setFont(TTF_Font font);
	void setFontColor(uint32_t color);
	void setFontColor(uint32_t color, uint32_t background);
	void setBackgroundColor(uint32_t color);
	void setAlignment(Align align);
	void setFontSize(uint32_t size);
	void drawString(const String &text, int x, int y, unsigned int fontSize, Align align, int32_t fgColor = -1, int32_t bgColor = -1, bool applyScale = true);
	void drawString(const String &text, int x, int y);
	void drawCentreString(const String &text, int x, int y, unsigned int fontSize = 0);
	void drawFittedString(const String &text, int x, int y, int limit_w, int limit_h, Align align);
	void drawFittedString(const String &text, int x, int y, int limit_w, int limit_h);
	unsigned int calculateFitFontSize(uint32_t limit_width, uint32_t limit_height, Layout layout, const String &text);
	void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);

	/* the original's TFT_eSPI text pass-throughs (the Matrix) */
	int16_t width() { return m_tft.width(); }
	int16_t height() { return m_tft.height(); }
	void setTextColor(uint16_t c) { m_tft.setTextColor(c); }
	void setTextColor(uint16_t c, uint16_t b) { m_tft.setTextColor(c, b); }
	uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { return m_tft.color565(r, g, b); }
	void setCursor(int16_t x, int16_t y) { m_tft.setCursor(x, y); }
	void setTextSize(uint8_t s) { m_tft.setTextSize(s); }
	void print(const String &s) { m_tft.print(s); }
	void print(char c) { m_tft.print(c); }
	void setMirror(bool on) { m_tft.setMirror(on); }
	/* immediate mode (the visualizer): each fillRect on one orb goes straight to its screen; dropDirty() then
	   forgets the rows marked meanwhile (they're already on the glass) */
	void setImmediate(bool on) { m_immediate = on; }
	void dropDirty();

	void drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color, bool roundEnds = false);
	JRESULT drawJpg(int32_t x, int32_t y, const uint8_t jpeg_data[], uint32_t data_size, uint8_t scale = 1, uint32_t imageColor = 0);
	JRESULT drawJpgCropped(int32_t x, int32_t y, const uint8_t jpeg_data[], uint32_t data_size);   /* bigger than the orb: cut */

	bool setBrightness(uint8_t brightness);
	uint8_t getBrightness() { return m_brightness; }
	void setOrbRotation(int r) { m_orbRotation = r; }
	/* which of a set's own five screens each orb shows (faces[orb] = screen number, 0xFF = none), while the XL draws
	   Weather, Hourly or Sky (the page's "Which face on which orb", 2026-11-18); nullptr = orb n shows screen n */
	void setFaceMap(const uint8_t *faces) { m_faces = faces; }

	TFT_eSPI &getDisplay() { return m_tft; }
	OpenFontRender &getRender() { return m_render; }

	/* sends every orb's drawn-on rows to the glass (lcd.h); returns how many orbs went */
	int flush();

private:
	TFT_eSPI &m_tft;
	OpenFontRender m_render;
	TTF_Font m_curFont = NONE;
	uint8_t m_brightness = 255;
	uint32_t m_imageColor = 0;
	int m_orbRotation = 0;
	const uint8_t *m_faces = nullptr;
	bool m_immediate = false;
	bool m_jpgKeepGoing = false;
	unsigned int getScaledFontSize(unsigned int fontSize);
	uint16_t dim(uint16_t color) { return Utils::rgb565dim(color, m_brightness); }
	static size_t jpgInput(JDEC *jd, uint8_t *buf, size_t len);
	static int jpgOutput(JDEC *jd, void *bitmap, JRECT *rect);
	bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);
	const uint8_t *m_jpg = nullptr;
	uint32_t m_jpgSize = 0, m_jpgPos = 0;
	int32_t m_jpgX = 0, m_jpgY = 0;
};
