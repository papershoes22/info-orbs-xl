#include "net.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include "lwip/apps/mdns.h"
#include "hotspot.h"
#include "lwip/netif.h"
#include "settings.h"
#include "orbs_time.h"

static char mac_s[18] = "unknown", ip_s[16] = "-", state_s[48] = "starting";
static bool started, sntp_on, joining;
static absolute_time_t next_try;
static uint32_t joins;
static int last_link = -99;
static absolute_time_t sntp_started;
/* the setup hotspot (hotspot.h): opens when there's no Wi-Fi to join: at once with none set, after 2 minutes if it has
   never joined since start-up (a new place, a wrong password), after 10 if it was joined and dropped (a router
   restarting at night shouldn't bring it up); open 10 minutes, then the home Wi-Fi is tried again */
static bool ever_joined;
static uint32_t down_since, hotspot_since;
#define HOTSPOT_AFTER_NEVER_MS (2u * 60000u)
#define HOTSPOT_AFTER_DROP_MS (10u * 60000u)
#define HOTSPOT_OPEN_MS (10u * 60000u)
static uint32_t sntp_restarts;

/* the time client again from the start (2026-11-16: twice, right after a firmware load, no time for 2 minutes and for
   70 s with Wi-Fi up and ping answering; the cause isn't proven). Since then it asks three pool names in turn
   (the setting first, then 1. and 2.pool.ntp.org: the same pool, other addresses), 3 s each. */
static void sntp_restart(const char *why)
{
	cyw43_arch_lwip_begin();
	if(sntp_on)
		sntp_stop();
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, settings.ntp_server[0] ? settings.ntp_server : "pool.ntp.org");
	sntp_setservername(1, "1.pool.ntp.org");
	sntp_setservername(2, "2.pool.ntp.org");
	sntp_init();
	cyw43_arch_lwip_end();
	sntp_on = true;
	sntp_started = get_absolute_time();
	if(why)
	{
		sntp_restarts++;
		printf("EV TIME %s: asking again\n", why);
	}
}

uint32_t net_sntp_restarts(void) { return sntp_restarts; }

/* the settings page changed the time server: asked again now */
void net_reconnect_time(void)
{
	if(sntp_on)
		sntp_restart("the time server changed");
}

/* for "?": each time server's address and whether its last 8 asks were answered (bits, newest lowest), and the DNS
   server the router gave */
const char *net_ntp_info(void)
{
	static char out[160];
	int n = 0;
	cyw43_arch_lwip_begin();
	for(int i = 0; i < SNTP_MAX_SERVERS && n < (int)sizeof out - 40; i++)
	{
		const ip_addr_t *a = sntp_getserver((u8_t)i);
		n += snprintf(out + n, sizeof out - n, "%s%s/%02x", i ? "," : "", a && !ip_addr_isany(a) ? ipaddr_ntoa(a) : "-",
		              sntp_getreachability((u8_t)i));
	}
	const ip_addr_t *d = dns_getserver(0);
	snprintf(out + n, sizeof out - n, " dns=%s", d && !ip_addr_isany(d) ? ipaddr_ntoa(d) : "-");
	cyw43_arch_lwip_end();
	return out;
}

void orbs_sntp_set(unsigned long sec, unsigned long us)
{
	orbs_time_set((uint32_t)sec, (uint32_t)us);
}

void net_init(void)
{
	if(cyw43_arch_init_with_country(CYW43_COUNTRY_USA) != 0)
	{
		snprintf(state_s, sizeof state_s, "failed: the Wi-Fi chip didn't start");
		return;
	}
	cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);        /* (brings the chip up; its LED stays off) */
	cyw43_arch_enable_sta_mode();
	cyw43_arch_lwip_begin();
	netif_set_hostname(netif_default, "info-orbs");
	cyw43_arch_lwip_end();
	const uint8_t *m = cyw43_state.mac;
	snprintf(mac_s, sizeof mac_s, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
	started = true;
	next_try = get_absolute_time();
}

