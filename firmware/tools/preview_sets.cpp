/* Draws the orbs' sets on the PC with the XL's own code (src/screen.cpp, tft.cpp, clock_widget.cpp, the original
   fonts through OpenFontRender), 2026-11-16. Writes one 1200x240 RGB file per scene; tools/preview_sets.sh makes PNGs. */
#include <cstdio>
#include <cstring>
extern "C" size_t __sanitizer_get_current_allocated_bytes(void);   /* (AddressSanitizer's count of heap in use) */
#include "../src/screen.h"
#include "../src/clock_widget.h"
#include "../src/weather.h"
#include "../src/sets_new.h"
#include "../src/matrix_widget.h"

static uint16_t fbs[N_ORB_FB][ORB_W * ORB_H];
static uint32_t fake_ms;
extern "C" uint32_t orbs_millis(void) { return fake_ms; }
extern "C" uint32_t orbs_random32(void) { return (uint32_t)rand() * 2654435761u ^ (uint32_t)rand(); }
extern "C" void lcd_show_rows(int, const uint16_t *, int, int) {}
extern "C" void lcd_show_rows_all(const uint16_t *, int, int) {}

static void dump(const char *dir, const char *name)
{
	char p[512];
	snprintf(p, sizeof p, "%s/%s.rgb", dir, name);
	FILE *f = fopen(p, "wb");
	for(int y = 0; y < ORB_H; y++)
		for(int o = 0; o < N_ORB_FB; o++)
			for(int x = 0; x < ORB_W; x++)
			{
				uint16_t v = fbs[o][y * ORB_W + x];
				v = (uint16_t)((v >> 8) | (v << 8));
				uint8_t rgb[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31) };
				fwrite(rgb, 1, 3, f);
			}
	fclose(f);
}

