#include "frigate_cams.h"
#include <cstdio>
#include <cstring>
#include "pico/stdlib.h"
#include "pics.h"
extern "C" {
#include "https.h"
#include "camlogin.h"
#include "orbs_time.h"
}

/* the cameras, in the rig's order: the five of the Cameras set, then the two the middle button's second page adds.
   Page p shows camera (5p + k) % 7 on orb k, as on the rig. CAM_ID = each camera's name in Frigate (put yours here, and
   the same names in settings_json.cpp's CAM_IDS); CAM_TITLE = what the orb writes on its picture. */
#define N_CAMS 7
static const char *const CAM_ID[N_CAMS] = { "cam1", "cam2", "cam3", "cam4", "cam5", "cam6", "cam7" };
static const char *const CAM_TITLE[N_CAMS] = { "CAMERA 1", "CAMERA 2", "CAMERA 3", "CAMERA 4", "CAMERA 5", "CAMERA 6", "CAMERA 7" };
static const int N_PAGES = (N_CAMS + 4) / 5;
/* two answers in turn: the next picture comes in over Wi-Fi while the last one is drawn (a picture is ~8-25 KB) */
static char fg_buf[2][64 * 1024] __uninitialized_psram("fgbuf");
static const uint32_t CHECK_EVERY_MS = 3600 * 1000;               /* while the rig is there */

extern orbs_tz_t *frigate_tz;                                      /* (main.cpp: the clock's time zone) */
extern bool frigate_12h;

static int cam_for(int page, int orb) { return (5 * page + orb) % N_CAMS; }

const char *FrigateCams::state() const
{
	if(!camlogin_set())
		return "unset";
	if(!m_tried)
		return "untried";
	return m_lastOk ? "ok" : "fail";
}

void FrigateCams::forgetToken()
{
	memset(m_token, 0, sizeof m_token);
	m_nextCheck = 0;
}

void FrigateCams::nextPage()
{
	m_page = (m_page + 1) % N_PAGES;
	m_orb = 0;
	printf("EV FRIGATE page %d of %d\n", m_page + 1, N_PAGES);
}

void FrigateCams::enter()
{
	m_orb = 0;
	m_retryAt = 0;
	pics_wait_idle();
	m_manager.clearAllScreens();
	m_manager.setFont(ROBOTO_REGULAR);
	for(int o = 0; o < 5; o++)
	{
		m_manager.selectScreen(o);
		m_manager.getDisplay().drawArc(120, 120, 119, 116, 0, 360, 0x18C3, TFT_BLACK, true);
		m_manager.drawString(CAM_TITLE[cam_for(m_page, o)], 120, 108, 13, Align::MiddleCenter, 0x9CF3, TFT_BLACK);
		m_manager.drawString(camlogin_set() ? "From Frigate" : "No Frigate login", 120, 134, 12, Align::MiddleCenter, 0x6B4D, TFT_BLACK);
	}
	m_manager.reset();
	m_manager.flush();
}

void FrigateCams::startLogin()
{
	static char body[160];
	/* the JSON escapes: the password is written as it is (use letters and digits); quotes or backslashes in it
	   would be escaped here */
	int n = snprintf(body, sizeof body, "{\"user\":\"%s\",\"password\":\"", camlogin.user);
	for(const char *p = camlogin.pass; *p && n < (int)sizeof body - 4; p++)
	{
		if(*p == '"' || *p == '\\')
			body[n++] = '\\';
		body[n++] = *p;
	}
	snprintf(body + n, sizeof body - n, "\"}");
	https_req_t r = { camlogin.host, camlogin.port, "POST", "/api/login", NULL, body, camlogin.pin, false };
	if(https_request(&r, fg_buf[m_buf], sizeof fg_buf[0]))
	{
		m_step = LOGGING_IN;
		m_busy = true;
	}
	memset(body, 0, sizeof body);                 /* (https.c keeps its own copy only until the answer) */
}

/* one camera's newest picture; down the kept connection while the set shows (the hourly check opens and closes its own) */
void FrigateCams::startFetch(int orb, int cam)
{
	static char path[96], hdr[700];
	snprintf(path, sizeof path, "/api/%s/latest.jpg?height=240", CAM_ID[cam]);
	snprintf(hdr, sizeof hdr, "Cookie: frigate_token=%s\r\n", m_token);
	https_req_t r = { camlogin.host, camlogin.port, "GET", path, hdr, NULL, camlogin.pin, !m_checkOnly };
	if(https_request(&r, fg_buf[m_buf], sizeof fg_buf[0]))
	{
		m_step = FETCHING;
		m_busy = true;
		m_fetchOrb = orb;
		m_fetchCam = cam;
	}
	memset(hdr, 0, sizeof hdr);
}

