#include "web.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "settings_json.h"
extern "C" {
#include "hotspot.h"
#include "https.h"
#include "assets.h"
#include "orbs_time.h"
}

#define WEB_CONNS 8                        /* (the page: its state and the five orb pictures, one after another) */
#define REQ_MAX (16 * 1024)                /* a request with its body (a whole settings file is ~5.5 KB since v4) */
#define OUT_MAX (40 * 1024)                /* an answer made here (the JSON ~4 KB, an orb's picture 29 KB) */
#define IDLE_MS 10000

struct WebConn {
	struct tcp_pcb *pcb;
	bool used, req_done, answering, closing;
	char *req;                             /* (REQ_MAX + 1 bytes in PSRAM: req_buf, since 2026-11-17) */
	size_t req_len, hdr_len, body_len;
	uint8_t *out;                          /* the answer: its header and (often) its body */
	size_t out_len, out_pos;
	const uint8_t *ext;                    /* a body kept elsewhere (the page in flash, a JPEG in PSRAM), sent after out */
	size_t ext_len, ext_pos;
	uint32_t last_ms;
	bool geocode;                          /* waiting for OpenWeatherMap's place search */
};
static WebConn conns[WEB_CONNS];
static char req_buf[WEB_CONNS][REQ_MAX + 1] __uninitialized_psram("webreq");
static uint8_t out_buf[WEB_CONNS][OUT_MAX] __uninitialized_psram("webout");
static struct tcp_pcb *listener;
static uint32_t requests;

uint32_t web_requests(void) { return requests; }

/* ---- lwIP's side (its interrupt: only buffers here) ---- */
static void conn_free(WebConn *c)
{
	c->pcb = nullptr;
	c->used = c->req_done = c->answering = c->closing = c->geocode = false;
	c->req_len = c->hdr_len = c->body_len = c->out_len = c->out_pos = c->ext_len = c->ext_pos = 0;
	c->ext = nullptr;
}

static void on_err(void *arg, err_t err)
{
	(void)err;
	conn_free((WebConn *)arg);             /* (lwIP has freed the pcb) */
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	WebConn *c = (WebConn *)arg;
	if(!p)
	{
		/* the browser closed its side: finish sending if there's an answer on the way, else close */
		if(!c->answering)
		{
			tcp_arg(pcb, nullptr);
			if(tcp_close(pcb) != ERR_OK)
				tcp_abort(pcb);
			conn_free(c);
			return ERR_OK;
		}
		c->closing = true;
		return ERR_OK;
	}
	if(err != ERR_OK)
	{
		pbuf_free(p);
		return err;
	}
	if(!c->req_done)
	{
		size_t n = p->tot_len;
		if(c->req_len + n > REQ_MAX)
			n = REQ_MAX - c->req_len;
		pbuf_copy_partial(p, c->req + c->req_len, (u16_t)n, 0);
		c->req_len += n;
		c->req[c->req_len] = 0;
		if(!c->hdr_len)
		{
			char *e = strstr(c->req, "\r\n\r\n");
			if(e)
			{
				c->hdr_len = (size_t)(e + 4 - c->req);
				for(char *h = strstr(c->req, "\r\n"); h && h < e; h = strstr(h + 2, "\r\n"))
					if(strncasecmp(h + 2, "Content-Length:", 15) == 0)
						c->body_len = (size_t)strtoul(h + 17, nullptr, 10);
			}
		}
		if(c->hdr_len && (c->req_len >= c->hdr_len + c->body_len || c->req_len >= REQ_MAX))
			c->req_done = true;
	}
	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	c->last_ms = to_ms_since_boot(get_absolute_time());
	return ERR_OK;
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb)
{
	WebConn *c = (WebConn *)arg;
	if(c && to_ms_since_boot(get_absolute_time()) - c->last_ms > IDLE_MS)
	{
		tcp_arg(pcb, nullptr);
		tcp_abort(pcb);                    /* (a browser that went quiet: its slot back) */
		conn_free(c);
		return ERR_ABRT;
	}
	return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
	(void)arg;
	if(err != ERR_OK || !pcb)
		return ERR_VAL;
	WebConn *c = nullptr;
	for(auto &x : conns)
		if(!x.used) { c = &x; break; }
	if(!c)
	{
		tcp_abort(pcb);                    /* (all busy: the browser tries again) */
		return ERR_ABRT;
	}
	conn_free(c);
	c->used = true;
	c->pcb = pcb;
	c->out = out_buf[c - conns];
	c->last_ms = to_ms_since_boot(get_absolute_time());
	tcp_arg(pcb, c);
	tcp_recv(pcb, on_recv);
	tcp_err(pcb, on_err);
	tcp_poll(pcb, on_poll, 4);             /* (every 2 s) */
	tcp_nagle_disable(pcb);
	return ERR_OK;
}

