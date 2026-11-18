/* The orbs' clock (2026-11-16): network time from pool.ntp.org (the original's NTP server), turned into local time
   with the time zone's own daylight-saving rule (a POSIX TZ rule, worked out on the XL: nothing asked of a web
   service, unlike the original's timeapi.io lookup). Plain C, tested on the PC against its own time zone database
   (tools/test_time.c). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int year, mon, mday, hour, min, sec, wday;   /* mon 1-12, wday 0 = Sunday */
	bool dst;
	int32_t utc_offset_s;                         /* local = UTC + this */
} orbs_tm_t;

typedef struct {
	int32_t std_off, dst_off;                     /* seconds WEST of UTC, as POSIX writes them (EST5 = 18000) */
	bool has_dst;
	int sm, sw, sd, ss, em, ew, ed, es;          /* start/end: month, week (5 = last), weekday, seconds after 00:00 */
} orbs_tz_t;

/* the POSIX rule for an IANA zone name ("America/New_York"), or NULL if not in the XL's table */
const char *orbs_tz_rule_for(const char *iana);
int orbs_tz_count(void);                  /* the zones the XL knows (for the settings page) */
const char *orbs_tz_name(int i);
bool orbs_tz_parse(const char *rule, orbs_tz_t *tz);            /* "EST5EDT,M3.2.0,M11.1.0" */
void orbs_tz_local(const orbs_tz_t *tz, int64_t utc, orbs_tm_t *out);

/* network time */
void orbs_time_set(uint32_t epoch_s, uint32_t us);   /* from SNTP */
bool orbs_time_synced(void);
int64_t orbs_time_now(void);                          /* seconds since 1970 UTC (0 if never synced) */
uint32_t orbs_time_syncs(void);
int64_t orbs_time_last_sync(void);

#ifdef __cplusplus
}
#endif
