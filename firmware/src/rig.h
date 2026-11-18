/* The rig's sets on the XL (build step 3, 2026-11-16): Rig stats, Music, Trove and Cameras are pictures the rig draws
   (a PC program, not in this repository) and sends over the USB cable; the Visualizer is drawn here from the 60 sound levels
   the rig sends 40 times a second (the old firmware's VisualizerWidget, line for line, each changed bar segment sent
   straight to its screen). The rig asks "Q" every second: that's how the XL knows it's there. With no word from the
   rig for 3 s its sets leave the rotation (and a rig set on screen gives way to the Clock).

   USB from the rig:
     "Q"                                  -> "STATUS set=<rig|music|trove|cams|viz|->" need=<mask> drawn=<n> bad=<n>
                                             up=<s> heap=<n>"   (need: the orbs that still want a picture)
     "P" u8 orb, u8 set, u32 len, JPEG    -> "PK <orb>" decoded and sent to its screen, "PN <orb>" not showing that set,
                                             "PF <orb>" didn't decode, "PB" a bad header or half a picture (step 4: the
                                             rig may send up to 3 before the first answer; pics.h)
     "V" u8 seq, u16 hue, 60 levels       -> (no answer) the visualizer's bars; hue in tenths of a degree, 0xFFFF rainbow */
#pragma once
#include "compat.h"
#include "screen.h"

enum { RIG_SET_RIG, RIG_SET_MUSIC, RIG_SET_TROVE, RIG_SET_CAMS, RIG_SET_VIZ, RIG_SET_PRINTER, RIG_SET_CLAUDE, RIG_SET_AI,
       RIG_SET_EVENTS, RIG_SET_HOUSE, RIG_SET_NETWORK, RIG_SET_BACKUPS, RIG_SET_PROJECTS, RIG_SET_MOVERS, RIG_SET_GAMING,
       RIG_SET_RADAR, N_RIG_SETS };   /* (the new sets from PRINTER on, 2026-11-17) */
extern const char *const rig_set_name[N_RIG_SETS];
#define RIG_ALIVE_MS 3000

struct RigLink {
	uint32_t contact_ms = 0;      /* last word from the rig (0 = never) */
	uint8_t need = 0x1F;
	uint32_t drawn = 0, bad = 0, viz_packets = 0, submitted = 0;
	int active = -1;              /* the rig set on screen, -1 = none */
	uint8_t level[60] = {};
	int hue = -1;                 /* tenths of a degree, -1 rainbow */
	bool levels_fresh = false;
	uint32_t last_viz_ms = 0;
	bool alive(uint32_t now) const { return contact_ms && now - contact_ms < RIG_ALIVE_MS; }
};
extern RigLink rig;

/* a rig picture set on screen: "Connecting to the rig..." until its first picture (the old firmware's face) */
class RigPictureSet {
public:
	RigPictureSet(ScreenManager &m) : m_manager(m) {}
	void enter(int set, const char *title);
	void draw(uint32_t now);           /* the face only while nothing has been sent on (so the second core is idle) */
	void beforeSubmit();               /* the first picture after that face: the other orbs go dark first */
	int set() const { return m_set; }
private:
	ScreenManager &m_manager;
	const char *m_title = "";
	int m_set = -1;
	uint32_t m_enterMs = 0, m_submittedAtEnter = 0;
	bool m_waitShown = false;
};

class VisualizerWidget {
public:
	static constexpr int BANDS = 60, PER_ORB = 12, SEGS = 18;
	VisualizerWidget(ScreenManager &m) : m_manager(m) { buildColors(-1); }
	void setup();
	void levels(const uint8_t *lv, int hueTenths);    /* a packet's 60 levels (0-255) */
	void draw(uint32_t now, bool force);
	uint32_t frames = 0;
private:
	ScreenManager &m_manager;
	uint16_t m_lit[BANDS][SEGS], m_dim[BANDS][SEGS], m_cap[BANDS];
	uint8_t m_level[BANDS] = {}, m_shownLv[BANDS] = {}, m_peak[BANDS] = {}, m_shownPeak[BANDS] = {};
	uint32_t m_peakAt[BANDS] = {};
	int m_hueTenths = -2;
	uint32_t m_lastPacket = 0, m_lastDecay = 0;
	bool m_fresh = false, m_barsShown = false;
	int m_repaintOrb = -1;
	void buildColors(int hueTenths);
	void drawSeg(int band, int seg, uint16_t color);
	void drawAll();
};

void drawStatusFace(ScreenManager &m, const char *title, const String &big, const String &small);