void net_reconnect(void)
{
	if(!started)
		return;
	cyw43_arch_lwip_begin();
	cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
	cyw43_arch_lwip_end();
	joining = false;
	last_link = -99;
	next_try = get_absolute_time();
}

static const char *link_name(int s)
{
	switch(s)
	{
	case CYW43_LINK_DOWN: return "down";
	case CYW43_LINK_JOIN: return "joined, waiting for an address";
	case CYW43_LINK_NOIP: return "joined, no address";
	case CYW43_LINK_UP: return "up";
	case CYW43_LINK_FAIL: return "failed: connection lost or refused";
	case CYW43_LINK_NONET: return "failed: network not found";
	case CYW43_LINK_BADAUTH: return "failed: wrong password";
	default: return "unknown";
	}
}

static void open_hotspot(uint32_t now)
{
	hotspot_since = now;
	/* off the home Wi-Fi from here: said, and nothing tries the internet meanwhile (2026-11-17: opened by the test
	   switch while joined, the link stayed "up", so the weather and the Frigate check ran and failed,
	   and the Frigate "fail" showed as a fault) */
	if(last_link == CYW43_LINK_UP)
		printf("EV WIFI off while the setup hotspot is open\n");
	last_link = -99;
	snprintf(ip_s, sizeof ip_s, "-");
	hotspot_start();
}

/* the Wi-Fi page saved a network: the hotspot closes, that network is tried */
void net_wifi_saved(void)
{
	if(hotspot_busy())
		hotspot_stop();
	ever_joined = false;
	down_since = to_ms_since_boot(get_absolute_time());
	net_reconnect();
}

bool net_hotspot(void) { return hotspot_on(); }

/* TEST ("H" over USB): open the hotspot now / close it and rejoin, the saved Wi-Fi untouched */
/* (hotspot.c) the home Wi-Fi is about to be turned off: the name's record there removed cleanly first (its memory, its
   timers and the multicast group), so it's added afresh when the Wi-Fi is back */
static bool mdns_started;                  /* (mdns_resp_init done: only after the first Wi-Fi connection) */

void net_sta_going_off(void)
{
	/* before mdns_resp_init its netif slot number isn't set: asking mdns_resp_netif_active then reads another part's
	   data (the DHCP client's) and removing it frees that: the XL panicked "mem_free: sanity check alignment" when the
	   hotspot opened with no Wi-Fi joined since power-on (2026-11-17, after its settings were lost) */
	if(!mdns_started)
		return;
	struct netif *sta = &cyw43_state.netif[CYW43_ITF_STA];
	cyw43_arch_lwip_begin();
	if(mdns_resp_netif_active(sta))
		mdns_resp_remove_netif(sta);
	cyw43_arch_lwip_end();
}

void net_hotspot_test_open(void)
{
	if(!hotspot_busy())
		open_hotspot(to_ms_since_boot(get_absolute_time()));
}

void net_wifi_saved_test_close(void)
{
	if(hotspot_busy())
		hotspot_stop();
	down_since = to_ms_since_boot(get_absolute_time());
	net_reconnect();
}

