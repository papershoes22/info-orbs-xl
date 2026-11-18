/* The two new sets that replace Stocks (2026-11-16).
   Both from the OpenWeatherMap answer the Weather set already fetches (plus its air-quality answer), drawn in the
   original sets' style: Roboto, white on black, the original weather icons.
   Hourly: the next five hours, one per orb: the hour, its icon, the temperature, the chance of rain.
   Sky:    sunrise (and the day's length), sunset, the moon's phase, the UV index, the air quality. */
#pragma once
#include "compat.h"
#include "screen.h"
#include "weather.h"

struct SkyExtra {
	int aqi = 0;                   /* OpenWeatherMap's 1 (good) - 5 (very poor), 0 = none yet */
	float pm25 = 0;                /* µg/m³ */
};

class HourlyWidget {
public:
	HourlyWidget(ScreenManager &m, WeatherData &d) : m_manager(m), model(d) {}
	void draw(bool twentyFour, int32_t utcToLocal, bool force);
private:
	ScreenManager &m_manager;
	WeatherData &model;
	uint32_t m_shownVersion = 0xFFFFFFFF;
};

class SkyWidget {
public:
	SkyWidget(ScreenManager &m, WeatherData &d, SkyExtra &x) : m_manager(m), model(d), extra(x) {}
	void draw(bool twentyFour, int32_t utcToLocal, int64_t now, bool force);
private:
	ScreenManager &m_manager;
	WeatherData &model;
	SkyExtra &extra;
	int64_t m_shownMinute = -1;
	uint32_t m_shownVersion = 0xFFFFFFFF;
	int m_shownAqi = -1;
};

const char *moon_phase_name(float phase);
int moon_illumination(float phase);              /* percent */
