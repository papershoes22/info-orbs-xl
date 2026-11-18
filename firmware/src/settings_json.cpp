#include "settings_json.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
extern "C" {
#include "orbs_time.h"
}

const char *const SET_KEYS[N_SETS_SAVED] = { "clock", "weather", "hourly", "sky", "matrix", "rig", "music", "trove", "cams", "viz",
	"printer", "claude", "ai", "events", "house", "network", "backups", "projects", "movers", "gaming", "radar", "messages",
	"spare22", "spare23" };
const char *const SET_TITLES[N_SETS_SAVED] = { "Clock", "Weather", "Hourly", "Sky", "Matrix", "Rig stats", "Music", "Trove", "Cameras",
	"Visualizer", "3D printer", "Claude", "AI and art", "Security events", "House", "Network", "Backups and drives", "Projects",
	"Trove movers", "Gaming", "Weather radar", "Messages", "(spare)", "(spare)" };
const bool SET_IS_RIG[N_SETS_SAVED] = { false, false, false, false, false, true, true, true, true, true,
	true, true, true, true, true, true, true, true, true, true, true, false, false, false };
const char *const THEMES[N_THEMES] = { "own", "ember", "ocean", "forest", "mono", "neon", "sunset", "ice" };
const char *const ACTIONS[N_ACTIONS] = { "own", "nothing", "prev_set", "next_set", "qr", "dark", "play_pause", "next_track",
	"prev_track", "cams_page", "clock_style", "clock_format", "middle_page", "brighter", "dimmer" };
