#include "screen.h"
#include "assets.h"

/* the original's colour helpers, unchanged (Utils::rgb565dim etc., declared in screen.h) */
#include "../third_party/orbs_utils.inc"

extern "C" void lcd_show_rows(int orb, const uint16_t *fb, int y0, int y1);
extern "C" void lcd_show_rows_all(const uint16_t *fb, int y0, int y1);
extern "C" void lcd_show_rect(int orb, const uint16_t *fb, int x, int y, int w, int h);

struct TTF_FontMetric {
	TTF_Font font;
	float scale;
};
/* the original's include/ttf-fonts.h */
static const TTF_FontMetric ttfFontMetrics[] = { { ROBOTO_REGULAR, 1.37f }, { FINAL_FRONTIER, 1.5f } };

static ScreenManager *s_instance;
static uint8_t s_workspace[TJPGD_WORKSPACE_SIZE] __attribute__((aligned(4)));

ScreenManager::ScreenManager(TFT_eSPI &tft) : m_tft(tft) {}

void ScreenManager::begin()
{
	s_instance = this;
	m_render.setCacheSize(8, 8, 4096);      /* (the original's choice) */
	setFont(DEFAULT_FONT);
	m_render.setDrawer(m_tft);
}

void ScreenManager::setFont(TTF_Font font)
{
	if(font == m_curFont)
		return;
	m_render.unloadFont();
	m_curFont = NONE;
	if(font == NONE)
		return;
	FT_Error error = 1;
	switch(font)
	{
	case ROBOTO_REGULAR: error = m_render.loadFont(font_roboto, sizeof font_roboto); break;
	case FINAL_FRONTIER: error = m_render.loadFont(font_final_frontier, sizeof font_final_frontier); break;
	case DSEG7: error = m_render.loadFont(font_dseg7, sizeof font_dseg7); break;
	case DSEG14: error = m_render.loadFont(font_dseg14, sizeof font_dseg14); break;
	default: break;
	}
	if(error == 0)
		m_curFont = font;
	else
		printf("EV ERROR font %d didn't load (%d)\n", (int)font, (int)error);
}

/* orbRotation 1 or 2 shows the sets from right to left (the original's selectScreen). With a face map (setFaceMap) the
   screen goes to the orb(s) it was placed on, and to none if it was placed nowhere. */
void ScreenManager::selectScreen(int screen)
{
	bool rotateDisplays = m_orbRotation == 1 || m_orbRotation == 2;
	uint8_t mask = 0;
	for(int i = 0; i < NUM_SCREENS; i++)
	{
		int currentDisplay = rotateDisplays ? NUM_SCREENS - i - 1 : i;
		if(m_faces ? m_faces[i] == screen : i == screen)
			mask |= (uint8_t)(1u << currentDisplay);
	}
	m_tft.select(mask);
}

void ScreenManager::selectAllScreens() { m_tft.select(0x1F); }
void ScreenManager::reset() { m_tft.select(0); }

void ScreenManager::fillAllScreens(uint32_t color)
{
	selectAllScreens();
	fillScreen(color);
	reset();
}

void ScreenManager::clearAllScreens() { fillAllScreens(TFT_BLACK); }

void ScreenManager::clearScreen(int screen)
{
	if(screen >= 0)
		selectScreen(screen);
	fillScreen(TFT_BLACK);
}

void ScreenManager::fillScreen(uint32_t color)
{
	m_tft.fillScreen(dim((uint16_t)color));
	m_render.setBackgroundColor(dim((uint16_t)color));
}

bool ScreenManager::setBrightness(uint8_t brightness)
{
	if(m_brightness != brightness)
	{
		m_brightness = brightness;
		return true;
	}
	return false;
}

void ScreenManager::setFontColor(uint32_t color) { m_render.setFontColor(dim((uint16_t)color)); }
void ScreenManager::setFontColor(uint32_t color, uint32_t background)
{
	m_render.setFontColor(dim((uint16_t)color));
	m_render.setBackgroundColor(dim((uint16_t)background));
}
void ScreenManager::setBackgroundColor(uint32_t color) { m_render.setBackgroundColor(dim((uint16_t)color)); }
void ScreenManager::setAlignment(Align align) { m_render.setAlignment(align); }
void ScreenManager::setFontSize(uint32_t size) { m_render.setFontSize(size); }

unsigned int ScreenManager::getScaledFontSize(unsigned int fontSize)
{
	for(const TTF_FontMetric &metric : ttfFontMetrics)
		if(metric.font == m_curFont)
			return (unsigned int)lroundf(metric.scale * fontSize);
	return fontSize;
}

void ScreenManager::drawString(const String &text, int x, int y)
{
	drawString(text, x, y, 0, m_render.getAlignment());
}

/* the original's, line for line, including its "dirty hack to correct misaligned Y"
   (https://github.com/takkaO/OpenFontRender/issues/38) */
void ScreenManager::drawString(const String &text, int x, int y, unsigned int fontSize, Align align, int32_t fgColor, int32_t bgColor, bool applyScale)
{
	if(fontSize == 0)
		fontSize = m_render.getFontSize();
	else if(applyScale)
		fontSize = getScaledFontSize(fontSize);
	if(fgColor == -1)
		fgColor = m_render.getFontColor();
	else
		fgColor = dim((uint16_t)fgColor);
	if(bgColor == -1)
		bgColor = m_render.getBackgroundColor();
	else
		bgColor = dim((uint16_t)bgColor);
	FT_BBox box = m_render.calculateBoundingBox(0, 0, fontSize, Align::TopLeft, Layout::Horizontal, text.c_str());
	m_render.setAlignment(align);
	m_render.setFontSize(fontSize);
	m_render.drawString(text.c_str(), x, y - box.yMin, (uint16_t)fgColor, (uint16_t)bgColor);
}

