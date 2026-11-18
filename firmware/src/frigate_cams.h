/* Cameras straight from a Frigate camera recorder when the rig isn't there (step 4, 2026-11-17). The XL logs in to
   Frigate (camlogin.h: best a read-only viewer account) over HTTPS, trusting only Frigate's own certificate (pinned), and fetches each camera's
   newest picture 240 high (/api/<camera>/latest.jpg?height=240: a frame Frigate has already decoded, cut to fill the orb),
   one orb after another, down one kept-open connection, the next one coming in while the last is drawn; the middle
   button pages to the other cameras (6 and 7). Drawn here with the camera's name and the time. While the rig is there it checks once an
   hour that this still works, so a broken backup shows up before it's needed ("fg=" in "Q"). */
#pragma once
#include "compat.h"
#include "screen.h"

class FrigateCams {
public:
	explicit FrigateCams(ScreenManager &m) : m_manager(m) {}
	void enter();                              /* the Cameras set, from Frigate */
	/* the main loop, every pass: may_fetch = the HTTPS client is free (no weather fetch); showing = on screen now */
	void poll(uint32_t now, bool may_fetch, bool showing);
	bool busy() const { return m_busy; }
	/* for "?" / "Q" */
	const char *state() const;                 /* "unset", "ok", "fail" */
	const char *error() const { return m_err; }
	uint32_t pictures() const { return m_pictures; }
	uint32_t fails() const { return m_fails; }
	uint32_t lastOkAgeS(uint32_t now) const { return m_okAt ? (now - m_okAt) / 1000 : 0xFFFFFFFFu; }
	uint32_t lastMs() const { return m_lastMs; }
	int lastLit() const { return m_lastLit; }     /* % of the last drawn orb that isn't black */
	int lastDecode() const { return m_lastDecode; }   /* TJpgDec's result for it (0 = OK) */
	void forgetToken();                        /* a new login came over USB */
	void nextPage();                           /* the middle button: the next page of cameras */

private:
	ScreenManager &m_manager;
	enum Step { IDLE, LOGGING_IN, FETCHING };
	Step m_step = IDLE;
	bool m_busy = false, m_showing = false, m_checkOnly = false, m_tried = false, m_lastOk = false;
	int m_orb = 0, m_page = 0, m_buf = 0, m_fetchOrb = -1, m_fetchCam = 0, m_loginFails = 0;
	uint32_t m_nextCheck = 60000, m_okAt = 0, m_pictures = 0, m_fails = 0, m_lastMs = 0, m_retryAt = 0, m_t0 = 0;
	char m_token[640] = "";
	uint8_t m_lastLit = 0;
	int m_lastDecode = -1;
	char m_err[96] = "not tried yet";
	void startLogin();
	void startFetch(int orb, int cam);
	void startNext(uint32_t now, bool may_fetch);
	void finished(uint32_t now, bool may_fetch);
	void drawPicture(int orb, int cam, const uint8_t *jpg, size_t len, uint32_t now);
	void drawNoPicture(int orb, int cam, const char *why);
};
