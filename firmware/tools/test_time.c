/* Checks orbs_time.c's local time against the PC's own time zone database (glibc + /usr/share/zoneinfo) for every
   zone in the XL's table, every 7 minutes 13 s from 2026-11-03 to 2036 (and every second around each change).
   Run: tools/test_time.sh */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../src/orbs_time.h"
static const char *names[] = { "America/New_York", "America/Detroit", "America/Chicago", "America/Denver", "America/Phoenix",
	"America/Los_Angeles", "America/Anchorage", "Pacific/Honolulu", "America/Toronto", "America/Vancouver",
	"Europe/London", "Europe/Berlin", "Europe/Paris", "UTC" };
int main(void)
{
	long checked = 0, bad = 0;
	for(size_t z = 0; z < sizeof names / sizeof names[0]; z++)
	{
		orbs_tz_t tz;
		if(!orbs_tz_parse(orbs_tz_rule_for(names[z]), &tz)) { printf("FAIL parse %s\n", names[z]); bad++; continue; }
		setenv("TZ", names[z], 1);
		tzset();
		int last_dst = -1;
		for(int64_t t = 1793664000; t < 2082758400; t += 433)   /* 2026-11-03 .. 2036-01-01 (after BC's change) */
		{
			int64_t lo = t, hi = t;
			struct tm g;
			time_t tt = (time_t)t;
			localtime_r(&tt, &g);
			if(last_dst >= 0 && g.tm_isdst != last_dst) { lo = t - 900; hi = t; }   /* around a change: every second */
			last_dst = g.tm_isdst;
			for(int64_t u = lo; u <= hi; u++)
			{
				tt = (time_t)u;
				localtime_r(&tt, &g);
				orbs_tm_t o;
				orbs_tz_local(&tz, u, &o);
				checked++;
				if(o.year != g.tm_year + 1900 || o.mon != g.tm_mon + 1 || o.mday != g.tm_mday || o.hour != g.tm_hour ||
				   o.min != g.tm_min || o.sec != g.tm_sec || o.wday != g.tm_wday || (int)o.dst != (g.tm_isdst > 0) ||
				   o.utc_offset_s != g.tm_gmtoff)
				{
					if(bad < 10)
						printf("FAIL %s %lld: ours %04d-%02d-%02d %02d:%02d:%02d dst%d, glibc %04d-%02d-%02d %02d:%02d:%02d dst%d\n",
						       names[z], (long long)u, o.year, o.mon, o.mday, o.hour, o.min, o.sec, o.dst, g.tm_year + 1900,
						       g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec, g.tm_isdst);
					bad++;
				}
			}
		}
	}
	printf("%ld times checked in %zu zones, %ld wrong: %s\n", checked, sizeof names / sizeof names[0], bad, bad ? "FAIL" : "ALL PASS");
	return bad != 0;
}