void web_init(void)
{
	if(listener)
		return;
	for(int i = 0; i < WEB_CONNS; i++)
		conns[i].req = req_buf[i];
	cyw43_arch_lwip_begin();
	struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
	if(pcb && tcp_bind(pcb, IP_ANY_TYPE, 80) == ERR_OK)
	{
		listener = tcp_listen_with_backlog(pcb, WEB_CONNS);
		tcp_accept(listener, on_accept);
	}
	else if(pcb)
		tcp_close(pcb);
	cyw43_arch_lwip_end();
	printf("EV WEB %s on port 80\n", listener ? "listening" : "COULD NOT listen");
}

/* ---- the main loop's side ---- */
static void begin(WebConn *c, int code, const char *ctype, size_t body_len, const char *extra = "")
{
	const char *text = code == 200 ? "OK" : code == 204 ? "No Content" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" :
	                   code == 404 ? "Not Found" : code == 413 ? "Too Large" : "Error";
	c->out_len = (size_t)snprintf((char *)c->out, 512, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
	                              "Cache-Control: no-store\r\nConnection: close\r\n%s\r\n", code, text, ctype, (unsigned)body_len, extra);
}

/* a JSON document as the whole answer */
static void send_json(WebConn *c, JsonDocument &doc, int code = 200, const char *extra = "")
{
	size_t n = measureJson(doc);
	begin(c, code, "application/json", n, extra);
	if(c->out_len + n > OUT_MAX)
	{
		begin(c, 500, "application/json", 41);
		memcpy(c->out + c->out_len, "{\"ok\":false,\"error\":\"answer too big\"}   ", 41);
		c->out_len += 41;
		return;
	}
	serializeJson(doc, (char *)c->out + c->out_len, OUT_MAX - c->out_len);
	c->out_len += n;
}

static void send_error(WebConn *c, int code, const char *why)
{
	JsonDocument d;
	d["ok"] = false;
	d["error"] = why;
	send_json(c, d, code);
}

static void fill_state(JsonDocument &d)
{
	settings_to_json(settings, d["settings"].to<JsonObject>());
	web_status(d["status"].to<JsonObject>());
	JsonObject meta = d["meta"].to<JsonObject>();
	JsonArray sets = meta["sets"].to<JsonArray>();
	uint32_t known = settings_sets_known();
	for(int i = 0; i < N_SETS_SAVED; i++)
	{
		if(!(known >> i & 1))
			continue;                          /* (only the sets that exist: the XL's own and the rig's built ones) */
		JsonObject s = sets.add<JsonObject>();
		s["key"] = SET_KEYS[i];
		s["title"] = SET_TITLES[i];
		s["rig"] = SET_IS_RIG[i];
		/* what its five orbs are: faces from a list (the rig's sets, Messages), its own five screens (weather), or one
		   picture over all five (clock, matrix, visualizer) */
		s["orbs"] = (i == 1 || i == 2 || i == 3) ? "screens" : (i == 0 || i == 4 || i == 9) ? "together" : "faces";
	}
	JsonArray th = meta["themes"].to<JsonArray>();
	for(int i = 0; i < N_THEMES; i++) th.add(THEMES[i]);
	JsonArray ac = meta["actions"].to<JsonArray>();
	for(int i = 0; i < N_ACTIONS; i++) ac.add(ACTIONS[i]);
	JsonArray zones = meta["zones"].to<JsonArray>();
	for(int i = 0; i < orbs_tz_count(); i++)
		zones.add(orbs_tz_name(i));
	JsonArray cams = meta["cams"].to<JsonArray>();
	for(int i = 0; i < N_CAMS_ALL; i++)
	{
		JsonObject cm = cams.add<JsonObject>();
		cm["id"] = CAM_IDS[i];
		cm["title"] = CAM_TITLES[i];
	}
}

/* ---- OpenWeatherMap's place search, through the XL's own key (never sent to the browser) ---- */
static char geo_q[96];
static bool geo_started;
static char geo_buf[8 * 1024];