/* the next thing to ask for, if any (and the HTTPS client is free) */
void FrigateCams::startNext(uint32_t now, bool may_fetch)
{
	if(!camlogin_set() || !may_fetch || (int32_t)(now - m_retryAt) < 0)
		return;
	if(!m_showing)
	{
		if((int32_t)(now - m_nextCheck) < 0)
			return;
		m_checkOnly = true;                        /* the hourly check while the rig is there */
	}
	else
		m_checkOnly = false;
	if(!m_token[0])
		startLogin();
	else if(m_checkOnly)
		startFetch(-1, 2);
	else
		startFetch(m_orb, cam_for(m_page, m_orb));
}

void FrigateCams::poll(uint32_t now, bool may_fetch, bool showing)
{
	if(showing && !m_showing)
		m_orb = 0;
	bool left = m_showing && !showing;
	m_showing = showing;
	if(m_busy)
	{
		https_state_t hs = https_poll();
		if(hs == HTTPS_DONE || hs == HTTPS_FAILED)
			finished(now, may_fetch);
		return;
	}
	if(left || (!showing && https_kept()))
		https_close();                             /* (the set went: its kept connection too, freeing ~40 KB) */
	startNext(now, may_fetch);
}

void FrigateCams::finished(uint32_t now, bool may_fetch)
{
	m_busy = false;
	https_state_t hs = https_poll();
	int st = https_status();
	size_t bl = 0;
	const char *body = https_body(&bl);
	m_lastMs = https_ms();
	if(m_step == LOGGING_IN)
	{
		m_step = IDLE;
		size_t cl = 0;
		const char *ck = hs == HTTPS_DONE ? https_header("Set-Cookie", &cl) : nullptr;
		const char *tok = ck ? strstr(ck, "frigate_token=") : nullptr;
		if(hs == HTTPS_DONE && st == 200 && tok && tok < ck + cl)
		{
			tok += 14;
			size_t tl = 0;
			while(tok + tl < ck + cl && tok[tl] != ';' && tl < sizeof m_token - 1)
				tl++;
			memcpy(m_token, tok, tl);
			m_token[tl] = 0;
			m_loginFails = 0;
			printf("EV FRIGATE logged in as %s (%u ms)\n", camlogin.user, m_lastMs);
		}
		else
		{
			m_fails++;
			m_loginFails++;
			m_tried = true;
			m_lastOk = false;
			if(hs == HTTPS_FAILED)
				snprintf(m_err, sizeof m_err, "login: %s", https_error());
			else
				snprintf(m_err, sizeof m_err, "Frigate refused the login (HTTP %d)", st);
			printf("EV FRIGATE %s\n", m_err);
			m_retryAt = now + (m_loginFails < 3 ? 10000u : 60000u);
			m_nextCheck = now + CHECK_EVERY_MS;
			if(m_showing)
				for(int o = 0; o < 5; o++)
					drawNoPicture(o, cam_for(m_page, o), hs == HTTPS_FAILED ? "Frigate didn't answer" : "Frigate refused the login");
		}
		https_reset();
		return;
	}
	m_step = IDLE;
	int orb = m_fetchOrb, cam = m_fetchCam;
	bool was_check = m_checkOnly;
	if(hs == HTTPS_DONE && st == 200 && bl > 100)
	{
		snprintf(m_err, sizeof m_err, "none");
		m_tried = m_lastOk = true;
		m_okAt = now;
		m_pictures++;
		m_nextCheck = now + CHECK_EVERY_MS;
		https_reset();
		if(was_check)
		{
			printf("EV FRIGATE check ok: %s %u bytes in %u ms\n", CAM_ID[cam], (unsigned)bl, m_lastMs);
			https_close();
			return;
		}
		/* the next one asked for now, into the other buffer, while this one is drawn */
		m_buf ^= 1;
		m_orb = (m_orb + 1) % 5;
		if(m_showing)
			startNext(now, may_fetch);
		if(orb >= 0 && cam == cam_for(m_page, orb))   /* (unless the page changed on the way) */
			drawPicture(orb, cam, (const uint8_t *)body, bl, now);
		return;
	}
	if(hs == HTTPS_DONE && st == 401)
		memset(m_token, 0, sizeof m_token);        /* the session ran out: log in again next time */
	else
	{
		m_fails++;
		m_tried = true;
		m_lastOk = false;
		if(hs == HTTPS_FAILED)
			snprintf(m_err, sizeof m_err, "%s: %s", CAM_ID[cam], https_error());
		else
			snprintf(m_err, sizeof m_err, "%s: Frigate said HTTP %d", CAM_ID[cam], st);
		printf("EV FRIGATE %s\n", m_err);
		m_nextCheck = now + (was_check ? 600000u : CHECK_EVERY_MS);   /* (a failed check: again in 10 minutes) */
		if(!was_check && orb >= 0)
		{
			drawNoPicture(orb, cam, hs == HTTPS_FAILED ? "Frigate didn't answer" : "Frigate has no picture");
			m_orb = (m_orb + 1) % 5;
			m_retryAt = now + 2000;
		}
	}
	https_reset();
	if(was_check)
		https_close();
}

