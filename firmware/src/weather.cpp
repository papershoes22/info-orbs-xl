#include "weather.h"
#include "assets.h"
#include <unordered_map>
#include <string>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include "ArduinoJson.h"     /* (third_party/ArduinoJson on the include path) */

static const char *const t_weekdays[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const t_months[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                          "September", "October", "November", "December" };

/* the original's Utils::formatFloat (dtostrf: printf rounding) + "°" as WeatherDataModel adds it */
static String deg(float v, int digits)
{
	char tmp[30];
	snprintf(tmp, sizeof tmp, "%.*f", digits, (double)v);
	return String(tmp) + "°";
}

/* OpenWeatherMapFeed::translateIcon, unchanged */
static String translateIcon(const std::string &icon)
{
	static const std::unordered_map<std::string, std::string> iconMapping = {
		{ "01d", "clear-day" }, { "01n", "clear-night" }, { "02d", "partly-cloudy-day" }, { "02n", "partly-cloudy-night" },
		{ "03d", "partly-cloudy-day" }, { "03n", "partly-cloudy-night" }, { "04d", "partly-cloudy-day" },
		{ "04n", "partly-cloudy-night" }, { "09d", "rain" }, { "09n", "rain" }, { "10d", "rain" }, { "10n", "rain" },
		{ "11d", "rain" }, { "11n", "rain" }, { "13d", "snow" }, { "13n", "snow" }, { "50d", "fog" }, { "50n", "fog" }
	};
	auto it = iconMapping.find(icon);
	if(it != iconMapping.end())
		return String(it->second.c_str());
	return "clear-day";
}

bool weather_parse(const char *json, size_t len, const char *city, WeatherData &d)
{
	/* only what the sets use (the original's filter, plus hourly and the sky fields) */
	JsonDocument filter;
	filter["timezone_offset"] = true;
	filter["current"]["dt"] = true;
	filter["current"]["temp"] = true;
	filter["current"]["sunrise"] = true;
	filter["current"]["sunset"] = true;
	filter["current"]["uvi"] = true;
	filter["current"]["weather"][0]["icon"] = true;
	filter["daily"][0]["summary"] = true;
	filter["daily"][0]["temp"]["min"] = true;
	filter["daily"][0]["temp"]["max"] = true;
	filter["daily"][0]["weather"][0]["icon"] = true;
	filter["daily"][0]["moon_phase"] = true;
	filter["daily"][0]["uvi"] = true;
	filter["hourly"][0]["dt"] = true;
	filter["hourly"][0]["temp"] = true;
	filter["hourly"][0]["pop"] = true;
	filter["hourly"][0]["weather"][0]["icon"] = true;
	JsonDocument doc;
	DeserializationError error = deserializeJson(doc, json, len, DeserializationOption::Filter(filter));
	if(error || !doc["current"]["temp"].is<float>() || !doc["daily"][3]["temp"]["max"].is<float>())
		return false;
	WeatherData n = d;
	n.city = city;
	n.currentTemp = doc["current"]["temp"].as<float>();
	n.currentText = doc["daily"][0]["summary"] | "";
	n.currentIcon = translateIcon(doc["current"]["weather"][0]["icon"] | "");
	n.todayHigh = doc["daily"][0]["temp"]["max"].as<float>();
	n.todayLow = doc["daily"][0]["temp"]["min"].as<float>();
	for(int i = 1; i < 4; i++)
	{
		/* (the original read daily[i]["weather"]["icon"]: weather is a list, so it always got nothing -> sun) */
		n.dayIcon[i - 1] = translateIcon(doc["daily"][i]["weather"][0]["icon"] | "");
		n.dayHigh[i - 1] = doc["daily"][i]["temp"]["max"].as<float>();
		n.dayLow[i - 1] = doc["daily"][i]["temp"]["min"].as<float>();
	}
	for(int i = 0; i < 6; i++)
	{
		n.hourDt[i] = doc["hourly"][i]["dt"] | (int64_t)0;
		n.hourTemp[i] = doc["hourly"][i]["temp"] | 0.0f;
		n.hourPop[i] = doc["hourly"][i]["pop"] | 0.0f;
		n.hourIcon[i] = doc["hourly"][i]["weather"][0]["icon"] | "";
	}
	n.sunrise = doc["current"]["sunrise"] | (int64_t)0;
	n.sunset = doc["current"]["sunset"] | (int64_t)0;
	n.uvi = doc["daily"][0]["uvi"] | 0.0f;
	n.moonPhase = doc["daily"][0]["moon_phase"] | 0.0f;
	n.tzOffset = doc["timezone_offset"] | 0;
	/* the original's model only marks a change when a value differs */
	bool changed = !d.valid || n.currentTemp != d.currentTemp || n.currentText != d.currentText ||
	               n.currentIcon != d.currentIcon || n.todayHigh != d.todayHigh || n.todayLow != d.todayLow || n.city != d.city;
	for(int i = 0; i < 3; i++)
		changed = changed || n.dayIcon[i] != d.dayIcon[i] || n.dayHigh[i] != d.dayHigh[i] || n.dayLow[i] != d.dayLow[i];
	n.valid = true;
	n.changed = d.changed || changed;
	n.version = d.version + 1;
	d = n;
	return true;
}

/* ---- WeatherWidget, the original's drawing ---- */

void WeatherWidget::setup(bool dark, int switchIntervalS)
{
	m_screenMode = dark ? 1 : 0;
	m_switchinterval = switchIntervalS;
	configureColors();
	m_prevMillisSwitch = millis();
	m_clockStamp = -1;
}

void WeatherWidget::changeMode()
{
	m_prevMillisSwitch = millis();
	m_mode = (m_mode == MODE_HIGHS) ? MODE_LOWS : MODE_HIGHS;
	threeDayWeather(4);
}

void WeatherWidget::buttonPressed(uint8_t buttonId, ButtonState state)
{
	if(buttonId == BUTTON_OK && state == BTN_SHORT)
		changeMode();
	if(buttonId == BUTTON_OK && state == BTN_MEDIUM)
		m_wantUpdate = true;
}

void WeatherWidget::draw(const WeatherTime &t, bool force)
{
	m_time = t;
	m_manager.setFont(DEFAULT_FONT);
	int clockStamp = m_time.hour * 60 + m_time.minute;
	if(clockStamp != m_clockStamp || force)
	{
		displayClock(0);
		m_clockStamp = clockStamp;
	}
	if(force || model.changed)
	{
		weatherText(1);
		drawWeatherIcon(2, model.currentIcon, 0, 0, 1);
		singleWeatherDeg(3);
		threeDayWeather(4);
		model.changed = false;
	}
	if((millis() - m_prevMillisSwitch >= (uint32_t)(m_switchinterval * 1000)) && m_switchinterval > 0)
	{
		changeMode();
		m_prevMillisSwitch = millis();
	}
}

static String padded(int v)
{
	return v < 10 ? "0" + String(v) : String(v);
}

void WeatherWidget::displayClock(int displayIndex)
{
	const int clockY = 120;
	const int dayOfWeekY = 190;
	const int dateY = 50;
	m_manager.selectScreen(displayIndex);
	m_manager.fillScreen(m_backgroundColor);
	m_manager.setFontColor(m_foregroundColor);
	/* GlobalTime::getDayAndMonth (not metric): "November 16" */
	m_manager.drawCentreString(String(t_months[(m_time.mon + 11) % 12]) + " " + String(m_time.mday), centre, dateY, 18);
	m_manager.drawCentreString(t_weekdays[m_time.wday % 7], centre, dayOfWeekY, 22);
	m_manager.drawString(padded(m_time.hour), centre - 10, clockY, 66, Align::MiddleRight);
	m_manager.drawString(":", centre, clockY, 66, Align::MiddleCenter);
	m_manager.drawString(padded(m_time.minute), centre + 10, clockY, 66, Align::MiddleLeft);
}

void WeatherWidget::drawWeatherIcon(int displayIndex, const String &condition, int x, int y, int scale)
{
	const uint8_t *icon = nullptr;
	size_t size = 0;
	bool light = m_screenMode == 0;
#define ICON(n) do { icon = light ? wx_##n##W : wx_##n##B; size = light ? sizeof wx_##n##W : sizeof wx_##n##B; } while(0)
	if(condition == "partly-cloudy-night")
		ICON(moonCloud);
	else if(condition == "partly-cloudy-day")
		ICON(sunClouds);
	else if(condition == "clear-day")
		ICON(sun);
	else if(condition == "clear-night")
		ICON(moon);
	else if(condition == "snow")
		ICON(snow);
	else if(condition == "rain")
		ICON(rain);
	else if(condition == "fog" || condition == "wind" || condition == "cloudy")
		ICON(clouds);
#undef ICON
	if(icon && size > 0)
	{
		m_manager.selectScreen(displayIndex);    /* (showJPG) */
		m_manager.drawJpg(x, y, icon, (uint32_t)size, (uint8_t)scale);
	}
}

void WeatherWidget::singleWeatherDeg(int displayIndex)
{
	m_manager.selectScreen(displayIndex);
	m_manager.fillScreen(m_backgroundColor);
	m_manager.drawCentreString(deg(model.currentTemp, 0), centre, 90, 88);
	if(m_screenMode == 0)
	{
		m_manager.fillRect(0, 150, 240, 90, m_foregroundColor);
		m_manager.fillRect(centre - 1, 150, 2, 90, m_backgroundColor);
	}
	int fontSize = 22;
	m_manager.setFontColor(m_invertedForegroundColor);
	m_manager.setBackgroundColor(m_invertedBackgroundColor);
	m_manager.drawCentreString("High", 80, 170, fontSize);
	m_manager.drawCentreString("Low", 160, 170, fontSize);
	m_manager.drawCentreString(deg(model.todayHigh, 0), 80, 210, fontSize);
	m_manager.drawCentreString(deg(model.todayLow, 0), 160, 210, fontSize);
	m_manager.setFontColor(m_foregroundColor);
	m_manager.setBackgroundColor(m_backgroundColor);
}

void WeatherWidget::weatherText(int displayIndex)
{
	m_manager.selectScreen(displayIndex);
	String message = model.currentText + " ";
	String messageArr[4];
	int variableRangeS = 0;
	int variableRangeE = 24;
	for(int i = 0; i < 4; i++)
	{
		while(message.substring(variableRangeE - 1, variableRangeE) != " ")
		{
			variableRangeE--;
			if(variableRangeE <= variableRangeS)
			{
				/* (a word over 24 letters: the original looped here for ever; cut it instead) */
				variableRangeE = variableRangeS + 24;
				break;
			}
		}
		messageArr[i] = message.substring(variableRangeS, variableRangeE);
		variableRangeS = variableRangeE;
		variableRangeE = variableRangeS + 24;
	}
	m_manager.fillScreen(m_backgroundColor);
	String cityName = model.city;
	cityName.remove(cityName.indexOf(",", 0));
	m_manager.setFontColor(m_foregroundColor);
	m_manager.drawFittedString(cityName, centre, 70, 195, 50, Align::MiddleCenter);
	int y = 118;
	for(int i = 0; i < 4; i++)
	{
		m_manager.drawCentreString(messageArr[i], centre, y, 15);
		y += 25;
	}
}

void WeatherWidget::threeDayWeather(int displayIndex)
{
	const int days = 3;
	const int columnSize = 75;
	const int highLowY = 210;
	m_manager.selectScreen(displayIndex);
	m_manager.fillScreen(m_backgroundColor);
	int fontSize = 22;
	if(m_screenMode == 0)
		m_manager.fillRect(0, 180, 240, 70, m_foregroundColor);
	m_manager.drawString(m_mode == MODE_HIGHS ? "Highs" : "Lows", centre, highLowY, fontSize, Align::MiddleCenter,
	                     m_invertedForegroundColor, m_invertedBackgroundColor);
	m_manager.setFontColor(m_foregroundColor);
	m_manager.setBackgroundColor(m_backgroundColor);
	int temperatureFontSize = fontSize;
	String temps[days];
	for(int i = 0; i < days; i++)
	{
		temps[i] = m_mode == MODE_HIGHS ? deg(model.dayHigh[i], 0) : deg(model.dayLow[i], 0);
		if(temps[i].length() > 4)
			temperatureFontSize = fontSize - 4;
	}
	m_manager.setFontColor(m_foregroundColor);
	for(int i = 0; i < days; i++)
	{
		const int x = (centre - columnSize) + i * columnSize;
		drawWeatherIcon(displayIndex, model.dayIcon[i], x - 30, 40, 4);
		m_manager.drawCentreString(temps[i], x, 122, temperatureFontSize);
		String shortDayName = t_weekdays[(m_time.wday + i + 1) % 7];
		shortDayName.remove(3);
		m_manager.drawString(shortDayName, x, 154, fontSize, Align::MiddleCenter);
	}
}

void WeatherWidget::configureColors()
{
	m_foregroundColor = m_screenMode == 0 ? TFT_BLACK : TFT_WHITE;
	m_backgroundColor = m_screenMode == 0 ? TFT_WHITE : TFT_BLACK;
	m_invertedForegroundColor = m_screenMode == 0 ? m_backgroundColor : m_foregroundColor;
	m_invertedBackgroundColor = m_screenMode == 0 ? m_foregroundColor : m_backgroundColor;
	m_manager.setBackgroundColor(m_backgroundColor);
}

bool air_parse(const char *json, size_t len, int &aqi, float &pm25)
{
	JsonDocument doc;
	if(deserializeJson(doc, json, len) || !doc["list"][0]["main"]["aqi"].is<int>())
		return false;
	aqi = doc["list"][0]["main"]["aqi"].as<int>();
	pm25 = doc["list"][0]["components"]["pm2_5"] | 0.0f;
	return true;
}
