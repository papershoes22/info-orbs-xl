/* The Weather set (2026-11-16): the orbs' original WeatherWidget (firmware/src/widgets/weatherwidget/WeatherWidget.cpp),
   its drawing line for line, fed from OpenWeatherMap One Call 3.0 as its OpenWeatherMapFeed read it (with one fix: the
   next three days' icons, which the original looked up in the wrong place and so always drew as sun). The same answer
   also carries what the Hourly and Sky sets need (kept here for them). */
#pragma once
#include "compat.h"
#include "screen.h"
#include "clock_widget.h"

#define WX_NAN -1024.0f

struct WeatherData {
	bool valid = false;            /* at least one good answer since start-up */
	bool changed = false;
	uint32_t version = 0;           /* +1 on every good answer (Hourly, Sky) */
	String city, currentText, currentIcon;
	float currentTemp = 0, todayHigh = 0, todayLow = 0;
	String dayIcon[3];
	float dayHigh[3] = { WX_NAN, WX_NAN, WX_NAN }, dayLow[3] = { WX_NAN, WX_NAN, WX_NAN };
	/* for the Hourly and Sky sets */
	int64_t hourDt[6] = {};
	float hourTemp[6] = {}, hourPop[6] = {};
	String hourIcon[6];
	int64_t sunrise = 0, sunset = 0;
	float moonPhase = 0, uvi = 0;
	int32_t tzOffset = 0;
};

/* reads One Call 3.0's JSON into d; false (d untouched) if it doesn't parse */
bool weather_parse(const char *json, size_t len, const char *city, WeatherData &d);
/* OpenWeatherMap's air pollution answer: AQI 1-5 and PM2.5 */
bool air_parse(const char *json, size_t len, int &aqi, float &pm25);

/* what the original's GlobalTime gave the weather set */
struct WeatherTime {
	int hour;                      /* in the clock's 12/24 h format */
	int minute;
	int mday, mon, wday;           /* mon 1-12, wday 0 = Sunday */
};

class WeatherWidget {
public:
	WeatherWidget(ScreenManager &manager, WeatherData &model) : m_manager(manager), model(model) {}
	void setup(bool dark, int switchIntervalS);
	void draw(const WeatherTime &t, bool force = false);
	void buttonPressed(uint8_t buttonId, ButtonState state);
	bool m_wantUpdate = false;     /* middle button 0.5-2 s: fetch now (main.cpp) */

private:
	enum { MODE_HIGHS, MODE_LOWS };
	ScreenManager &m_manager;
	WeatherData &model;
	WeatherTime m_time{};
	int m_mode = MODE_HIGHS;
	int m_screenMode = 1;          /* Dark */
	int m_switchinterval = 20;
	uint32_t m_prevMillisSwitch = 0;
	int m_clockStamp = -1;
	uint32_t m_foregroundColor = TFT_WHITE, m_backgroundColor = TFT_BLACK;
	uint32_t m_invertedForegroundColor = TFT_WHITE, m_invertedBackgroundColor = TFT_BLACK;
	static const int centre = 120;
	void changeMode();
	void configureColors();
	void displayClock(int displayIndex);
	void drawWeatherIcon(int displayIndex, const String &condition, int x, int y, int scale);
	void singleWeatherDeg(int displayIndex);
	void weatherText(int displayIndex);
	void threeDayWeather(int displayIndex);
};
