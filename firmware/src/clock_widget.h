/* The Clock set (2026-11-16): the orbs' original ClockWidget (firmware/src/widgets/clockwidget/ClockWidget.cpp),
   its drawing line for line: DSEG7 Modern Bold digits at 200 px with the shadow "8" behind, the blinking colon, the
   second tick round the middle orb, AM/PM in DSEG14 at 25 px, and the Nixie tubes (the original's JPEGs). Custom
   clocks (pictures on the ESP32's file system) aren't carried over. */
#pragma once
#include "compat.h"
#include "screen.h"

enum { BUTTON_LEFT, BUTTON_OK, BUTTON_RIGHT };
enum ButtonState { BTN_NOTHING, BTN_SHORT, BTN_MEDIUM, BTN_LONG, BTN_VERY_LONG };
enum { CLOCK_FORMAT_24_HOUR = 0, CLOCK_FORMAT_12_HOUR = 1, CLOCK_FORMAT_12_HOUR_AMPM = 2 };
enum class ClockType { NORMAL = 0, NIXIE = 1 };

/* what the original's GlobalTime gives the clock */
struct ClockTime {
	int hour;          /* 0-23 in 24 h, else 1-12 */
	int minute, second;
	bool pm;
};

struct DigitOffset {
	int x;
	int y;
};

class ClockWidget {
public:
	explicit ClockWidget(ScreenManager &manager) : m_manager(manager) {}
	void setup();
	void update(const ClockTime &t, bool force = false);
	void draw(bool force = false);
	void buttonPressed(uint8_t buttonId, ButtonState state);
	/* settings (from settings.h) */
	int m_type = (int)ClockType::NORMAL;
	int m_format = CLOCK_FORMAT_12_HOUR_AMPM;
	bool m_showSecondTicks = true;
	int m_fgColor = 0x4FE0;
	int m_shadowColor = 0x20A1;
	bool m_shadowing = true;
	int m_overrideNixieColor = 0;
	bool m_changed = false;        /* a button changed type/format: main.cpp saves the settings */
	bool m_needTime = false;       /* changeFormat wants the time again now */

private:
	ScreenManager &m_manager;
	void changeFormat();
	void changeClockType();
	bool isValidClockType(int clockType);
	DigitOffset getOffsetForDigit(const String &digit);
	void displayDigit(int displayIndex, const String &lastDigit, const String &digit, uint32_t color, bool shadowing);
	void displayDigit(int displayIndex, const String &lastDigit, const String &digit, uint32_t color);
	void displaySeconds(int displayIndex, int seconds, int color);
	void displayAmPm(String &amPm, uint32_t color);
	void displayDigitImage(int displayIndex, const String &digit);
	void displayNixie(int displayIndex, uint8_t index);

	int m_minuteSingle = 0, m_hourSingle = 0, m_secondSingle = 0;
	int m_lastMinuteSingle{ -1 }, m_lastHourSingle{ -1 }, m_lastSecondSingle{ -1 };
	String m_display1Digit, m_display2Digit, m_display4Digit, m_display5Digit;
	String m_lastDisplay1Digit, m_lastDisplay2Digit, m_lastDisplay4Digit, m_lastDisplay5Digit;
	String m_amPm, m_lastAmPm{ "" };
	/* CLOCK_DIGITS_OFFSET: not all DSEG7 digits sit the same */
	DigitOffset m_digitOffsets[10] = { { 0, 0 }, { 1, -5 }, { 0, 0 }, { 1, 0 }, { 0, -5 }, { 0, 0 }, { 0, 0 }, { 0, -5 }, { 0, 0 }, { 0, 0 } };
	ClockTime m_last{};
};