int main(int argc, char **argv)
{
	if(argc < 2) return 2;
	for(int o = 0; o < N_ORB_FB; o++) orb_fb[o] = fbs[o];
	TFT_eSPI tft;
	ScreenManager sm(tft);
	sm.begin();
	sm.clearAllScreens();
	ClockWidget clk(sm);
	clk.setup();
	ClockTime t{ 12, 34, 6, true };
	clk.update(t, true);
	clk.draw(true);
	dump(argv[1], "1_clock_1234pm_even");
	t.second = 7;                       /* the next second: colon dimmed, tick moved */
	clk.update(t);
	clk.draw();
	dump(argv[1], "2_clock_1234pm_odd");
	t = { 9, 5, 30, false };            /* a single-digit hour, AM, minute change */
	clk.update(t);
	clk.draw();
	dump(argv[1], "3_clock_905am");
	clk.m_type = (int)ClockType::NIXIE;  /* Nixie */
	sm.clearAllScreens();
	clk.update(t, true);
	clk.draw(true);
	dump(argv[1], "4_nixie_905");
	clk.m_type = (int)ClockType::NORMAL; /* 24 h */
	clk.m_format = CLOCK_FORMAT_24_HOUR;
	sm.clearAllScreens();
	t = { 17, 48, 0, true };
	clk.update(t, true);
	clk.draw(true);
	dump(argv[1], "5_clock_24h_1748");
	/* the Weather set with a made-up forecast (One Call 3.0's shape, no real data) */
	static const char sample[] = R"({"timezone_offset":-18000,"current":{"dt":1794873600,"temp":47.6,"sunrise":1794832800,
		"sunset":1794868800,"uvi":0,"weather":[{"icon":"04n"}]},"hourly":[{"dt":1794873600,"temp":47.6,"pop":0.1,
		"weather":[{"icon":"04n"}]},{"dt":1794877200,"temp":46.2,"pop":0.2,"weather":[{"icon":"10n"}]}],
		"daily":[{"summary":"Expect a day of partly cloudy with rain","temp":{"min":38.4,"max":52.1},"moon_phase":0.85,"uvi":2.1,
		"weather":[{"icon":"10d"}]},{"temp":{"min":35.0,"max":49.6},"weather":[{"icon":"01d"}]},{"temp":{"min":33.2,"max":44.9},
		"weather":[{"icon":"13d"}]},{"temp":{"min":40.7,"max":58.3},"weather":[{"icon":"03d"}]}]})";
	WeatherData wd;
	bool ok = weather_parse(sample, sizeof sample - 1, "New York", wd);
	fprintf(stderr, "weather sample parsed: %s, %s, %.1f, days %s/%s/%s\n", ok ? "yes" : "NO", wd.currentText.c_str(), wd.currentTemp,
	        wd.dayIcon[0].c_str(), wd.dayIcon[1].c_str(), wd.dayIcon[2].c_str());
	WeatherWidget wx(sm, wd);
	wx.setup(true, 20);
	sm.clearAllScreens();
	WeatherTime wt{ 7, 51, 16, 11, 0 };
	wx.draw(wt, true);
	dump(argv[1], "6_weather_dark");
	/* the new sets with made-up values: 2026-11-16, 2:30 PM in New York (UTC-5) */
	const int32_t off = -18000;
	const int64_t day0 = 1795237200;                 /* 2026-11-16 00:00 local, in UTC seconds */
	static const char *icons[6] = { "04d", "04d", "10d", "10d", "03n", "01n" };
	static const float temps[6] = { 47, 46, 44, 42, 40, 38 }, pops[6] = { 0.1f, 0.2f, 0.55f, 0.6f, 0.15f, 0 };
	for(int i = 0; i < 6; i++)
	{
		wd.hourDt[i] = day0 + (14 + i) * 3600;
		wd.hourTemp[i] = temps[i];
		wd.hourPop[i] = pops[i];
		wd.hourIcon[i] = icons[i];
	}
	wd.sunrise = day0 + 6 * 3600 + 43 * 60;          /* 6:43 AM */
	wd.sunset = day0 + 16 * 3600 + 36 * 60;          /* 4:36 PM */
	wd.moonPhase = 0.85f;
	wd.uvi = 2.1f;
	SkyExtra sx;
	sx.aqi = 2;
	sx.pm25 = 6.4f;
	HourlyWidget hw(sm, wd);
	sm.clearAllScreens();
	hw.draw(false, off, true);
	dump(argv[1], "7_hourly");
	SkyWidget skw(sm, wd, sx);
	sm.clearAllScreens();
	skw.draw(false, off, day0 + 14 * 3600 + 30 * 60, true);
	dump(argv[1], "8_sky");
	/* the Matrix: mirror mode (one picture for all five), 60 frames of 100 ms */
	sm.clearAllScreens();
	sm.setMirror(true);
	MatrixWidget mw(sm);
	mw.setup();
	for(int f = 0; f < 60; f++)
	{
		fake_ms += 100;
		mw.draw();
	}
	sm.setMirror(false);
	for(int o = 1; o < N_ORB_FB; o++)
		memcpy(fbs[o], fbs[0], sizeof fbs[0]);   /* (the screens would show the same picture) */
	dump(argv[1], "9_matrix");
	/* memory over time: 2000 seconds of the 12 h clock (fonts switch DSEG7 <-> DSEG14 every second, as on the XL) */
	clk.m_format = CLOCK_FORMAT_12_HOUR_AMPM;
	size_t before = 0;
	for(int i = 0; i < 2000; i++)
	{
		t = { 1 + (i / 60 / 60) % 12, (i / 60) % 60, i % 60, (i / 3600) % 2 == 1 };
		clk.update(t);
		clk.draw();
		if(i == 100)
			before = __sanitizer_get_current_allocated_bytes();
	}
	size_t after = __sanitizer_get_current_allocated_bytes();
	fprintf(stderr, "heap in use after 100 s: %zu bytes, after 2000 s: %zu bytes (%+ld)\n", before, after, (long)after - (long)before);
	return 0;
}