void net_poll(void)
{
	if(!started)
		return;
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if(hotspot_busy())
	{
		hotspot_poll();
		snprintf(state_s, sizeof state_s, hotspot_on() ? "hotspot open (setup)" : "scanning for the hotspot");
		if(hotspot_on() && settings.wifi_ssid[0] && now - hotspot_since > HOTSPOT_OPEN_MS)
		{
			hotspot_stop();                    /* (10 minutes and nobody set it up: the home Wi-Fi again) */
			down_since = now;
			last_link = -99;
			next_try = get_absolute_time();
		}
		return;
	}
	if(!settings.wifi_ssid[0] && now > 5000)
	{
		snprintf(state_s, sizeof state_s, "no-settings");
		open_hotspot(now);
		return;
	}
	if(!settings.wifi_ssid[0])
	{
		snprintf(state_s, sizeof state_s, "no-settings");
		return;
	}
	int s = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
	if(s != last_link)
	{
		if(s == CYW43_LINK_UP)
		{
			joins++;
			snprintf(ip_s, sizeof ip_s, "%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
			printf("EV WIFI up ip=%s\n", ip_s);
			/* the settings page's name on the home network: info-orbs.local (and its web service), said again after
			   each re-join */
			/* (2026-11-17: the hotspot turns the home Wi-Fi off and on, and the driver removes and re-adds its network
			   for that, which wipes the name's record there; "restart" alone then did nothing and info-orbs.local was
			   gone until a restart. Now: added again whenever it's missing) */
			struct netif *sta = &cyw43_state.netif[CYW43_ITF_STA];
			cyw43_arch_lwip_begin();
			if(!mdns_started)
			{
				mdns_resp_init();
				mdns_started = true;
			}
			if(mdns_resp_netif_active(sta))
				mdns_resp_restart(sta);
			else if(mdns_resp_add_netif(sta, CYW43_HOST_NAME) == ERR_OK)
				mdns_resp_add_service(sta, "Info Orbs", "_http", DNSSD_PROTO_TCP, 80, NULL, NULL);
			else
				printf("EV WIFI the name info-orbs.local couldn't be set up\n");
			cyw43_arch_lwip_end();
			if(!sntp_on)
				sntp_restart(NULL);
		}
		else if(last_link == CYW43_LINK_UP)
		{
			printf("EV WIFI lost (%s)\n", link_name(s));
			snprintf(ip_s, sizeof ip_s, "-");
		}
		last_link = s;
	}
	snprintf(state_s, sizeof state_s, "%s", joining && s == CYW43_LINK_DOWN ? "joining" : link_name(s));
	if(s == CYW43_LINK_UP)
	{
		ever_joined = true;
		down_since = 0;
	}
	else
	{
		if(!down_since)
			down_since = now;
		if(now - down_since > (ever_joined ? HOTSPOT_AFTER_DROP_MS : HOTSPOT_AFTER_NEVER_MS))
		{
			printf("EV WIFI can't join \"%s\" for %lu min: opening the setup hotspot\n", settings.wifi_ssid,
			       (unsigned long)((now - down_since) / 60000));
			open_hotspot(now);
			return;
		}
	}
	if(s == CYW43_LINK_UP && sntp_on)
	{
		int64_t since = absolute_time_diff_us(sntp_started, get_absolute_time()) / 1000000;
		if(!orbs_time_synced() && since > 30)
			sntp_restart("no time 30 s after asking");
		else if(orbs_time_synced() && orbs_time_now() - orbs_time_last_sync() > 3 * 3600 && since > 3 * 3600)
			sntp_restart("no new time for 3 hours");
	}
	bool need = s == CYW43_LINK_DOWN || s == CYW43_LINK_FAIL || s == CYW43_LINK_NONET || s == CYW43_LINK_BADAUTH;
	if(need && time_reached(next_try))
	{
		cyw43_arch_lwip_begin();
		int r = cyw43_arch_wifi_connect_async(settings.wifi_ssid, settings.wifi_pass[0] ? settings.wifi_pass : NULL,
		                                      settings.wifi_pass[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN);
		cyw43_arch_lwip_end();
		joining = r == 0;
		next_try = make_timeout_time_ms(10000);
	}
}

bool net_up(void) { return last_link == CYW43_LINK_UP; }
const char *net_state(void) { return state_s; }
const char *net_ip(void) { return ip_s; }
const char *net_mac(void) { return mac_s; }
uint32_t net_joins(void) { return joins; }

int net_rssi(void)
{
	int32_t r = 0;
	if(!net_up() || cyw43_wifi_get_rssi(&cyw43_state, &r) != 0)
		return 0;
	return (int)r;
}