static const char *const STYLES_G[5] = { "own", "ring", "needle", "bar", "number" };
static const char *const BTN_NAMES[3] = { "left", "middle", "right" };
static const char *const KIND_NAMES[3] = { "short", "medium", "long" };
static const char *const EVENTS[6] = { "", "music", "game", "rig_sleep", "person_door", "print_done" };
static const char *const DAYS[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
static const char *const WARN_KEYS[N_WARN] = { "cpu", "gpu", "ssd", "ram" };
static const char *const CUSTOM_KINDS[3] = { "none", "message", "countdown" };
static const char *const SIZES[4] = { "auto", "small", "medium", "big" };
static const char *const TEMPS[2] = { "C", "F" };
static const char *const DATES[3] = { "mdy", "dmy", "ymd" };
static const char *const DECIMALS[3] = { "none", "auto", "more" };
static const bool SET_IS_NATIVE5[N_SETS_SAVED] = { false, true, true, true, false };   /* (weather, hourly, sky: 5 screens to place) */

uint32_t sets_from_rig;
uint32_t settings_sets_known(void) { return 0x3FFu | SETS_NEW_ON | sets_from_rig; }
int set_key_index_any(const char *key)
{
	for(int i = 0; i < N_SETS_SAVED; i++)
		if(key && strcmp(key, SET_KEYS[i]) == 0)
			return i;
	return -1;
}
const char *const CAM_IDS[N_CAMS_ALL] = { "cam1", "cam2", "cam3", "cam4", "cam5", "cam6", "cam7" };   /* your Frigate camera names */
const char *const CAM_TITLES[N_CAMS_ALL] = { "Camera 1", "Camera 2", "Camera 3", "Camera 4", "Camera 5", "Camera 6", "Camera 7" };
static const char *const FORMATS[3] = { "24", "12", "12ampm" };
static const char *const MIDDLES[4] = { "memory", "health", "power", "cooling" };
static const char *const NIGHTS[3] = { "off", "dim", "dark" };
static const char *const LINKS[3] = { "normal", "fast", "fastest" };   /* (screens.link, 2026-11-18) */

int set_key_index(const char *key)
{
	uint32_t known = settings_sets_known();
	for(int i = 0; i < N_SETS_SAVED; i++)
		if(key && (known >> i & 1) && strcmp(key, SET_KEYS[i]) == 0)
			return i;
	return -1;
}

/* a button action as text: one of ACTIONS, or "show:<set>" */
static void action_text(char *out, size_t n, uint8_t a)
{
	if(a >= 0xC0 && a - 0xC0 < N_SETS_SAVED)
		snprintf(out, n, "show:%s", SET_KEYS[a - 0xC0]);
	else
		snprintf(out, n, "%s", ACTIONS[a < N_ACTIONS ? a : 0]);
}

static int action_code(const char *t)
{
	if(!t)
		return -1;
	if(strncmp(t, "show:", 5) == 0)
	{
		int k = set_key_index(t + 5);
		return k < 0 ? -1 : 0xC0 + k;
	}
	for(int i = 0; i < N_ACTIONS; i++)
		if(strcmp(t, ACTIONS[i]) == 0)
			return i;
	return -1;
}

static void hex(char *out, uint32_t c) { snprintf(out, 8, "#%02x%02x%02x", (unsigned)(c >> 16 & 255), (unsigned)(c >> 8 & 255), (unsigned)(c & 255)); }
static void hhmm(char *out, uint16_t m) { snprintf(out, 6, "%02u:%02u", (unsigned)(m / 60 % 24), (unsigned)(m % 60)); }

void settings_to_json(const orbs_settings_t &s, JsonObject o)
{
	char b[8];
	uint32_t known = settings_sets_known();
	JsonObject sets = o["sets"].to<JsonObject>();
	JsonArray order = sets["order"].to<JsonArray>();
	for(int i = 0; i < N_SETS_SAVED; i++)
		if(s.set_order[i] < N_SETS_SAVED && (known >> s.set_order[i] & 1))
			order.add(SET_KEYS[s.set_order[i]]);
	JsonObject en = sets["enabled"].to<JsonObject>();
	for(int i = 0; i < N_SETS_SAVED; i++)
		if(known >> i & 1)
			en[SET_KEYS[i]] = (s.set_enabled >> i & 1) != 0;
	sets["cycle_s"] = s.cycle_s;
	sets["start"] = s.start_set < N_SETS_SAVED ? SET_KEYS[s.start_set] : "last";
	JsonObject c = o["clock"].to<JsonObject>();
	c["style"] = s.clock_type ? "nixie" : "normal";
	c["format"] = FORMATS[s.clock_format <= 2 ? s.clock_format : 2];
	c["ticks"] = s.show_second_ticks != 0;
	hex(b, s.clock_color); c["color"] = b;
	c["shadow"] = s.clock_shadowing != 0;
	hex(b, s.clock_shadow_color); c["shadow_color"] = b;
	hex(b, s.clock_nixie_color); c["nixie_color"] = b;
	c["tz"] = s.tz_name;
	c["ntp"] = s.ntp_server;
	JsonObject w = o["weather"].to<JsonObject>();
	w["enabled"] = s.weather_enabled != 0;
	w["city"] = s.weather_city;
	w["lat"] = atof(s.weather_lat);
	w["lon"] = atof(s.weather_lon);
	w["units"] = s.weather_units ? "imperial" : "metric";
	w["dark"] = s.weather_dark != 0;
	w["hl_cycle_s"] = s.weather_hl_cycle_s;
	w["refresh_min"] = s.weather_refresh_min;
	JsonObject m = o["matrix"].to<JsonObject>();
	m["enabled"] = (s.set_enabled >> 4 & 1) != 0;
	m["big_font"] = s.matrix_big_font != 0;
	hex(b, s.matrix_color); m["color"] = b;
	hex(b, s.matrix_head_color); m["head_color"] = b;
	m["line_min"] = s.matrix_line_min;
	m["line_max"] = s.matrix_line_max;
	m["speed_min"] = s.matrix_speed_min;
	m["speed_max"] = s.matrix_speed_max;
	m["frame_ms"] = s.matrix_frame_ms;
	JsonObject r = o["rig"].to<JsonObject>();
	r["rig_hz"] = s.rig_hz;
	r["rig_middle"] = MIDDLES[s.rig_middle & 3];
	r["kwh_price"] = s.kwh_price_m / 1000.0;
	for(int p = 0; p < 2; p++)
	{
		JsonArray a = r[p ? "cams_page2" : "cams_page1"].to<JsonArray>();
		for(int k = 0; k < 5; k++)
			a.add(CAM_IDS[s.cams_page[p][k] < N_CAMS_ALL ? s.cams_page[p][k] : 0]);
	}
	r["cam_quality"] = s.cam_quality;
	r["cam_fps"] = s.cam_fps;
	if(s.viz_color == VIZ_ALBUM)
		r["viz_color"] = "album";
	else if(s.viz_color == VIZ_RAINBOW)
		r["viz_color"] = "rainbow";
	else
	{
		hex(b, s.viz_color);
		r["viz_color"] = b;
	}
	JsonObject sc = o["screens"].to<JsonObject>();
	sc["brightness"] = s.brightness;
	sc["link"] = LINKS[s.link_speed <= 2 ? s.link_speed : 0];
	JsonObject n = sc["night"].to<JsonObject>();
	n["mode"] = NIGHTS[s.night_mode <= 2 ? s.night_mode : 0];
	hhmm(b, s.night_start_min); n["start"] = b;
	hhmm(b, s.night_end_min); n["end"] = b;
	n["brightness"] = s.night_brightness;
	JsonArray rot = sc["rotation"].to<JsonArray>();
	for(int i = 0; i < 5; i++)
		rot.add((s.rotation[i] & 3) * 90);
	/* ---- version 4 ---- */
	JsonArray orbs = sc["orbs"].to<JsonArray>();
	for(int i = 0; i < 5; i++)
	{
		JsonObject ob = orbs.add<JsonObject>();
		ob["brightness"] = s.orb_bright[i];
		JsonArray g = ob["gain"].to<JsonArray>();
		for(int k = 0; k < 3; k++)
			g.add(s.orb_gain[i][k]);
	}
	JsonObject per = sets["per"].to<JsonObject>();
	for(int i = 0; i < N_SETS_SAVED; i++)
	{
		if(!(known >> i & 1))
			continue;
		JsonObject ps = per[SET_KEYS[i]].to<JsonObject>();
		ps["cycle_s"] = s.set_cycle_s[i];
		ps["brightness"] = s.set_brightness[i];
		ps["theme"] = THEMES[s.set_theme[i] < N_THEMES ? s.set_theme[i] : 0];
		JsonArray fa = ps["faces"].to<JsonArray>();
		JsonArray st = ps["styles"].to<JsonArray>();
		for(int k = 0; k < 5; k++)
		{
			if(s.set_faces[i][k] == 0xFF)
				fa.add("blank");
			else
				fa.add(s.set_faces[i][k]);
			st.add(STYLES_G[s.set_style[i][k] < 5 ? s.set_style[i][k] : 0]);
		}
		JsonObject bt = ps["buttons"].to<JsonObject>();
		for(int bn = 0; bn < 3; bn++)
		{
			JsonObject bo = bt[BTN_NAMES[bn]].to<JsonObject>();
			for(int kd = 0; kd < 3; kd++)
			{
				char a[24];
				action_text(a, sizeof a, s.buttons[i][bn * 3 + kd]);
				bo[KIND_NAMES[kd]] = a;
			}
		}
	}
	JsonObject fm = o["formats"].to<JsonObject>();
	fm["temp"] = TEMPS[s.temp_unit & 1];
	fm["date"] = DATES[s.date_order <= 2 ? s.date_order : 0];
	fm["decimals"] = DECIMALS[s.decimals <= 2 ? s.decimals : 1];
	JsonArray rules = o["rules"].to<JsonArray>();
	for(int i = 0; i < N_RULES; i++)
	{
		const orbs_rule_t &r = s.rules[i];
		if(r.kind != RULE_TIME && r.kind != RULE_EVENT)
			continue;
		JsonObject ro = rules.add<JsonObject>();
		ro["kind"] = r.kind == RULE_TIME ? "time" : "event";
		ro["set"] = r.set == 0xFE ? "dark" : (r.set < N_SETS_SAVED ? SET_KEYS[r.set] : "clock");
		if(r.kind == RULE_TIME)
		{
			JsonArray d = ro["days"].to<JsonArray>();
			for(int k = 0; k < 7; k++)
				if(r.days >> k & 1)
					d.add(DAYS[k]);
			hhmm(b, r.start_min); ro["start"] = b;
			hhmm(b, r.end_min); ro["end"] = b;
		}
		else
		{
			ro["event"] = EVENTS[r.event <= 5 ? r.event : 0];
			ro["secs"] = r.secs;
		}
	}
	JsonObject wa = o["warnings"].to<JsonObject>();
	for(int i = 0; i < N_WARN; i++)
	{
		JsonObject wo = wa[WARN_KEYS[i]].to<JsonObject>();
		wo["on"] = s.warn[i].on != 0;
		wo["limit"] = s.warn[i].limit_c;
		wo["how"] = s.warn[i].how ? "pulse" : "colour";
	}
	JsonArray cu = o["custom"].to<JsonArray>();
	for(int i = 0; i < N_CUSTOM; i++)
	{
		const orbs_custom_t &cf = s.custom[i];
		JsonObject co = cu.add<JsonObject>();
		co["kind"] = CUSTOM_KINDS[cf.kind <= 2 ? cf.kind : 0];
		co["title"] = cf.title;
		co["text"] = cf.text;
		hex(b, cf.color); co["color"] = b;
		hex(b, cf.bg); co["bg"] = b;
		co["size"] = SIZES[cf.size <= 3 ? cf.size : 0];
		co["target"] = cf.target;              /* (Unix time; the page shows it as a local date and time) */
	}
}

/* ---- checking a change ---- */
namespace {
struct Ctx {
	JsonObject err;
	bool ok = true;
	void bad(const char *path, const char *why) { err[path] = why; ok = false; }
};

bool get_bool(Ctx &c, JsonVariantConst v, const char *path, uint8_t &out)
{
	if(!v.is<bool>()) { c.bad(path, "on or off"); return false; }
	out = v.as<bool>() ? 1 : 0;
	return true;
}

bool get_int(Ctx &c, JsonVariantConst v, const char *path, long lo, long hi, long &out, bool zero_ok = false)
{
	if(v.is<bool>() || !v.is<long>()) { c.bad(path, "a whole number"); return false; }
	long x = v.as<long>();
	if(!((x >= lo && x <= hi) || (zero_ok && x == 0)))
	{
		static char why[40];
		if(zero_ok) snprintf(why, sizeof why, "0 or %ld-%ld", lo, hi); else snprintf(why, sizeof why, "%ld to %ld", lo, hi);
		c.err[path] = why;            /* (ArduinoJson copies the text) */
		c.ok = false;
		return false;
	}
	out = x;
	return true;
}

bool get_colour(Ctx &c, JsonVariantConst v, const char *path, uint32_t &out)
{
	const char *t = v.is<const char *>() ? v.as<const char *>() : nullptr;
	if(!t || strlen(t) != 7 || t[0] != '#') { c.bad(path, "a colour like #4aff00"); return false; }
	for(int i = 1; i < 7; i++)
		if(!isxdigit((unsigned char)t[i])) { c.bad(path, "a colour like #4aff00"); return false; }
	out = (uint32_t)strtoul(t + 1, nullptr, 16);
	return true;
}

int pick(const char *t, const char *const *opts, int n)
{
	for(int i = 0; t && i < n; i++)
		if(strcmp(t, opts[i]) == 0)
			return i;
	return -1;
}

bool get_hhmm(Ctx &c, JsonVariantConst v, const char *path, uint16_t &out)
{
	const char *t = v.is<const char *>() ? v.as<const char *>() : nullptr;
	if(!t || strlen(t) != 5 || t[2] != ':' || !isdigit((unsigned char)t[0]) || !isdigit((unsigned char)t[1]) ||
	   !isdigit((unsigned char)t[3]) || !isdigit((unsigned char)t[4])) { c.bad(path, "a time like 22:00"); return false; }
	int h = (t[0] - '0') * 10 + t[1] - '0', m = (t[3] - '0') * 10 + t[4] - '0';
	if(h > 23 || m > 59) { c.bad(path, "a time like 22:00"); return false; }
	out = (uint16_t)(h * 60 + m);
	return true;
}

/* any key of `o` not in `known` is an error */
void unknown(Ctx &c, JsonObjectConst o, const char *section, const char *const *known, int n)
{
	for(JsonPairConst kv : o)
	{
		bool found = false;
		for(int i = 0; i < n && !found; i++)
			found = strcmp(kv.key().c_str(), known[i]) == 0;
		if(!found)
		{
			char p[64];
			snprintf(p, sizeof p, "%s%s%s", section, *section ? "." : "", kv.key().c_str());
			c.bad(p, "not a setting");
		}
	}
}
}  // namespace

bool settings_patch(orbs_settings_t &s, JsonObjectConst p, JsonObject errors)
{
	Ctx c{errors};
	long x;
	uint8_t b;
	static const char *const TOP[] = { "sets", "clock", "weather", "matrix", "rig", "screens", "formats", "rules", "warnings", "custom" };
	unknown(c, p, "", TOP, 10);
	for(JsonPairConst kv : p)
	{
		int ti = pick(kv.key().c_str(), TOP, 10);
		bool list = ti == 7 || ti == 9;      /* (rules and custom are lists) */
		if(ti >= 0 && !list && !kv.value().is<JsonObjectConst>())
			c.bad(kv.key().c_str(), "must be a group of settings");
		if(ti >= 0 && list && !kv.value().is<JsonArrayConst>())
			c.bad(kv.key().c_str(), "must be a list");
	}
	uint32_t known = settings_sets_known();
	if(JsonObjectConst o = p["sets"]; !o.isNull())
	{
		static const char *const K[] = { "order", "enabled", "cycle_s", "start", "per" };
		unknown(c, o, "sets", K, 5);
		if(!o["order"].isNull())
		{
			/* every set the page lists, once; the sets it doesn't list (not built yet) keep their order after them */
			JsonArrayConst a = o["order"];
			int nk = __builtin_popcount(known);
			uint8_t ord[N_SETS_SAVED];
			uint32_t seen = 0;
			bool good = !a.isNull() && (int)a.size() == nk;
			for(int i = 0; good && i < nk; i++)
			{
				int k = set_key_index(a[i].as<const char *>());
				good = k >= 0 && !(seen >> k & 1);
				if(good) { ord[i] = (uint8_t)k; seen |= 1u << k; }
			}
			if(good)
			{
				int n = nk;
				for(int i = 0; i < N_SETS_SAVED; i++)
					if(s.set_order[i] < N_SETS_SAVED && !(known >> s.set_order[i] & 1) && !(seen >> s.set_order[i] & 1))
					{
						ord[n++] = s.set_order[i];
						seen |= 1u << s.set_order[i];
					}
				for(int i = 0; i < N_SETS_SAVED && n < N_SETS_SAVED; i++)
					if(!(seen >> i & 1)) ord[n++] = (uint8_t)i;
				memcpy(s.set_order, ord, sizeof ord);
			}
			else c.bad("sets.order", "every set once");
		}
		if(JsonObjectConst e = o["enabled"]; !e.isNull())
			for(JsonPairConst kv : e)
			{
				int k = set_key_index(kv.key().c_str());
				char path[40];
				snprintf(path, sizeof path, "sets.enabled.%s", kv.key().c_str());
				if(k < 0) c.bad(path, "not a set");
				else if(get_bool(c, kv.value(), path, b))
					s.set_enabled = b ? (s.set_enabled | 1u << k) : (s.set_enabled & ~(1u << k));
			}
		else if(!o["enabled"].isNull())
			c.bad("sets.enabled", "every set");
		if(!o["cycle_s"].isNull() && get_int(c, o["cycle_s"], "sets.cycle_s", 5, 3600, x, true)) s.cycle_s = (uint16_t)x;
		if(JsonObjectConst per = o["per"]; !per.isNull())
			for(JsonPairConst kv : per)
			{
				const char *kn = kv.key().c_str();
				if(!kn) kn = "";                              /* (never null in practice; the compiler can't know) */
				int k = set_key_index(kn);
				char base[48];
				snprintf(base, sizeof base, "sets.per.%s", kn);
				if(k < 0) { c.bad(base, "not a set"); continue; }
				JsonObjectConst ps = kv.value();
				if(ps.isNull()) { c.bad(base, "must be a group of settings"); continue; }
				static const char *const PK[] = { "cycle_s", "brightness", "theme", "faces", "styles", "buttons" };
				unknown(c, ps, base, PK, 6);
				char path[72];
				snprintf(path, sizeof path, "%s.cycle_s", base);
				if(!ps["cycle_s"].isNull() && get_int(c, ps["cycle_s"], path, 5, 3600, x, true)) s.set_cycle_s[k] = (uint16_t)x;
				snprintf(path, sizeof path, "%s.brightness", base);
				if(!ps["brightness"].isNull() && get_int(c, ps["brightness"], path, 10, 255, x, true)) s.set_brightness[k] = (uint8_t)x;
				snprintf(path, sizeof path, "%s.theme", base);
				if(!ps["theme"].isNull()) { int i = pick(ps["theme"].as<const char *>(), THEMES, N_THEMES); if(i < 0) c.bad(path, "one of the themes"); else s.set_theme[k] = (uint8_t)i; }
				if(!ps["faces"].isNull())
				{
					/* a face per orb from the set's catalogue (0-31) or blank; the XL's weather sets: their five screens
					   in any order, each once */
					snprintf(path, sizeof path, "%s.faces", base);
					JsonArrayConst a = ps["faces"];
					uint8_t f5[5];
					bool good = !a.isNull() && a.size() == 5;
					uint8_t seen5 = 0;
					for(int q = 0; good && q < 5; q++)
					{
						if(a[q].is<const char *>() && strcmp(a[q].as<const char *>(), "blank") == 0 && !SET_IS_NATIVE5[k]) { f5[q] = 0xFF; continue; }
						long id = a[q].is<long>() && !a[q].is<bool>() ? a[q].as<long>() : -1;
						good = id >= 0 && id <= (SET_IS_NATIVE5[k] ? 4 : 31);
						if(good && SET_IS_NATIVE5[k]) { good = !(seen5 >> id & 1); seen5 |= (uint8_t)(1u << id); }
						f5[q] = (uint8_t)id;
					}
					if(good) memcpy(s.set_faces[k], f5, 5);
					else c.bad(path, SET_IS_NATIVE5[k] ? "its five screens, each once" : "five faces (0-31 or blank)");
				}
				if(!ps["styles"].isNull())
				{
					snprintf(path, sizeof path, "%s.styles", base);
					JsonArrayConst a = ps["styles"];
					uint8_t g5[5];
					bool good = !a.isNull() && a.size() == 5;
					for(int q = 0; good && q < 5; q++)
					{
						int i = pick(a[q].as<const char *>(), STYLES_G, 5);
						good = i >= 0;
						g5[q] = (uint8_t)i;
					}
					if(good) memcpy(s.set_style[k], g5, 5); else c.bad(path, "five of own, ring, needle, bar, number");
				}
				if(JsonObjectConst bt = ps["buttons"]; !bt.isNull())
				{
					snprintf(path, sizeof path, "%s.buttons", base);
					unknown(c, bt, path, BTN_NAMES, 3);
					for(int bn = 0; bn < 3; bn++)
					{
						JsonObjectConst bo = bt[BTN_NAMES[bn]];
						if(bo.isNull()) continue;
						char p2[96];
						snprintf(p2, sizeof p2, "%s.buttons.%s", base, BTN_NAMES[bn]);
						unknown(c, bo, p2, KIND_NAMES, 3);
						for(int kd = 0; kd < 3; kd++)
						{
							if(bo[KIND_NAMES[kd]].isNull()) continue;
							char p3[112];
							snprintf(p3, sizeof p3, "%s.%s", p2, KIND_NAMES[kd]);
							int a = action_code(bo[KIND_NAMES[kd]].as<const char *>());
							if(a < 0) c.bad(p3, "one of the actions");
							else s.buttons[k][bn * 3 + kd] = (uint8_t)a;
						}
					}
				}
				else if(!ps["buttons"].isNull())
					c.bad(path, "must be a group of settings");
			}
		if(!o["start"].isNull())
		{
			const char *t = o["start"].as<const char *>();
			int k = set_key_index(t);
			if(t && strcmp(t, "last") == 0) s.start_set = 0xFF;
			else if(k >= 0) s.start_set = (uint8_t)k;
			else c.bad("sets.start", "the last one or a set");
		}
	}
	if(JsonObjectConst o = p["clock"]; !o.isNull())
	{
		static const char *const K[] = { "style", "format", "ticks", "color", "shadow", "shadow_color", "nixie_color", "tz", "ntp" };
		unknown(c, o, "clock", K, 9);
		static const char *const STYLES[] = { "normal", "nixie" };
		if(!o["style"].isNull()) { int i = pick(o["style"].as<const char *>(), STYLES, 2); if(i < 0) c.bad("clock.style", "one of normal, nixie"); else s.clock_type = (uint8_t)i; }
		if(!o["format"].isNull()) { int i = pick(o["format"].as<const char *>(), FORMATS, 3); if(i < 0) c.bad("clock.format", "one of 24, 12, 12ampm"); else s.clock_format = (uint8_t)i; }
		if(!o["ticks"].isNull() && get_bool(c, o["ticks"], "clock.ticks", b)) s.show_second_ticks = b;
		if(!o["shadow"].isNull() && get_bool(c, o["shadow"], "clock.shadow", b)) s.clock_shadowing = b;
		uint32_t col;
		if(!o["color"].isNull() && get_colour(c, o["color"], "clock.color", col)) s.clock_color = col;
		if(!o["shadow_color"].isNull() && get_colour(c, o["shadow_color"], "clock.shadow_color", col)) s.clock_shadow_color = col;
		if(!o["nixie_color"].isNull() && get_colour(c, o["nixie_color"], "clock.nixie_color", col)) s.clock_nixie_color = col;
		if(!o["tz"].isNull())
		{
			const char *t = o["tz"].as<const char *>();
			if(!t || !orbs_tz_rule_for(t) || strlen(t) >= sizeof s.tz_name) c.bad("clock.tz", "one of the listed time zones");
			else { memset(s.tz_name, 0, sizeof s.tz_name); strcpy(s.tz_name, t); }
		}
		if(!o["ntp"].isNull())
		{
			const char *t = o["ntp"].as<const char *>();
			bool good = t && *t && strlen(t) <= 39;
			for(const char *q = t; good && *q; q++)
				good = isalnum((unsigned char)*q) || *q == '.' || *q == '-';
			if(!good) c.bad("clock.ntp", "a server name (letters, digits, dots, dashes)");
			else { memset(s.ntp_server, 0, sizeof s.ntp_server); strcpy(s.ntp_server, t); }
		}
	}
	if(JsonObjectConst o = p["weather"]; !o.isNull())
	{
		static const char *const K[] = { "enabled", "city", "lat", "lon", "units", "dark", "hl_cycle_s", "refresh_min" };
		unknown(c, o, "weather", K, 8);
		if(!o["enabled"].isNull() && get_bool(c, o["enabled"], "weather.enabled", b)) s.weather_enabled = b;
		if(!o["dark"].isNull() && get_bool(c, o["dark"], "weather.dark", b)) s.weather_dark = b;
		if(!o["city"].isNull())
		{
			const char *t = o["city"].as<const char *>();
			if(!t || !*t || strlen(t) > 23) c.bad("weather.city", "1 to 23 characters");
			else { memset(s.weather_city, 0, sizeof s.weather_city); strcpy(s.weather_city, t); }
		}
		for(int k = 0; k < 2; k++)
		{
			const char *key = k ? "lon" : "lat";
			JsonVariantConst v = o[key];
			if(v.isNull()) continue;
			double lim = k ? 180 : 90;
			char path[16];
			snprintf(path, sizeof path, "weather.%s", key);
			if(v.is<bool>() || !(v.is<double>() || v.is<long>())) { c.bad(path, "a number"); continue; }
			double d = v.as<double>();
			if(!(d >= -lim && d <= lim)) { c.bad(path, k ? "-180 to 180" : "-90 to 90"); continue; }
			char *dst = k ? s.weather_lon : s.weather_lat;
			snprintf(dst, 16, "%.6f", d);
			for(size_t n = strlen(dst); n > 1 && dst[n - 1] == '0' && dst[n - 2] != '.'; n--) dst[n - 1] = 0;   /* (no trailing zeros) */
		}
		static const char *const UNITS[] = { "metric", "imperial" };
		if(!o["units"].isNull()) { int i = pick(o["units"].as<const char *>(), UNITS, 2); if(i < 0) c.bad("weather.units", "one of imperial, metric"); else s.weather_units = (uint8_t)i; }
		if(!o["hl_cycle_s"].isNull() && get_int(c, o["hl_cycle_s"], "weather.hl_cycle_s", 3, 600, x, true)) s.weather_hl_cycle_s = (uint16_t)x;
		if(!o["refresh_min"].isNull() && get_int(c, o["refresh_min"], "weather.refresh_min", 5, 60, x)) s.weather_refresh_min = (uint8_t)x;
	}
	if(JsonObjectConst o = p["matrix"]; !o.isNull())
	{
		static const char *const K[] = { "enabled", "big_font", "color", "head_color", "line_min", "line_max", "speed_min", "speed_max", "frame_ms" };
		unknown(c, o, "matrix", K, 9);
		if(!o["enabled"].isNull() && get_bool(c, o["enabled"], "matrix.enabled", b))
			s.set_enabled = b ? (s.set_enabled | 1u << 4) : (s.set_enabled & ~(1u << 4));
		if(!o["big_font"].isNull() && get_bool(c, o["big_font"], "matrix.big_font", b)) s.matrix_big_font = b;
		uint32_t col;
		if(!o["color"].isNull() && get_colour(c, o["color"], "matrix.color", col)) s.matrix_color = col;
		if(!o["head_color"].isNull() && get_colour(c, o["head_color"], "matrix.head_color", col)) s.matrix_head_color = col;
		if(!o["line_min"].isNull() && get_int(c, o["line_min"], "matrix.line_min", 1, 30, x)) s.matrix_line_min = (uint8_t)x;
		if(!o["line_max"].isNull() && get_int(c, o["line_max"], "matrix.line_max", 1, 40, x)) s.matrix_line_max = (uint8_t)x;
		if(!o["speed_min"].isNull() && get_int(c, o["speed_min"], "matrix.speed_min", 1, 30, x)) s.matrix_speed_min = (uint8_t)x;
		if(!o["speed_max"].isNull() && get_int(c, o["speed_max"], "matrix.speed_max", 1, 40, x)) s.matrix_speed_max = (uint8_t)x;
		if(!o["frame_ms"].isNull() && get_int(c, o["frame_ms"], "matrix.frame_ms", 20, 1000, x)) s.matrix_frame_ms = (uint16_t)x;
	}
	if(JsonObjectConst o = p["rig"]; !o.isNull())
	{
		static const char *const K[] = { "rig_hz", "rig_middle", "cams_page1", "cams_page2", "cam_quality", "cam_fps", "viz_color",
		                                 "kwh_price" };
		unknown(c, o, "rig", K, 8);
		if(!o["kwh_price"].isNull())
		{
			/* $ per kWh, 0 (not set) to 2.000, to a tenth of a cent */
			JsonVariantConst v = o["kwh_price"];
			double d = v.is<double>() || v.is<long>() ? v.as<double>() : -1;
			if((!v.is<double>() && !v.is<long>()) || v.is<bool>() || d < 0 || d > 2.0) c.bad("rig.kwh_price", "0 to 2.000 (dollars a kWh)");
			else s.kwh_price_m = (uint16_t)(d * 1000.0 + 0.5);
		}
		if(!o["rig_hz"].isNull())
		{
			if(get_int(c, o["rig_hz"], "rig.rig_hz", 1, 4, x) && (x == 1 || x == 2 || x == 4)) s.rig_hz = (uint8_t)x;
			else if(c.err["rig.rig_hz"].isNull()) c.bad("rig.rig_hz", "one of 1, 2, 4");
		}
		if(!o["rig_middle"].isNull()) { int i = pick(o["rig_middle"].as<const char *>(), MIDDLES, 4); if(i < 0) c.bad("rig.rig_middle", "one of memory, health, power, cooling"); else s.rig_middle = (uint8_t)i; }
		for(int pg = 0; pg < 2; pg++)
		{
			const char *key = pg ? "cams_page2" : "cams_page1";
			if(o[key].isNull()) continue;
			JsonArrayConst a = o[key];
			uint8_t v[5];
			bool good = !a.isNull() && a.size() == 5;
			for(int k = 0; good && k < 5; k++)
			{
				int idx = -1;
				for(int j = 0; j < N_CAMS_ALL; j++)
					if(a[k].is<const char *>() && strcmp(a[k].as<const char *>(), CAM_IDS[j]) == 0) idx = j;
				good = idx >= 0;
				v[k] = (uint8_t)idx;
			}
			char path[24];
			snprintf(path, sizeof path, "rig.%s", key);
			if(good) memcpy(s.cams_page[pg], v, 5); else c.bad(path, "five cameras");
		}
		if(!o["cam_quality"].isNull() && get_int(c, o["cam_quality"], "rig.cam_quality", 60, 95, x)) s.cam_quality = (uint8_t)x;
		if(!o["cam_fps"].isNull() && get_int(c, o["cam_fps"], "rig.cam_fps", 1, 15, x)) s.cam_fps = (uint8_t)x;
		if(!o["viz_color"].isNull())
		{
			const char *t = o["viz_color"].as<const char *>();
			uint32_t col;
			if(t && strcmp(t, "album") == 0) s.viz_color = VIZ_ALBUM;
			else if(t && strcmp(t, "rainbow") == 0) s.viz_color = VIZ_RAINBOW;
			else if(t && t[0] == '#' && get_colour(c, o["viz_color"], "rig.viz_color", col)) s.viz_color = col;
			else if(c.err["rig.viz_color"].isNull()) c.bad("rig.viz_color", "album, rainbow or a colour");
		}
	}
	if(JsonObjectConst o = p["screens"]; !o.isNull())
	{
		static const char *const K[] = { "brightness", "night", "rotation", "orbs", "link" };
		unknown(c, o, "screens", K, 5);
		if(!o["link"].isNull()) { int i = pick(o["link"].as<const char *>(), LINKS, 3); if(i < 0) c.bad("screens.link", "one of normal, fast, fastest"); else s.link_speed = (uint8_t)i; }
		if(!o["orbs"].isNull())
		{
			JsonArrayConst a = o["orbs"];
			bool good = !a.isNull() && a.size() == 5;
			uint8_t br5[5], gn[5][3];
			memcpy(br5, s.orb_bright, 5);
			memcpy(gn, s.orb_gain, sizeof gn);
			for(int q = 0; good && q < 5; q++)
			{
				JsonObjectConst ob = a[q];
				good = !ob.isNull();
				if(!good) break;
				char path[40];
				snprintf(path, sizeof path, "screens.orbs.%d", q);
				static const char *const OK2[] = { "brightness", "gain" };
				unknown(c, ob, path, OK2, 2);
				snprintf(path, sizeof path, "screens.orbs.%d.brightness", q);
				if(!ob["brightness"].isNull() && get_int(c, ob["brightness"], path, 50, 100, x)) br5[q] = (uint8_t)x;
				if(!ob["gain"].isNull())
				{
					JsonArrayConst g = ob["gain"];
					snprintf(path, sizeof path, "screens.orbs.%d.gain", q);
					bool gg = !g.isNull() && g.size() == 3;
					for(int ch = 0; gg && ch < 3; ch++)
					{
						long v = g[ch].is<long>() && !g[ch].is<bool>() ? g[ch].as<long>() : -1;
						gg = v >= 50 && v <= 100;
						gn[q][ch] = (uint8_t)v;
					}
					if(!gg) c.bad(path, "three of 50-100 (red, green, blue percent)");
				}
			}
			if(good) { memcpy(s.orb_bright, br5, 5); memcpy(s.orb_gain, gn, sizeof gn); }
			else c.bad("screens.orbs", "five orbs");
		}
		if(!o["brightness"].isNull() && get_int(c, o["brightness"], "screens.brightness", 10, 255, x)) s.brightness = (uint8_t)x;
		if(JsonObjectConst n = o["night"]; !n.isNull())
		{
			static const char *const NK[] = { "mode", "start", "end", "brightness" };
			unknown(c, n, "screens.night", NK, 4);
			if(!n["mode"].isNull()) { int i = pick(n["mode"].as<const char *>(), NIGHTS, 3); if(i < 0) c.bad("screens.night.mode", "one of off, dim, dark"); else s.night_mode = (uint8_t)i; }
			uint16_t t;
			if(!n["start"].isNull() && get_hhmm(c, n["start"], "screens.night.start", t)) s.night_start_min = t;
			if(!n["end"].isNull() && get_hhmm(c, n["end"], "screens.night.end", t)) s.night_end_min = t;
			if(!n["brightness"].isNull() && get_int(c, n["brightness"], "screens.night.brightness", 5, 255, x)) s.night_brightness = (uint8_t)x;
		}
		else if(!o["night"].isNull())
			c.bad("screens.night", "must be a group of settings");
		if(!o["rotation"].isNull())
		{
			JsonArrayConst a = o["rotation"];
			uint8_t r[5];
			bool good = !a.isNull() && a.size() == 5;
			for(int k = 0; good && k < 5; k++)
			{
				long d = a[k].is<long>() ? a[k].as<long>() : -1;
				good = !a[k].is<bool>() && (d == 0 || d == 90 || d == 180 || d == 270);
				r[k] = (uint8_t)(d / 90);
			}
			if(good) memcpy(s.rotation, r, 5); else c.bad("screens.rotation", "five of 0, 90, 180, 270");
		}
	}
	if(JsonObjectConst o = p["formats"]; !o.isNull())
	{
		static const char *const K[] = { "temp", "date", "decimals" };
		unknown(c, o, "formats", K, 3);
		if(!o["temp"].isNull()) { int i = pick(o["temp"].as<const char *>(), TEMPS, 2); if(i < 0) c.bad("formats.temp", "C or F"); else s.temp_unit = (uint8_t)i; }
		if(!o["date"].isNull()) { int i = pick(o["date"].as<const char *>(), DATES, 3); if(i < 0) c.bad("formats.date", "one of mdy, dmy, ymd"); else s.date_order = (uint8_t)i; }
		if(!o["decimals"].isNull()) { int i = pick(o["decimals"].as<const char *>(), DECIMALS, 3); if(i < 0) c.bad("formats.decimals", "one of none, auto, more"); else s.decimals = (uint8_t)i; }
	}
	if(!p["rules"].isNull())
	{
		/* the whole list at once (up to 12) */
		JsonArrayConst a = p["rules"];
		orbs_rule_t nr[N_RULES];
		memset(nr, 0, sizeof nr);
		bool good = !a.isNull() && a.size() <= N_RULES;
		if(!good) c.bad("rules", "up to 12 rules");
		for(int i = 0; good && i < (int)a.size(); i++)
		{
			JsonObjectConst ro = a[i];
			char base[24];
			snprintf(base, sizeof base, "rules.%d", i);
			if(ro.isNull()) { c.bad(base, "a rule"); good = false; break; }
			static const char *const RK[] = { "kind", "set", "days", "start", "end", "event", "secs" };
			unknown(c, ro, base, RK, 7);
			char path[40];
			static const char *const KINDS[] = { "time", "event" };
			int kind = pick(ro["kind"].as<const char *>(), KINDS, 2);
			snprintf(path, sizeof path, "%s.kind", base);
			if(kind < 0) { c.bad(path, "time or event"); good = false; break; }
			nr[i].kind = kind == 0 ? RULE_TIME : RULE_EVENT;
			snprintf(path, sizeof path, "%s.set", base);
			const char *st = ro["set"].as<const char *>();
			int sk = set_key_index(st);
			if(st && strcmp(st, "dark") == 0) nr[i].set = 0xFE;
			else if(sk >= 0) nr[i].set = (uint8_t)sk;
			else { c.bad(path, "a set or dark"); good = false; break; }
			if(kind == 0)
			{
				JsonArrayConst d = ro["days"];
				snprintf(path, sizeof path, "%s.days", base);
				uint8_t days = 0;
				bool dg = !d.isNull() && d.size() >= 1 && d.size() <= 7;
				for(int q = 0; dg && q < (int)d.size(); q++)
				{
					int di = pick(d[q].as<const char *>(), DAYS, 7);
					dg = di >= 0;
					if(dg) days |= (uint8_t)(1u << di);
				}
				if(!dg) { c.bad(path, "days like sun, mon"); good = false; break; }
				nr[i].days = days;
				snprintf(path, sizeof path, "%s.start", base);
				if(!get_hhmm(c, ro["start"], path, nr[i].start_min)) { good = false; break; }
				snprintf(path, sizeof path, "%s.end", base);
				if(!get_hhmm(c, ro["end"], path, nr[i].end_min)) { good = false; break; }
			}
			else
			{
				snprintf(path, sizeof path, "%s.event", base);
				int ev = pick(ro["event"].as<const char *>(), EVENTS + 1, 5);
				if(ev < 0) { c.bad(path, "one of music, game, rig_sleep, person_door, print_done"); good = false; break; }
				nr[i].event = (uint8_t)(ev + 1);
				snprintf(path, sizeof path, "%s.secs", base);
				if(!ro["secs"].isNull()) { if(!get_int(c, ro["secs"], path, 5, 3600, x, true)) { good = false; break; } nr[i].secs = (uint16_t)x; }
			}
		}
		if(good) memcpy(s.rules, nr, sizeof nr);
	}
	if(JsonObjectConst o = p["warnings"]; !o.isNull())
	{
		unknown(c, o, "warnings", WARN_KEYS, N_WARN);
		for(int i = 0; i < N_WARN; i++)
		{
			JsonObjectConst wo = o[WARN_KEYS[i]];
			if(wo.isNull()) continue;
			char base[24], path[40];
			snprintf(base, sizeof base, "warnings.%s", WARN_KEYS[i]);
			static const char *const WK[] = { "on", "limit", "how" };
			unknown(c, wo, base, WK, 3);
			snprintf(path, sizeof path, "%s.on", base);
			if(!wo["on"].isNull() && get_bool(c, wo["on"], path, b)) s.warn[i].on = b;
			snprintf(path, sizeof path, "%s.limit", base);
			if(!wo["limit"].isNull() && get_int(c, wo["limit"], path, 30, 110, x)) s.warn[i].limit_c = (uint8_t)x;
			snprintf(path, sizeof path, "%s.how", base);
			static const char *const HOW[] = { "colour", "pulse" };
			if(!wo["how"].isNull()) { int h = pick(wo["how"].as<const char *>(), HOW, 2); if(h < 0) c.bad(path, "colour or pulse"); else s.warn[i].how = (uint8_t)h; }
		}
	}
	if(!p["custom"].isNull())
	{
		/* own faces: by position, any of the eight (an entry may change only some fields) */
		JsonArrayConst a = p["custom"];
		if(a.isNull() || a.size() > N_CUSTOM) c.bad("custom", "up to 8 faces");
		else
			for(int i = 0; i < (int)a.size(); i++)
			{
				JsonObjectConst co = a[i];
				char base[24], path[40];
				snprintf(base, sizeof base, "custom.%d", i);
				if(co.isNull()) continue;          /* (null: that one unchanged) */
				static const char *const CK[] = { "kind", "title", "text", "color", "bg", "size", "target" };
				unknown(c, co, base, CK, 7);
				orbs_custom_t &cf = s.custom[i];
				snprintf(path, sizeof path, "%s.kind", base);
				if(!co["kind"].isNull()) { int k = pick(co["kind"].as<const char *>(), CUSTOM_KINDS, 3); if(k < 0) c.bad(path, "none, message or countdown"); else cf.kind = (uint8_t)k; }
				for(int t = 0; t < 2; t++)
				{
					const char *key = t ? "text" : "title";
					size_t cap = t ? sizeof cf.text : sizeof cf.title;
					if(co[key].isNull()) continue;
					const char *v = co[key].as<const char *>();
					snprintf(path, sizeof path, "%s.%s", base, key);
					if(!v || strlen(v) >= cap) { static char why[24]; snprintf(why, sizeof why, "up to %u characters", (unsigned)(cap - 1)); c.bad(path, why); continue; }
					char *dst = t ? cf.text : cf.title;
					memset(dst, 0, cap);
					strcpy(dst, v);
				}
				uint32_t col;
				snprintf(path, sizeof path, "%s.color", base);
				if(!co["color"].isNull() && get_colour(c, co["color"], path, col)) cf.color = col;
				snprintf(path, sizeof path, "%s.bg", base);
				if(!co["bg"].isNull() && get_colour(c, co["bg"], path, col)) cf.bg = col;
				snprintf(path, sizeof path, "%s.size", base);
				if(!co["size"].isNull()) { int k = pick(co["size"].as<const char *>(), SIZES, 4); if(k < 0) c.bad(path, "auto, small, medium or big"); else cf.size = (uint8_t)k; }
				snprintf(path, sizeof path, "%s.target", base);
				if(!co["target"].isNull())
				{
					if(co["target"].is<bool>() || !co["target"].is<long long>()) c.bad(path, "a date and time");
					else { long long t = co["target"].as<long long>(); if(t < 0 || t > 4102444800LL) c.bad(path, "a date before 2100"); else cf.target = (uint32_t)t; }
				}
			}
	}
	/* rules across fields, on the result (as the stand-in checks the whole settings) */
	if(c.err["matrix.line_min"].isNull() && c.err["matrix.line_max"].isNull() && s.matrix_line_max < s.matrix_line_min)
		c.bad("matrix.line_max", "at least the shortest line");
	if(c.err["matrix.speed_min"].isNull() && c.err["matrix.speed_max"].isNull() && s.matrix_speed_max < s.matrix_speed_min)
		c.bad("matrix.speed_max", "at least the slowest");
	return c.ok;
}
