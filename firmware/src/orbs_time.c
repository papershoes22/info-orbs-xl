#include "orbs_time.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* the zones the settings page offers: each one's rule from the last line of its zoneinfo file (tzdata 2026e on the
   rig; British Columbia stays on UTC-7 since 2026-11-01). tools/test_time.sh checks them against that database. */
static const char *const zones[][2] = {
	{ "America/New_York", "EST5EDT,M3.2.0,M11.1.0" }, { "America/Detroit", "EST5EDT,M3.2.0,M11.1.0" },
	{ "America/Chicago", "CST6CDT,M3.2.0,M11.1.0" }, { "America/Denver", "MST7MDT,M3.2.0,M11.1.0" },
	{ "America/Phoenix", "MST7" }, { "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0" },
	{ "America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0" }, { "Pacific/Honolulu", "HST10" },
	{ "America/Toronto", "EST5EDT,M3.2.0,M11.1.0" }, { "America/Vancouver", "MST7" },
	{ "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0" }, { "Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3" },
	{ "Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3" }, { "UTC", "UTC0" },
};

const char *orbs_tz_rule_for(const char *iana)
{
	for(size_t i = 0; i < sizeof zones / sizeof zones[0]; i++)
		if(strcmp(zones[i][0], iana) == 0)
			return zones[i][1];
	return NULL;
}

int orbs_tz_count(void) { return (int)(sizeof zones / sizeof zones[0]); }
const char *orbs_tz_name(int i) { return i >= 0 && i < orbs_tz_count() ? zones[i][0] : ""; }

/* [+|-]hh[:mm[:ss]] */
static const char *parse_hms(const char *p, int32_t *out)
{
	int sign = 1;
	if(*p == '+' || *p == '-')
		sign = *p++ == '-' ? -1 : 1;
	if(!isdigit((unsigned char)*p))
		return NULL;
	int32_t v = strtol(p, (char **)&p, 10) * 3600;
	if(*p == ':')
	{
		v += strtol(p + 1, (char **)&p, 10) * 60;
		if(*p == ':')
			v += strtol(p + 1, (char **)&p, 10);
	}
	*out = sign * v;
	return p;
}

static const char *parse_name(const char *p)
{
	if(*p == '<')
	{
		while(*p && *p != '>')
			p++;
		return *p ? p + 1 : NULL;
	}
	const char *s = p;
	while(isalpha((unsigned char)*p))
		p++;
	return p - s >= 3 ? p : NULL;
}

/* Mm.w.d[/time] */
static const char *parse_rule(const char *p, int *m, int *w, int *d, int *secs)
{
	if(*p != 'M')
		return NULL;
	*m = strtol(p + 1, (char **)&p, 10);
	if(*p != '.')
		return NULL;
	*w = strtol(p + 1, (char **)&p, 10);
	if(*p != '.')
		return NULL;
	*d = strtol(p + 1, (char **)&p, 10);
	*secs = 7200;
	if(*p == '/')
	{
		int32_t t;
		p = parse_hms(p + 1, &t);
		if(!p)
			return NULL;
		*secs = t;
	}
	return (*m >= 1 && *m <= 12 && *w >= 1 && *w <= 5 && *d >= 0 && *d <= 6) ? p : NULL;
}

bool orbs_tz_parse(const char *rule, orbs_tz_t *tz)
{
	memset(tz, 0, sizeof *tz);
	const char *p = parse_name(rule);
	if(!p || !(p = parse_hms(p, &tz->std_off)))
		return false;
	if(!*p)
		return true;
	if(!(p = parse_name(p)))
		return false;
	tz->has_dst = true;
	tz->dst_off = tz->std_off - 3600;
	if(*p && *p != ',')
		if(!(p = parse_hms(p, &tz->dst_off)))
			return false;
	if(*p != ',' || !(p = parse_rule(p + 1, &tz->sm, &tz->sw, &tz->sd, &tz->ss)))
		return false;
	if(*p != ',' || !(p = parse_rule(p + 1, &tz->em, &tz->ew, &tz->ed, &tz->es)))
		return false;
	return *p == 0;
}

/* days since 1970-01-01 for a civil date (Howard Hinnant's days_from_civil) */
static int64_t days_from_civil(int y, int m, int d)
{
	y -= m <= 2;
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	int64_t yoe = y - era * 400;
	int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
	z += 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	int64_t doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t yy = yoe + era * 400;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yy + (*m <= 2));
}

static int month_days(int y, int m)
{
	static const int md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if(m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
		return 29;
	return md[m - 1];
}

/* local seconds since 1970 of rule day m.w.d at secs, in year y */
static int64_t rule_local(int y, int m, int w, int d, int secs)
{
	int64_t first = days_from_civil(y, m, 1);
	int wd_first = (int)((first % 7 + 7 + 4) % 7);       /* 1970-01-01 was a Thursday (4) */
	int day = 1 + (d - wd_first + 7) % 7 + 7 * (w - 1);
	while(day > month_days(y, m))
		day -= 7;
	return (days_from_civil(y, m, day)) * 86400 + secs;
}

void orbs_tz_local(const orbs_tz_t *tz, int64_t utc, orbs_tm_t *o)
{
	int32_t off = tz->std_off;
	bool dst = false;
	if(tz->has_dst)
	{
		int y, m, d;
		civil_from_days((utc - tz->std_off) / 86400 - ((utc - tz->std_off) % 86400 < 0), &y, &m, &d);
		int64_t start = rule_local(y, tz->sm, tz->sw, tz->sd, tz->ss) + tz->std_off;   /* in standard time */
		int64_t end = rule_local(y, tz->em, tz->ew, tz->ed, tz->es) + tz->dst_off;     /* in summer time */
		if(start < end)
			dst = utc >= start && utc < end;
		else
			dst = !(utc >= end && utc < start);          /* (southern hemisphere) */
		if(dst)
			off = tz->dst_off;
	}
	int64_t local = utc - off;
	int64_t days = local / 86400, rem = local % 86400;
	if(rem < 0)
	{
		rem += 86400;
		days--;
	}
	civil_from_days(days, &o->year, &o->mon, &o->mday);
	o->hour = (int)(rem / 3600);
	o->min = (int)(rem % 3600 / 60);
	o->sec = (int)(rem % 60);
	o->wday = (int)((days % 7 + 7 + 4) % 7);
	o->dst = dst;
	o->utc_offset_s = -off;
}

/* ---- network time ---- */
#ifdef ORBS_HOST
#include <sys/time.h>
static uint64_t now_us(void) { struct timeval tv; gettimeofday(&tv, 0); return (uint64_t)tv.tv_sec * 1000000u + tv.tv_usec; }
#else
#include "pico/time.h"
static uint64_t now_us(void) { return time_us_64(); }
#endif

static volatile int64_t sync_epoch_us;    /* epoch (us) at sync_mono_us */
static volatile uint64_t sync_mono_us;
static volatile uint32_t syncs;

void orbs_time_set(uint32_t epoch_s, uint32_t us)
{
	sync_epoch_us = (int64_t)epoch_s * 1000000 + us;
	sync_mono_us = now_us();
	syncs++;
}

bool orbs_time_synced(void) { return syncs > 0; }
uint32_t orbs_time_syncs(void) { return syncs; }
int64_t orbs_time_last_sync(void) { return syncs ? sync_epoch_us / 1000000 : 0; }

int64_t orbs_time_now(void)
{
	if(!syncs)
		return 0;
	return (sync_epoch_us + (int64_t)(now_us() - sync_mono_us)) / 1000000;
}