static bool geo_query_ok(const char *q)
{
	if(!*q || strlen(q) >= sizeof geo_q)
		return false;
	for(const char *p = q; *p; p++)                /* (already URL-encoded by the page: passed on as it is) */
		if(!(isalnum((unsigned char)*p) || strchr("%,.+-_", *p)))
			return false;
	return true;
}

static void geo_answer(WebConn *c, https_state_t hs)
{
	JsonDocument d;
	size_t bl = 0;
	const char *body = https_body(&bl);
	JsonDocument in;
	if(hs == HTTPS_DONE && https_status() == 200 && deserializeJson(in, body, bl) == DeserializationError::Ok && in.is<JsonArrayConst>())
	{
		d["ok"] = true;
		JsonArray out = d["places"].to<JsonArray>();
		for(JsonObjectConst p : in.as<JsonArrayConst>())
		{
			JsonObject o = out.add<JsonObject>();
			o["name"] = p["name"];
			o["state"] = p["state"] | "";
			o["country"] = p["country"] | "";
			o["lat"] = p["lat"];
			o["lon"] = p["lon"];
		}
	}
	else
	{
		d["ok"] = false;
		d["error"] = hs == HTTPS_FAILED ? "OpenWeatherMap didn't answer" : "OpenWeatherMap said no";
	}
	https_reset();
	send_json(c, d);
	c->answering = true;
}

/* who sent a change, and what (the page never carries a password or a key): "EV SETTINGS from 192.168.1.36: {...}", in
   the rig's journal through the feed (2026-11-17: the clock turned #1100ff and nothing said from where) */
static void say(WebConn *c, const char *what, const char *body, size_t n)
{
	char who[20] = "?";
	if(c->pcb)
		snprintf(who, sizeof who, "%s", ipaddr_ntoa(&c->pcb->remote_ip));
	char b[161];
	size_t k = 0;
	for(size_t i = 0; i < n && k < sizeof b - 1; i++)
		if(body[i] >= ' ' && body[i] < 127)
			b[k++] = body[i];
	b[k] = 0;
	printf("EV %s from %s: %s%s\n", what, who, b, n > k ? " ..." : "");
}

