#include "rig.h"
#include <cmath>

const char *const rig_set_name[N_RIG_SETS] = { "rig", "music", "trove", "cams", "viz", "printer", "claude", "ai", "events",
                                              "house", "network", "backups", "projects", "movers", "gaming", "radar" };
RigLink rig;

/* the old firmware's RigSetWidget::drawStatusFace, unchanged but for TFT_eSPI's drawArc through our stand-in */
void drawStatusFace(ScreenManager &m, const char *title, const String &big, const String &small)
{
	m.setFont(ROBOTO_REGULAR);
	for(int i = 0; i < 5; i++)
	{
		m.selectScreen(i);
		m.fillScreen(TFT_BLACK);
		m.getDisplay().drawArc(120, 120, 119, 116, 0, 360, i == 2 ? 0x2A9F : 0x18C3, TFT_BLACK, true);
		if(i == 2)
		{
			m.drawString(title, 120, 78, 16, Align::MiddleCenter, 0x5D7F, TFT_BLACK);
			m.drawString(big, 120, 118, 28, Align::MiddleCenter, TFT_WHITE, TFT_BLACK);
			m.drawString(small, 120, 156, 18, Align::MiddleCenter, 0xA534, TFT_BLACK);
		}
		else
			m.drawString(title, 120, 120, 16, Align::MiddleCenter, 0x4228, TFT_BLACK);
	}
	m.reset();
}

void RigPictureSet::enter(int set, const char *title)
{
	m_set = set;
	m_title = title;
	m_enterMs = millis();
	m_submittedAtEnter = rig.submitted;
	m_waitShown = false;
	rig.need = 0x1F;
	rig.active = set;
}

void RigPictureSet::draw(uint32_t now)
{
	/* the rig is there: its pictures usually land within a second, so "Connecting" only if they don't */
	if(!m_waitShown && rig.submitted == m_submittedAtEnter && now - m_enterMs > 1500)
	{
		drawStatusFace(m_manager, m_title, "Connecting", "to the rig...");
		m_waitShown = true;
	}
}

void RigPictureSet::beforeSubmit()
{
	if(m_waitShown)
	{
		m_manager.clearAllScreens();
		m_manager.flush();
		m_waitShown = false;
	}
}

/* ---- the visualizer: the old firmware's VisualizerWidget ---- */
static constexpr int BAR_W = 11, BAR_PITCH = 15, SEG_H = 6, SEG_PITCH = 8;
static constexpr int X0 = (240 - (VisualizerWidget::PER_ORB * BAR_PITCH - (BAR_PITCH - BAR_W))) / 2;   /* 32 */
static constexpr int Y_BOTTOM = 190;
static constexpr uint32_t PEAK_HOLD_MS = 350, PEAK_FALL_MS = 45, NO_SOUND_MS = 1000;
static const char *ORB_LABEL[5] = { "BASS", "LOW MIDS", "MIDS", "HIGH MIDS", "TREBLE" };

