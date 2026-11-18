#include "clock_widget.h"
#include "assets.h"

/* the original's defaults (ClockWidget.h) */
#define CLOCK_FONT DSEG7
#define CLOCK_FONT_SIZE 200
#define CLOCK_OFFSET_X_DIGITS -10
#define CLOCK_OFFSET_X_COLON 0

void ClockWidget::setup()
{
	m_lastDisplay1Digit = "";
	m_lastDisplay2Digit = "";
	m_lastDisplay4Digit = "";
	m_lastDisplay5Digit = "";
}

void ClockWidget::draw(bool force)
{
	m_manager.setFont(CLOCK_FONT);
	if(m_lastDisplay1Digit != m_display1Digit || force)
	{
		displayDigit(0, m_lastDisplay1Digit, m_display1Digit, m_fgColor);
		m_lastDisplay1Digit = m_display1Digit;
	}
	if(m_lastDisplay2Digit != m_display2Digit || force)
	{
		displayDigit(1, m_lastDisplay2Digit, m_display2Digit, m_fgColor);
		m_lastDisplay2Digit = m_display2Digit;
	}
	if(m_lastDisplay4Digit != m_display4Digit || force)
	{
		displayDigit(3, m_lastDisplay4Digit, m_display4Digit, m_fgColor);
		m_lastDisplay4Digit = m_display4Digit;
	}
	if(m_lastDisplay5Digit != m_display5Digit || force)
	{
		displayDigit(4, m_lastDisplay5Digit, m_display5Digit, m_fgColor);
		m_lastDisplay5Digit = m_display5Digit;
	}
	if(m_secondSingle != m_lastSecondSingle || force)
	{
		if(m_secondSingle % 2 == 0)
			displayDigit(2, "", ":", m_fgColor, false);
		else
			displayDigit(2, "", ":", m_shadowColor, false);
		if(m_showSecondTicks)
		{
			displaySeconds(2, m_lastSecondSingle, TFT_BLACK);   /* (not a custom clock: clear the old tick) */
			displaySeconds(2, m_secondSingle, m_fgColor);
		}
		m_lastSecondSingle = m_secondSingle;
		if(m_type == (int)ClockType::NORMAL)
		{
			if(m_format == CLOCK_FORMAT_12_HOUR_AMPM)
			{
				if(m_amPm != m_lastAmPm)
				{
					displayAmPm(m_lastAmPm, TFT_BLACK);
					m_lastAmPm = m_amPm;
				}
				displayAmPm(m_amPm, m_fgColor);
			}
		}
	}
}

void ClockWidget::displayAmPm(String &amPm, uint32_t color)
{
	m_manager.selectScreen(2);
	m_manager.setFontColor(color, TFT_BLACK);
	/* the original's workaround: DSEG7 clock -> AM/PM in DSEG14 */
	m_manager.setFont(DSEG14);
	m_manager.drawString(amPm, SCREEN_SIZE / 5 * 4, SCREEN_SIZE / 2, 25, Align::MiddleCenter);
}

void ClockWidget::update(const ClockTime &t, bool force)
{
	m_hourSingle = t.hour;
	m_minuteSingle = t.minute;
	m_secondSingle = t.second;
	m_amPm = t.pm ? "PM" : "AM";
	if(m_lastHourSingle != m_hourSingle || force)
	{
		if(m_hourSingle < 10)
		{
			if(m_format == CLOCK_FORMAT_24_HOUR)
				m_display1Digit = "0";
			else
				m_display1Digit = " ";
		}
		else
			m_display1Digit = int(m_hourSingle / 10);
		m_display2Digit = m_hourSingle % 10;
		m_lastHourSingle = m_hourSingle;
	}
	if(m_lastMinuteSingle != m_minuteSingle || force)
	{
		String currentMinutePadded = String(m_minuteSingle).length() == 1 ? "0" + String(m_minuteSingle) : String(m_minuteSingle);
		m_display4Digit = currentMinutePadded.substring(0, 1);
		m_display5Digit = currentMinutePadded.substring(1, 2);
		m_lastMinuteSingle = m_minuteSingle;
	}
}

void ClockWidget::changeFormat()
{
	m_format++;
	if(m_type == (int)ClockType::NORMAL)
	{
		if(m_format > 2)
			m_format = 0;
	}
	else if(m_format > 1)
		m_format = 0;
	m_manager.clearAllScreens();
	m_needTime = true;              /* main.cpp: update(force) with the new 12/24 h time, then draw(true) */
	m_changed = true;
}

