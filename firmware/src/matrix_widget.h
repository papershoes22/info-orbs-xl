/* The Matrix set (2026-11-16): the orbs' original MatrixWidget (src/widgets/matrixwidget/MatrixWidget.cpp) with its
   DigitalRainAnimation, the same rain on all five orbs at once. The defaults (the settings an ESP32
   unit had): text #31a110, head #6ac67b, lines 3-15 long, speed 3-15, every 100 ms, small font. (They move into
   the XL's settings with the settings page.) A fresh animation each time the set comes up: the original's
   prepareAnim() adds to its lists every time it runs. */
#pragma once
#include "screen.h"
#include "DigitalRainAnimation.hpp"

class MatrixWidget {
public:
	explicit MatrixWidget(ScreenManager &manager) : m_manager(manager) {}
	~MatrixWidget() { delete m_effect; }
	void setup()
	{
		delete m_effect;
		m_effect = new DigitalRainAnimation();
		m_manager.selectAllScreens();
		if(m_bigFont)
			m_effect->init(&m_manager, true, false);
		else
			m_effect->init(&m_manager);
		m_effect->setup(m_lineMin, m_lineMax, m_speedMin, m_speedMax, m_updateInterval);
		int R = ((m_textColor >> 11) & 0x1F) * 255 / 31, G = ((m_textColor >> 5) & 0x3F) * 255 / 63, B = (m_textColor & 0x1F) * 255 / 31;
		m_effect->setTextColor((uint8_t)R, (uint8_t)G, (uint8_t)B);
		R = ((m_headTextColor >> 11) & 0x1F) * 255 / 31;
		G = ((m_headTextColor >> 5) & 0x3F) * 255 / 63;
		B = (m_headTextColor & 0x1F) * 255 / 31;
		m_effect->setHeadCharColor((uint8_t)R, (uint8_t)G, (uint8_t)B);
	}
	void draw()
	{
		if(!m_effect)
			return;
		m_manager.selectAllScreens();
		m_effect->loop();
	}
	void stop()
	{
		delete m_effect;
		m_effect = nullptr;
	}
	bool m_bigFont = false;
	int m_textColor = 0x3502;        /* #31a110 */
	int m_headTextColor = 0x6E2F;    /* #6ac67b */
	int m_lineMin = 3, m_lineMax = 15, m_speedMin = 3, m_speedMax = 15, m_updateInterval = 100;

private:
	ScreenManager &m_manager;
	DigitalRainAnimation *m_effect = nullptr;
};