/* the original's, unchanged */
unsigned int ScreenManager::calculateFitFontSize(uint32_t limit_width, uint32_t limit_height, Layout layout, const String &text)
{
	return m_render.calculateFitFontSize(limit_width, limit_height, layout, text.c_str());
}

void ScreenManager::drawCentreString(const String &text, int x, int y, unsigned int fontSize)
{
	drawString(text, x, y, fontSize, Align::MiddleCenter);
}

void ScreenManager::drawFittedString(const String &text, int x, int y, int limit_w, int limit_h, Align align)
{
	unsigned int fontSize = calculateFitFontSize(limit_w, limit_h, Layout::Horizontal, text);
	drawString(text, x, y, fontSize, align, -1, -1, false);
}

void ScreenManager::drawFittedString(const String &text, int x, int y, int limit_w, int limit_h)
{
	drawFittedString(text, x, y, limit_w, limit_h, m_render.getAlignment());
}

void ScreenManager::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
	m_tft.fillRect(x, y, w, h, dim((uint16_t)color));
	if(m_immediate)
		for(int o = 0; o < N_ORB_FB; o++)
			if(m_tft.selected() == (1u << o))
				lcd_show_rect(o, orb_fb[o], x, y, w, h);
}

void ScreenManager::dropDirty()
{
	for(int o = 0; o < N_ORB_FB; o++)
	{
		orb_dirty[o].y0 = 1;
		orb_dirty[o].y1 = 0;
	}
}

void ScreenManager::drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color, bool roundEnds)
{
	m_tft.drawSmoothArc(x, y, r, ir, startAngle, endAngle, dim((uint16_t)fg_color), dim((uint16_t)bg_color), roundEnds);
}

/* TJpg_Decoder's array input and output (its jd_input / jd_output), then the original's tftOutput */
size_t ScreenManager::jpgInput(JDEC *jd, uint8_t *buf, size_t len)
{
	(void)jd;
	ScreenManager *t = s_instance;
	if(t->m_jpgPos + len > t->m_jpgSize)
		len = t->m_jpgSize - t->m_jpgPos;
	if(buf)
		memcpy(buf, t->m_jpg + t->m_jpgPos, len);
	t->m_jpgPos += len;
	return len;
}

int ScreenManager::jpgOutput(JDEC *jd, void *bitmap, JRECT *rect)
{
	(void)jd;
	ScreenManager *t = s_instance;
	int16_t x = (int16_t)(rect->left + t->m_jpgX), y = (int16_t)(rect->top + t->m_jpgY);
	uint16_t w = (uint16_t)(rect->right + 1 - rect->left), h = (uint16_t)(rect->bottom + 1 - rect->top);
	return t->tftOutput(x, y, w, h, (uint16_t *)bitmap) ? 1 : 0;
}

bool ScreenManager::tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
	if(y >= m_tft.height() || x >= m_tft.width())
		return m_jpgKeepGoing;                 /* (the original stops the whole picture here; drawJpgCropped goes on) */
	if(m_imageColor != 0)
		Utils::colorizeImageData(bitmap, (size_t)w * h, m_imageColor, 1.25, true);
	if(m_brightness != 255)
		Utils::rgb565dimBitmap(bitmap, (size_t)w * h, m_brightness, true);
	m_tft.pushImage(x, y, w, h, bitmap);
	return true;
}

JRESULT ScreenManager::drawJpg(int32_t x, int32_t y, const uint8_t jpeg_data[], uint32_t data_size, uint8_t scale, uint32_t imageColor)
{
	JDEC jdec;
	m_imageColor = imageColor;
	m_jpg = jpeg_data;
	m_jpgSize = data_size;
	m_jpgPos = 0;
	m_jpgX = x;
	m_jpgY = y;
	jdec.swap = 1;                         /* TJpgDec.setSwapBytes(true), as the original */
	uint8_t s = 0;                         /* TJpgDec's setJpgScale: 1 -> 0, 2 -> 1, 4 -> 2, 8 -> 3 */
	while((1u << s) < scale && s < 3)
		s++;
	JRESULT r = jd_prepare(&jdec, jpgInput, s_workspace, TJPGD_WORKSPACE_SIZE, nullptr);
	if(r == JDR_OK)
		r = jd_decomp(&jdec, jpgOutput, s);
	m_imageColor = 0;
	return r;
}

/* a picture wider or taller than the orb, cut to it: the original's drawJpg stops decoding at the first block past the
   right edge, so a picture centred on the orb (Frigate's are 426 wide) showed only its top strip (2026-11-17) */
JRESULT ScreenManager::drawJpgCropped(int32_t x, int32_t y, const uint8_t jpeg_data[], uint32_t data_size)
{
	m_jpgKeepGoing = true;
	JRESULT r = drawJpg(x, y, jpeg_data, data_size);
	m_jpgKeepGoing = false;
	return r;
}

int ScreenManager::flush()
{
	if(m_tft.mirror())
	{
		/* one picture, all five screens selected at once */
		OrbDirty &d = orb_dirty[0];
		if(d.y0 > d.y1)
			return 0;
		lcd_show_rows_all(orb_fb[0], d.y0, d.y1 + 1);
		d.y0 = 1;
		d.y1 = 0;
		return 5;
	}
	int n = 0;
	for(int o = 0; o < N_ORB_FB; o++)
	{
		OrbDirty &d = orb_dirty[o];
		if(d.y0 > d.y1)
			continue;
		lcd_show_rows(o, orb_fb[o], d.y0, d.y1 + 1);
		d.y0 = 1;
		d.y1 = 0;
		n++;
	}
	return n;
}