static void handle(WebConn *c)
{
	requests++;
	char method[8] = "", path[200] = "";
	sscanf(c->req, "%7s %199s", method, path);
	const char *body = c->req + c->hdr_len;
	size_t blen = c->req_len - c->hdr_len;
	char *q = strchr(path, '?');
	char *query = q ? q + 1 : nullptr;
	if(q)
		*q = 0;
	if(c->req_len >= REQ_MAX && c->hdr_len + c->body_len > REQ_MAX)
		return send_error(c, 413, "too big");
	if(!strcmp(path, "/favicon.ico"))
	{
		begin(c, 204, "image/x-icon", 0);          /* (browsers ask for a tab icon: none, quietly) */
		return;
	}
	/* the setup hotspot: its Wi-Fi page at / and at every address a phone tries to see whether there's internet (so an
	   iPhone opens it by itself); the settings page at /settings */
	bool hs = hotspot_on();
	if(hs && !strcmp(method, "GET") && strncmp(path, "/api/", 5) && strcmp(path, "/settings"))
	{
		begin(c, 200, "text/html; charset=utf-8", web_wifi_gz_len, "Content-Encoding: gzip\r\n");
		c->ext = web_wifi_gz;
		c->ext_len = web_wifi_gz_len;
		return;
	}
	if(!strcmp(method, "GET") && (!strcmp(path, "/") || !strcmp(path, "/settings")))
	{
		begin(c, 200, "text/html; charset=utf-8", web_index_gz_len, "Content-Encoding: gzip\r\n");
		c->ext = web_index_gz;
		c->ext_len = web_index_gz_len;
		return;
	}
	if(!strcmp(method, "GET") && !strcmp(path, "/api/wifi/networks"))
	{
		JsonDocument d;
		JsonArray a = d["networks"].to<JsonArray>();
		const hotspot_net_t *nets;
		int n = hs ? hotspot_networks(&nets) : 0;
		for(int i = 0; i < n; i++)
		{
			JsonObject o = a.add<JsonObject>();
			o["ssid"] = nets[i].ssid;
			o["rssi"] = nets[i].rssi;
			o["secured"] = nets[i].secured != 0;
		}
		return send_json(c, d);
	}
	if(!strcmp(method, "POST") && !strcmp(path, "/api/wifi"))
	{
		/* the home Wi-Fi's password: only from the orbs' own hotspot (whoever joined it saw the password on the orbs),
		   never from the home network (2026-11-16) */
		if(!hs)
			return send_error(c, 403, "only from the orbs' own setup hotspot");
		JsonDocument in;
		if(deserializeJson(in, body, blen) != DeserializationError::Ok)
			return send_error(c, 400, "not JSON");
		const char *ssid = in["ssid"] | "", *pass = in["password"] | "";
		size_t sl = strlen(ssid), pl = strlen(pass);
		if(sl < 1 || sl > 32)
			return send_error(c, 200, "the network name: 1 to 32 characters");
		if(pl && (pl < 8 || pl > 63))
			return send_error(c, 200, "a Wi-Fi password is 8 to 63 characters, or none for an open network");
		char who[20] = "?";
		if(c->pcb)
			snprintf(who, sizeof who, "%s", ipaddr_ntoa(&c->pcb->remote_ip));
		printf("EV WIFI new network \"%s\" from the hotspot page (%s), password %u characters\n", ssid, who, (unsigned)pl);
		web_wifi_save(ssid, pass);
		memset((char *)body, 0, blen);                   /* (no copy of the password left in the request ...) */
		memset((char *)pass, 0, pl);                     /* (... or in the parsed copy) */
		JsonDocument out;
		out["ok"] = true;
		return send_json(c, out);
	}
	if(!strcmp(method, "GET") && !strcmp(path, "/api/state"))
	{
		JsonDocument d;
		fill_state(d);
		return send_json(c, d);
	}
	if(!strcmp(method, "GET") && !strcmp(path, "/api/backup"))
	{
		JsonDocument d;
		settings_to_json(settings, d.to<JsonObject>());
		return send_json(c, d, 200, "Content-Disposition: attachment; filename=\"orbs-settings.json\"\r\n");
	}
	if(!strcmp(method, "POST") && !strcmp(path, "/api/settings"))
	{
		JsonDocument in, out;
		if(deserializeJson(in, body, blen) != DeserializationError::Ok || !in.is<JsonObjectConst>())
			return send_error(c, 400, "not JSON");
		orbs_settings_t next = settings;
		JsonObject errs = out["errors"].to<JsonObject>();
		if(!settings_patch(next, in.as<JsonObjectConst>(), errs))
		{
			out["ok"] = false;
			return send_json(c, out);
		}
		out.remove("errors");
		say(c, "SETTINGS", body, blen);
		if(!web_apply(next))
			return send_error(c, 200, "the orbs couldn't take it now");
		out["ok"] = true;
		settings_to_json(settings, out["settings"].to<JsonObject>());
		return send_json(c, out);
	}
	if(!strcmp(method, "POST") && !strcmp(path, "/api/action"))
	{
		JsonDocument in, out;
		if(deserializeJson(in, body, blen) != DeserializationError::Ok || !in.is<JsonObjectConst>())
			return send_error(c, 400, "not JSON");
		char why[80] = "";
		say(c, "ACTION", body, blen);
		bool ok = web_action(in.as<JsonObjectConst>(), why, sizeof why);
		out["ok"] = ok;
		if(!ok)
			out["error"] = why;
		return send_json(c, out);
	}
	if(!strcmp(method, "GET") && !strcmp(path, "/api/presets"))
	{
		JsonDocument out;
		web_presets(out["presets"].to<JsonArray>());
		return send_json(c, out);
	}
	if(!strcmp(method, "POST") && !strcmp(path, "/api/presets"))
	{
		JsonDocument in, out;
		if(deserializeJson(in, body, blen) != DeserializationError::Ok || !in.is<JsonObjectConst>())
			return send_error(c, 400, "not JSON");
		char why[80] = "";
		say(c, "PRESET", body, blen);
		bool ok = web_preset(in.as<JsonObjectConst>(), why, sizeof why);
		out["ok"] = ok;
		if(!ok)
			out["error"] = why;
		web_presets(out["presets"].to<JsonArray>());
		return send_json(c, out);
	}
	if(!strcmp(method, "GET") && !strncmp(path, "/api/orb/", 9) && path[9] >= '0' && path[9] <= '4' && !path[10])
	{
		const uint8_t *ext = nullptr;
		const char *ctype = "image/bmp";
		size_t n = web_orb_picture(path[9] - '0', c->out + 512, OUT_MAX - 512, &ext, &ctype);
		if(!n)
			return send_error(c, 404, "no picture");
		begin(c, 200, ctype, n);
		if(ext)
		{
			c->ext = ext;
			c->ext_len = n;
		}
		else
		{
			memmove(c->out + c->out_len, c->out + 512, n);
			c->out_len += n;
		}
		return;
	}
	if(!strcmp(method, "GET") && !strcmp(path, "/api/geocode"))
	{
		const char *qq = query && !strncmp(query, "q=", 2) ? query + 2 : "";
		if(!settings.owm_key[0])
			return send_error(c, 200, "no weather key on the orbs (orbs key)");
		if(!geo_query_ok(qq))
			return send_error(c, 200, "letters, digits, spaces and commas only");
		if(geo_q[0])
			return send_error(c, 200, "a search is already running");
		snprintf(geo_q, sizeof geo_q, "%s", qq);
		geo_started = false;
		c->geocode = true;                 /* (answered when OpenWeatherMap has: web_poll) */
		return;
	}
	send_error(c, 404, "no such page");
}

