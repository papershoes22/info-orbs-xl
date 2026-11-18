#include "sets_new.h"
#include "assets.h"
#include <cmath>

#define GREY 0x8410                  /* the status text grey */
#define DIMGREY 0x4208

static String time_text(int64_t local, bool twentyFour)
{
	int64_t s = local % 86400;
	if(s < 0)
		s += 86400;
	int h = (int)(s / 3600), m = (int)(s % 3600 / 60);
	char b[16];
	if(twentyFour)
		snprintf(b, sizeof b, "%d:%02d", h, m);
	else
		snprintf(b, sizeof b, "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h >= 12 ? "PM" : "AM");
	return String(b);
}

static String hour_text(int64_t local, bool twentyFour)
{
	int64_t s = local % 86400;
	if(s < 0)
		s += 86400;
	int h = (int)(s / 3600);
	char b[16];
	if(twentyFour)
		snprintf(b, sizeof b, "%02d:00", h);
	else
		snprintf(b, sizeof b, "%d %s", h % 12 == 0 ? 12 : h % 12, h >= 12 ? "PM" : "AM");
	return String(b);
}

/* the original's icon choice (WeatherWidget::drawWeatherIcon, dark set) for an OpenWeatherMap icon code */
static void icon_for(const String &owm, const uint8_t **p, size_t *n)
{
	String c = owm.substring(0, 2);
	bool night = owm.length() > 2 && owm.charAt(2) == 'n';
	if(c == "01") { *p = night ? wx_moonB : wx_sunB; *n = night ? sizeof wx_moonB : sizeof wx_sunB; }
	else if(c == "02" || c == "03" || c == "04") { *p = night ? wx_moonCloudB : wx_sunCloudsB; *n = night ? sizeof wx_moonCloudB : sizeof wx_sunCloudsB; }
	else if(c == "09" || c == "10" || c == "11") { *p = wx_rainB; *n = sizeof wx_rainB; }
	else if(c == "13") { *p = wx_snowB; *n = sizeof wx_snowB; }
	else { *p = wx_cloudsB; *n = sizeof wx_cloudsB; }
}

static String deg0(float v)
{
	char b[16];
	snprintf(b, sizeof b, "%.0f", (double)v);
	return String(b) + "°";
}

/* ---- Hourly ---- */
void HourlyWidget::draw(bool twentyFour, int32_t utcToLocal, bool force)
{
	if(!force && model.version == m_shownVersion)
		return;
	m_shownVersion = model.version;
	m_manager.setFont(ROBOTO_REGULAR);
	for(int o = 0; o < 5; o++)
	{
		int i = o + 1;                           /* hourly[0] is the hour we're in: the next five */
		m_manager.selectScreen(o);
		m_manager.fillScreen(TFT_BLACK);
		m_manager.setFontColor(TFT_WHITE, TFT_BLACK);
		m_manager.drawString(hour_text(model.hourDt[i] + utcToLocal, twentyFour), 120, 38, 18, Align::MiddleCenter);
		const uint8_t *p;
		size_t n;
		icon_for(model.hourIcon[i], &p, &n);
		m_manager.drawJpg(60, 50, p, (uint32_t)n, 2);            /* the original's icon at half size */
		m_manager.drawString(deg0(model.hourTemp[i]), 120, 182, 30, Align::MiddleCenter);
		int pop = (int)lroundf(model.hourPop[i] * 100);
		m_manager.setFontColor(pop >= 30 ? 0x6D9F : GREY, TFT_BLACK);   /* (a soft blue once rain is likely) */
		m_manager.drawString(String("Rain ") + String(pop) + "%", 120, 214, 13, Align::MiddleCenter);
	}
}

/* ---- Sky ---- */
const char *moon_phase_name(float p)
{
	if(p < 0.03f || p > 0.97f) return "New Moon";
	if(p < 0.22f) return "Waxing Crescent";
	if(p < 0.28f) return "First Quarter";
	if(p < 0.47f) return "Waxing Gibbous";
	if(p < 0.53f) return "Full Moon";
	if(p < 0.72f) return "Waning Gibbous";
	if(p < 0.78f) return "Last Quarter";
	return "Waning Crescent";
}

int moon_illumination(float p)
{
	return (int)lroundf((1.0f - cosf(2.0f * (float)M_PI * p)) * 50.0f);
}

static inline uint16_t mix565(uint16_t a, uint16_t b, int t)     /* t 0..16 */
{
	int r = (((a >> 11) & 31) * (16 - t) + ((b >> 11) & 31) * t) / 16;
	int g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16;
	int bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
	return (uint16_t)(r << 11 | g << 5 | bl);
}

/* the moon as it looks tonight: lit part pale, dark part a dim disc, edges smoothed (4 x 4 samples a pixel) */
static void draw_moon(TFT_eSPI &tft, int cx, int cy, int R, float phase)
{
	const uint16_t lit = 0xEF5B, dark = 0x18E3, bg = TFT_BLACK;
	float k = cosf(2.0f * (float)M_PI * phase);
	bool waxing = phase <= 0.5f;
	for(int y = -R - 1; y <= R + 1; y++)
		for(int x = -R - 1; x <= R + 1; x++)
		{
			int in = 0, on = 0;
			for(int sy = 0; sy < 4; sy++)
				for(int sx = 0; sx < 4; sx++)
				{
					float fx = (x + (sx + 0.5f) / 4.0f - 0.5f) / R, fy = (y + (sy + 0.5f) / 4.0f - 0.5f) / R;
					float r2 = fx * fx + fy * fy;
					if(r2 > 1.0f)
						continue;
					in++;
					float half = sqrtf(1.0f - fy * fy);
					bool l = waxing ? fx > k * half : fx < -k * half;
					on += l;
				}
			if(!in)
				continue;
			uint16_t c = mix565(dark, lit, on * 16 / in);
			tft.drawPixel(cx + x, cy + y, mix565(bg, c, in));
		}
}

static const char *uv_level(float uv, uint16_t *col)
{
	if(uv < 3) { *col = 0x4FE0; return "Low"; }
	if(uv < 6) { *col = 0xFFE0; return "Moderate"; }
	if(uv < 8) { *col = 0xFC60; return "High"; }
	if(uv < 11) { *col = 0xF800; return "Very High"; }
	*col = 0xB01F;
	return "Extreme";
}

static const char *aqi_level(int a, uint16_t *col)
{
	switch(a)
	{
	case 1: *col = 0x4FE0; return "Good";
	case 2: *col = 0xAFE0; return "Fair";
	case 3: *col = 0xFFE0; return "Moderate";
	case 4: *col = 0xFC60; return "Poor";
	case 5: *col = 0xF800; return "Very Poor";
	default: *col = GREY; return "-";
	}
}

void SkyWidget::draw(bool twentyFour, int32_t utcToLocal, int64_t now, bool force)
{
	/* again each minute (the sun's place on its arc), on new weather, on new air quality */
	if(!force && now / 60 == m_shownMinute && model.version == m_shownVersion && extra.aqi == m_shownAqi)
		return;
	m_shownMinute = now / 60;
	m_shownVersion = model.version;
	m_shownAqi = extra.aqi;
	TFT_eSPI &tft = m_manager.getDisplay();
	m_manager.setFont(ROBOTO_REGULAR);

	/* D1 sunrise + day length, D2 sunset: the sun's path across the day as an arc, a dot where it is now */
	for(int o = 0; o < 2; o++)
	{
		m_manager.selectScreen(o);
		m_manager.fillScreen(TFT_BLACK);
		const uint16_t sun = 0xFE60, path = 0x3186;
		m_manager.drawSmoothArc(120, 150, 92, 89, 90, 270, path, TFT_BLACK);   /* the half-circle path, horizon below */
		tft.drawFastHLine(20, 150, 200, DIMGREY);
		float f = model.sunset > model.sunrise ? (float)(now - model.sunrise) / (float)(model.sunset - model.sunrise) : -1;
		if(f >= 0 && f <= 1)
		{
			float a = (float)M_PI * (1.0f - f);
			tft.fillSmoothCircle(120 + (int)lroundf(90.5f * cosf(a)), 150 - (int)lroundf(90.5f * sinf(a)), 7, sun, TFT_BLACK);
		}
		m_manager.setFontColor(GREY, TFT_BLACK);
		m_manager.drawString(o == 0 ? "Sunrise" : "Sunset", 120, 100, 16, Align::MiddleCenter);
		m_manager.setFontColor(TFT_WHITE, TFT_BLACK);
		m_manager.drawString(time_text((o == 0 ? model.sunrise : model.sunset) + utcToLocal, twentyFour), 120, 130, 26, Align::MiddleCenter);
		if(o == 0)
		{
			int64_t len = model.sunset - model.sunrise;
			char b[32];
			snprintf(b, sizeof b, "Daylight %dh %02dm", (int)(len / 3600), (int)(len % 3600 / 60));
			m_manager.setFontColor(GREY, TFT_BLACK);
			m_manager.drawString(b, 120, 180, 13, Align::MiddleCenter);
		}
		else
		{
			int64_t left = model.sunset - now;
			char b[32];
			if(left > 0)
				snprintf(b, sizeof b, "in %dh %02dm", (int)(left / 3600), (int)(left % 3600 / 60));
			else
				snprintf(b, sizeof b, "the sun has set");
			m_manager.setFontColor(GREY, TFT_BLACK);
			m_manager.drawString(b, 120, 180, 13, Align::MiddleCenter);
		}
	}

	/* D3 the moon */
	m_manager.selectScreen(2);
	m_manager.fillScreen(TFT_BLACK);
	draw_moon(tft, 120, 100, 52, model.moonPhase);
	m_manager.setFontColor(TFT_WHITE, TFT_BLACK);
	m_manager.drawString(moon_phase_name(model.moonPhase), 120, 178, 15, Align::MiddleCenter);
	m_manager.setFontColor(GREY, TFT_BLACK);
	m_manager.drawString(String(moon_illumination(model.moonPhase)) + "% lit", 120, 204, 13, Align::MiddleCenter);

	/* D4 UV index (today's highest), a ring coloured by its level */
	{
		uint16_t col;
		const char *lvl = uv_level(model.uvi, &col);
		m_manager.selectScreen(3);
		m_manager.fillScreen(TFT_BLACK);
		int span = (int)lroundf(fminf(model.uvi, 11.0f) / 11.0f * 300.0f);
		m_manager.drawSmoothArc(120, 120, 104, 96, 30, 330, 0x2104, TFT_BLACK);
		if(span > 0)
			m_manager.drawSmoothArc(120, 120, 104, 96, 30, 30 + span, col, TFT_BLACK, true);
		m_manager.setFontColor(GREY, TFT_BLACK);
		m_manager.drawString("UV today", 120, 72, 15, Align::MiddleCenter);
		m_manager.setFontColor(TFT_WHITE, TFT_BLACK);
		char b[16];
		snprintf(b, sizeof b, "%.0f", (double)model.uvi);
		m_manager.drawString(b, 120, 122, 48, Align::MiddleCenter);
		m_manager.setFontColor(col, TFT_BLACK);
		m_manager.drawString(lvl, 120, 168, 16, Align::MiddleCenter);
	}

	/* D5 air quality */
	{
		uint16_t col;
		const char *lvl = aqi_level(extra.aqi, &col);
		m_manager.selectScreen(4);
		m_manager.fillScreen(TFT_BLACK);
		tft.fillSmoothCircle(120, 96, 26, col, TFT_BLACK);
		m_manager.setFontColor(GREY, TFT_BLACK);
		m_manager.drawString("Air quality", 120, 48, 15, Align::MiddleCenter);
		m_manager.setFontColor(TFT_WHITE, TFT_BLACK);
		m_manager.drawString(lvl, 120, 150, 22, Align::MiddleCenter);
		m_manager.setFontColor(GREY, TFT_BLACK);
		if(extra.aqi)
		{
			/* "PM2.5 6 µg/m" and a small raised 3 (the original's Roboto has no ³) */
			char b[32];
			snprintf(b, sizeof b, "PM2.5 %.0f µg/m", (double)extra.pm25);
			OpenFontRender &r = m_manager.getRender();
			FT_BBox b1 = r.calculateBoundingBox(0, 0, 16, Align::TopLeft, Layout::Horizontal, b);      /* (12 x Roboto's 1.37) */
			FT_BBox b3 = r.calculateBoundingBox(0, 0, 11, Align::TopLeft, Layout::Horizontal, "3");    /* (8 x 1.37) */
			int w1 = (int)(b1.xMax - b1.xMin), w3 = (int)(b3.xMax - b3.xMin);
			int x0 = 120 - (w1 + 1 + w3) / 2;
			m_manager.drawString(b, x0, 184, 12, Align::MiddleLeft);
			m_manager.drawString("3", x0 + w1 + 1, 179, 8, Align::MiddleLeft);
		}
		else
			m_manager.drawString("not fetched yet", 120, 184, 12, Align::MiddleCenter);
	}
}