bool ClockWidget::isValidClockType(int clockType)
{
	return clockType == (int)ClockType::NORMAL || clockType == (int)ClockType::NIXIE;
}

void ClockWidget::changeClockType()
{
	m_type++;
	if(m_type > (int)ClockType::NIXIE)
		m_type = 0;
	m_manager.clearAllScreens();
	draw(true);
	m_changed = true;
}

void ClockWidget::buttonPressed(uint8_t buttonId, ButtonState state)
{
	if(buttonId == BUTTON_OK && state == BTN_SHORT)
		changeClockType();
	else if(buttonId == BUTTON_OK && state == BTN_MEDIUM)
		changeFormat();
}

DigitOffset ClockWidget::getOffsetForDigit(const String &digit)
{
	if(digit.length() > 0)
	{
		char c = digit.charAt(0);
		if(c >= '0' && c <= '9')
			return m_digitOffsets[c - '0'];
	}
	return { 0, 0 };
}

void ClockWidget::displayDigit(int displayIndex, const String &lastDigit, const String &digit, uint32_t color, bool shadowing)
{
	if(m_type == (int)ClockType::NIXIE)
	{
		if(digit == ":" && color == (uint32_t)m_shadowColor)
			displayDigitImage(displayIndex, " ");          /* colon off */
		else
			displayDigitImage(displayIndex, digit);
	}
	else
	{
		int fontSize = CLOCK_FONT_SIZE;
		char c = digit.charAt(0);
		bool isDigit = (c >= '0' && c <= '9') || c == ' ';
		int defaultX = SCREEN_SIZE / 2 + (isDigit ? CLOCK_OFFSET_X_DIGITS : CLOCK_OFFSET_X_COLON);
		int defaultY = SCREEN_SIZE / 2;
		DigitOffset digitOffset = getOffsetForDigit(digit);
		DigitOffset lastDigitOffset = getOffsetForDigit(lastDigit);
		m_manager.selectScreen(displayIndex);
		if(shadowing)
		{
			m_manager.setFontColor(m_shadowColor, TFT_BLACK);
			m_manager.drawString("8", defaultX, defaultY, fontSize, Align::MiddleCenter);   /* DSEG7: 8 = every segment */
		}
		else
		{
			m_manager.setFontColor(TFT_BLACK, TFT_BLACK);
			m_manager.drawString(lastDigit, defaultX + lastDigitOffset.x, defaultY + lastDigitOffset.y, fontSize, Align::MiddleCenter);
		}
		m_manager.setFontColor(color, TFT_BLACK);
		m_manager.drawString(digit, defaultX + digitOffset.x, defaultY + digitOffset.y, fontSize, Align::MiddleCenter);
	}
}

void ClockWidget::displayDigit(int displayIndex, const String &lastDigit, const String &digit, uint32_t color)
{
	displayDigit(displayIndex, lastDigit, digit, color, m_shadowing);
}

void ClockWidget::displaySeconds(int displayIndex, int seconds, int color)
{
	if(color != m_fgColor && m_type != (int)ClockType::NORMAL)
		return;                                    /* (the whole picture is drawn anyway) */
	if(m_type == (int)ClockType::NIXIE)
		color = m_overrideNixieColor != TFT_BLACK ? m_overrideNixieColor : 0xfd40;   /* the tubes' orange */
	m_manager.selectScreen(displayIndex);
	int startA = ((seconds * 6) + 180 - 3) % 360;
	int endA = ((seconds * 6) + 180 + 3) % 360;
	m_manager.drawSmoothArc(SCREEN_SIZE / 2, SCREEN_SIZE / 2, 120, 110, startA, endA, color, TFT_BLACK);
}

void ClockWidget::displayDigitImage(int displayIndex, const String &digit)
{
	if(digit.length() != 1)
		return;
	char c = digit.charAt(0);
	uint8_t index;
	if(c == ' ')
		index = 10;
	else if(c == ':')
		index = 11;
	else
		index = (uint8_t)(c - '0');
	if(index >= 12)
		return;
	displayNixie(displayIndex, index);
}

void ClockWidget::displayNixie(int displayIndex, uint8_t index)
{
	m_manager.selectScreen(displayIndex);
	m_manager.drawJpg(0, 0, nixie_jpg[index], (uint32_t)nixie_jpg_len[index], 1, (uint32_t)m_overrideNixieColor);
}