static void geo_poll(void)
{
	WebConn *c = nullptr;
	for(auto &x : conns)
		if(x.used && x.geocode)
			c = &x;
	if(!geo_q[0])
		return;
	if(!c)                                     /* (the browser gave up) */
	{
		if(geo_started && https_poll() != HTTPS_BUSY)
			https_reset();
		if(!geo_started || https_poll() != HTTPS_BUSY)
			geo_q[0] = 0;
		return;
	}
	if(!geo_started)
	{
		if(!web_https_free())
			return;
		static char path[200];
		snprintf(path, sizeof path, "/geo/1.0/direct?q=%s&limit=5&appid=%s", geo_q, settings.owm_key);
		geo_started = https_start("api.openweathermap.org", path, geo_buf, sizeof geo_buf);
		memset(path, 0, sizeof path);              /* (it held the key) */
		return;
	}
	https_state_t hs = https_poll();
	if(hs == HTTPS_DONE || hs == HTTPS_FAILED)
	{
		c->geocode = false;
		geo_answer(c, hs);
		geo_q[0] = 0;
		geo_started = false;
	}
}

bool web_geocode_busy(void) { return geo_started; }

/* a body kept elsewhere that a connection hasn't handed to TCP yet lies in [p, p + len) (it must not change till then) */
bool web_sending(const uint8_t *p, size_t len)
{
	for(auto &c : conns)
		if(c.used && c.ext && c.ext_pos < c.ext_len && c.ext < p + len && c.ext + c.ext_len > p)
			return true;
	return false;
}

void web_poll(uint32_t now)
{
	(void)now;
	geo_poll();
	for(auto &x : conns)
	{
		WebConn *c = &x;
		if(!c->used)
			continue;
		/* a request whole: answered here */
		if(c->req_done && !c->answering && !c->geocode)
		{
			cyw43_arch_lwip_begin();               /* (the network can't free this connection while it's answered) */
			if(c->used)
			{
				handle(c);
				if(!c->geocode)
					c->answering = true;
			}
			cyw43_arch_lwip_end();
		}
		if(!c->answering || !c->pcb)
			continue;
		cyw43_arch_lwip_begin();
		while(c->pcb)
		{
			size_t room = tcp_sndbuf(c->pcb);
			const uint8_t *src;
			size_t left;
			if(c->out_pos < c->out_len) { src = c->out + c->out_pos; left = c->out_len - c->out_pos; }
			else if(c->ext_pos < c->ext_len) { src = c->ext + c->ext_pos; left = c->ext_len - c->ext_pos; }
			else
			{
				/* all of it handed to TCP: close (it sends the rest first) */
				tcp_arg(c->pcb, nullptr);
				if(tcp_close(c->pcb) != ERR_OK)
					tcp_abort(c->pcb);
				conn_free(c);
				break;
			}
			if(room == 0 || tcp_sndqueuelen(c->pcb) >= TCP_SND_QUEUELEN)
				break;
			size_t n = left < room ? left : room;
			if(n > 1460)
				n = 1460;
			if(tcp_write(c->pcb, src, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK)
				break;
			if(c->out_pos < c->out_len) c->out_pos += n; else c->ext_pos += n;
			c->last_ms = to_ms_since_boot(get_absolute_time());
		}
		if(c->pcb)
			tcp_output(c->pcb);
		cyw43_arch_lwip_end();
	}
}