/* the picture cut to fill the orb (Frigate's is 16:9, 240 high), the camera's name at the bottom and the time at the
   top, in dark rounded labels as on the rig's camera faces */
void FrigateCams::drawPicture(int orb, int cam, const uint8_t *jpg, size_t len, uint32_t now)
{
	int w = 240, h = 240;
	for(size_t i = 2; i + 9 < len; i++)          /* the picture's size from its frame header (SOF0 / SOF2) */
		if(jpg[i] == 0xFF && (jpg[i + 1] == 0xC0 || jpg[i + 1] == 0xC2))
		{
			h = jpg[i + 5] << 8 | jpg[i + 6];
			w = jpg[i + 7] << 8 | jpg[i + 8];
			break;
		}
	pics_wait_idle();
	m_manager.selectScreen(orb);
	m_manager.fillScreen(TFT_BLACK);
	JRESULT jr = m_manager.drawJpgCropped(120 - w / 2, 120 - h / 2, jpg, (uint32_t)len);
	/* how much of the orb the picture lit (pixels that aren't black, inside the round screen): said in "?" so a black
	   orb shows in the numbers too (2026-11-17: the first test counted 321 pictures over black screens) */
	{
		const uint16_t *fb = orb_fb[orb];
		uint32_t in = 0, lit = 0;
		for(int yy = 0; yy < 240; yy += 4)
			for(int xx = 0; xx < 240; xx += 4)
				if((xx - 120) * (xx - 120) + (yy - 120) * (yy - 120) < 110 * 110)
				{
					in++;
					lit += fb[yy * 240 + xx] != 0;
				}
		m_lastLit = in ? (uint8_t)(lit * 100 / in) : 0;
		m_lastDecode = (int)jr;
	}
	TFT_eSPI &d = m_manager.getDisplay();
	m_manager.setFont(ROBOTO_REGULAR);
	const char *name = CAM_TITLE[cam];
	int pw = (int)strlen(name) * 8 + 20;
	d.fillSmoothRoundRect(120 - pw / 2, 196, pw, 20, 10, 0x0861);
	m_manager.drawString(name, 120, 206, 11, Align::MiddleCenter, TFT_WHITE, 0x0861);
	char stamp[16] = "";
	if(frigate_tz)
	{
		orbs_tm_t tm;
		orbs_tz_local(frigate_tz, orbs_time_now(), &tm);
		int hh = frigate_12h ? (tm.hour % 12 == 0 ? 12 : tm.hour % 12) : tm.hour;
		if(frigate_12h)
			snprintf(stamp, sizeof stamp, "%d:%02d:%02d %s", hh, tm.min, tm.sec, tm.hour >= 12 ? "PM" : "AM");
		else
			snprintf(stamp, sizeof stamp, "%02d:%02d:%02d", hh, tm.min, tm.sec);
	}
	int tw = (int)strlen(stamp) * 7 + 24;
	d.fillSmoothRoundRect(120 - tw / 2, 16, tw, 16, 8, 0x0861);
	d.fillSmoothCircle(120 - tw / 2 + 10, 24, 3, 0x2E8B, 0x0861);    /* green: a fresh picture */
	m_manager.drawString(stamp, 124, 24, 10, Align::MiddleCenter, 0xDF1D, 0x0861);
	m_manager.reset();
	m_manager.flush();
	(void)now;
}

void FrigateCams::drawNoPicture(int orb, int cam, const char *why)
{
	pics_wait_idle();
	m_manager.selectScreen(orb);
	m_manager.fillScreen(TFT_BLACK);
	m_manager.getDisplay().drawArc(120, 120, 119, 116, 0, 360, 0xF206, TFT_BLACK, true);
	m_manager.setFont(ROBOTO_REGULAR);
	m_manager.drawString(CAM_TITLE[cam], 120, 96, 13, Align::MiddleCenter, 0x9CF3, TFT_BLACK);
	m_manager.drawString("No picture", 120, 124, 16, Align::MiddleCenter, 0xF206, TFT_BLACK);
	m_manager.drawString(why, 120, 150, 11, Align::MiddleCenter, 0x8410, TFT_BLACK);
	m_manager.reset();
	m_manager.flush();
}