static uint16_t hsv565(float h, float s, float v)
{
	h = fmodf(h, 360.0f);
	if(h < 0)
		h += 360.0f;
	float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c;
	float r, g, b;
	if(h < 60) { r = c; g = x; b = 0; }
	else if(h < 120) { r = x; g = c; b = 0; }
	else if(h < 180) { r = 0; g = c; b = x; }
	else if(h < 240) { r = 0; g = x; b = c; }
	else if(h < 300) { r = x; g = 0; b = c; }
	else { r = c; g = 0; b = x; }
	uint8_t R = (uint8_t)((r + m) * 255), G = (uint8_t)((g + m) * 255), B = (uint8_t)((b + m) * 255);
	return (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
}

void VisualizerWidget::buildColors(int hueTenths)
{
	m_hueTenths = hueTenths;
	for(int b = 0; b < BANDS; b++)
	{
		float hue = hueTenths < 0 ? 280.0f * b / (BANDS - 1) : hueTenths / 10.0f - 30.0f + 60.0f * b / (BANDS - 1);
		for(int s = 0; s < SEGS; s++)
		{
			float t = (float)s / (SEGS - 1);
			m_lit[b][s] = hsv565(hue, 0.95f - 0.35f * t, 0.72f + 0.28f * t);
			m_dim[b][s] = hsv565(hue, 0.75f, 0.10f);
		}
		m_cap[b] = hsv565(hue, 0.15f, 1.0f);
	}
}

void VisualizerWidget::setup()
{
	memset(m_level, 0, sizeof m_level);
	m_barsShown = false;
}

void VisualizerWidget::levels(const uint8_t *lv, int hue)
{
	for(int b = 0; b < BANDS; b++)
		m_level[b] = (uint8_t)((lv[b] * SEGS + 127) / 255);
	int diff = hue < 0 || m_hueTenths < 0 ? (hue == m_hueTenths ? 0 : 9999) : abs(hue - m_hueTenths);
	if(diff > 1800)
		diff = hue < 0 || m_hueTenths < 0 ? diff : 3600 - diff;
	if(diff >= 20)
	{
		buildColors(hue);
		m_repaintOrb = 0;
	}
	m_lastPacket = millis();
	m_fresh = true;
}

void VisualizerWidget::drawSeg(int band, int seg, uint16_t color)
{
	int x = X0 + (band % PER_ORB) * BAR_PITCH;
	int y = Y_BOTTOM - (seg + 1) * SEG_H - seg * (SEG_PITCH - SEG_H);
	m_manager.fillRect(x, y, BAR_W, SEG_H, color);
}

void VisualizerWidget::drawAll()
{
	m_manager.setFont(ROBOTO_REGULAR);
	for(int o = 0; o < 5; o++)
	{
		m_manager.selectScreen(o);
		m_manager.fillScreen(TFT_BLACK);
		for(int k = 0; k < PER_ORB; k++)
		{
			int b = o * PER_ORB + k;
			for(int s = 0; s < SEGS; s++)
				drawSeg(b, s, s < m_level[b] ? m_lit[b][s] : m_dim[b][s]);
			m_shownLv[b] = m_level[b];
			m_peak[b] = m_shownPeak[b] = m_level[b];
		}
		m_manager.drawString(ORB_LABEL[o], 120, 210, 13, Align::MiddleCenter, 0x6B6D, TFT_BLACK);
	}
	m_manager.reset();
}

void VisualizerWidget::draw(uint32_t now, bool force)
{
	if(m_lastPacket && now - m_lastPacket > NO_SOUND_MS && !m_fresh)
	{
		/* the rig stopped sending: let the bars fall */
		bool any = false;
		for(int b = 0; b < BANDS; b++)
		{
			any |= m_level[b] != 0;
			m_level[b] = 0;
		}
		m_fresh = any;
	}
	if(force || !m_barsShown)
	{
		drawAll();
		m_barsShown = true;
		m_fresh = false;
		frames++;
		return;
	}
	bool decayDue = now - m_lastDecay >= PEAK_FALL_MS;
	if(!m_fresh && !decayDue && m_repaintOrb < 0)
		return;
	if(decayDue)
		m_lastDecay = now;
	m_fresh = false;
	m_manager.setImmediate(true);              /* (each changed segment straight to its screen) */
	for(int o = 0; o < 5; o++)
	{
		m_manager.selectScreen(o);
		for(int k = 0; k < PER_ORB; k++)
		{
			int b = o * PER_ORB + k;
			uint8_t lv = m_level[b], was = m_shownLv[b];
			if(lv >= m_peak[b])
			{
				m_peak[b] = lv;
				m_peakAt[b] = now;
			}
			else if(decayDue && now - m_peakAt[b] > PEAK_HOLD_MS && m_peak[b] > 0)
				m_peak[b]--;
			if(o == m_repaintOrb)
			{
				for(int s = 0; s < SEGS; s++)
					drawSeg(b, s, s < lv ? m_lit[b][s] : (m_peak[b] > lv && s == m_peak[b] - 1) ? m_cap[b] : m_dim[b][s]);
				m_shownLv[b] = lv;
				m_shownPeak[b] = m_peak[b];
				continue;
			}
			uint8_t oldCap = m_shownPeak[b];
			if(oldCap > 0 && oldCap > lv && oldCap != m_peak[b])
				drawSeg(b, oldCap - 1, oldCap - 1 < lv ? m_lit[b][oldCap - 1] : m_dim[b][oldCap - 1]);
			if(lv > was)
				for(int s = was; s < lv; s++) drawSeg(b, s, m_lit[b][s]);
			else if(lv < was)
				for(int s = lv; s < was; s++) drawSeg(b, s, m_dim[b][s]);
			m_shownLv[b] = lv;
			if(m_peak[b] > lv && m_peak[b] > 0)
				drawSeg(b, m_peak[b] - 1, m_cap[b]);
			m_shownPeak[b] = m_peak[b];
		}
	}
	if(m_repaintOrb >= 0 && ++m_repaintOrb >= 5)
		m_repaintOrb = -1;
	m_manager.setImmediate(false);
	m_manager.dropDirty();
	m_manager.reset();
	frames++;
}
