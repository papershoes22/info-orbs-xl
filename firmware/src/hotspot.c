#include "hotspot.h"
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include "net.h"

static bool on, scanning;
static char password[11];
static hotspot_net_t nets[HOTSPOT_MAX_NETS];
static int n_nets;
static uint32_t clients;
static struct udp_pcb *dhcp_pcb, *dns_pcb;
static absolute_time_t scan_end;

bool hotspot_on(void) { return on; }
bool hotspot_busy(void) { return on || scanning; }
const char *hotspot_password(void) { return password; }
uint32_t hotspot_clients(void) { return clients; }
int hotspot_networks(const hotspot_net_t **list) { *list = nets; return n_nets; }

/* ---- the networks around, before the hotspot opens (the Wi-Fi page lists them) ---- */
static int on_scan(void *env, const cyw43_ev_scan_result_t *r)
{
	(void)env;
	if(!r || !r->ssid_len || r->ssid_len > 32)
		return 0;
	char name[33];
	memcpy(name, r->ssid, r->ssid_len);
	name[r->ssid_len] = 0;
	for(int i = 0; i < n_nets; i++)
		if(!strcmp(nets[i].ssid, name))
		{
			if(r->rssi > nets[i].rssi)
				nets[i].rssi = r->rssi;
			return 0;
		}
	if(n_nets < HOTSPOT_MAX_NETS)
	{
		strcpy(nets[n_nets].ssid, name);
		nets[n_nets].rssi = r->rssi;
		nets[n_nets].secured = r->auth_mode != 0;
		n_nets++;
	}
	return 0;
}

/* ---- DHCP: an address for each phone that joins (192.168.4.16-23, an hour, the orbs as router and name server) ---- */
#define POOL 8
static uint8_t pool_mac[POOL][6];

static int lease_for(const uint8_t *mac)
{
	for(int i = 0; i < POOL; i++)
		if(!memcmp(pool_mac[i], mac, 6))
			return i;
	static const uint8_t none[6];
	for(int i = 0; i < POOL; i++)
		if(!memcmp(pool_mac[i], none, 6))
		{
			memcpy(pool_mac[i], mac, 6);
			clients++;
			return i;
		}
	return -1;
}

static void on_dhcp(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
	(void)arg; (void)addr; (void)port;
	uint8_t m[548];
	size_t n = pbuf_copy_partial(p, m, sizeof m, 0);
	pbuf_free(p);
	if(n < 240 || m[0] != 1 || m[236] != 99 || m[237] != 130 || m[238] != 83 || m[239] != 99)
		return;                                        /* (not a BOOTREQUEST with the DHCP magic) */
	int type = 0;
	for(size_t i = 240; i + 1 < n && m[i] != 255;)
	{
		if(m[i] == 0) { i++; continue; }
		if(m[i] == 53 && m[i + 1] >= 1 && i + 2 < n)
			type = m[i + 2];
		i += 2 + m[i + 1];
	}
	if(type != 1 && type != 3)                         /* DISCOVER or REQUEST */
		return;
	int k = lease_for(m + 28);
	if(k < 0)
		return;
	uint8_t o[300];
	memset(o, 0, sizeof o);
	o[0] = 2; o[1] = 1; o[2] = 6;                      /* BOOTREPLY, Ethernet */
	memcpy(o + 4, m + 4, 4);                           /* xid */
	memcpy(o + 10, m + 10, 2);                         /* flags */
	o[16] = 192; o[17] = 168; o[18] = 4; o[19] = (uint8_t)(16 + k);    /* yiaddr */
	o[20] = 192; o[21] = 168; o[22] = 4; o[23] = 1;    /* siaddr */
	memcpy(o + 28, m + 28, 16);                        /* chaddr */
	o[236] = 99; o[237] = 130; o[238] = 83; o[239] = 99;
	uint8_t *q = o + 240;
	*q++ = 53; *q++ = 1; *q++ = type == 1 ? 2 : 5;     /* OFFER / ACK */
	*q++ = 54; *q++ = 4; *q++ = 192; *q++ = 168; *q++ = 4; *q++ = 1;
	*q++ = 51; *q++ = 4; *q++ = 0; *q++ = 0; *q++ = 0x0E; *q++ = 0x10;  /* an hour */
	*q++ = 1; *q++ = 4; *q++ = 255; *q++ = 255; *q++ = 255; *q++ = 0;
	*q++ = 3; *q++ = 4; *q++ = 192; *q++ = 168; *q++ = 4; *q++ = 1;
	*q++ = 6; *q++ = 4; *q++ = 192; *q++ = 168; *q++ = 4; *q++ = 1;
	*q++ = 255;
	struct pbuf *r = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(q - o), PBUF_RAM);
	if(!r)
		return;
	memcpy(r->payload, o, (size_t)(q - o));
	ip_addr_t bcast;
	IP_ADDR4(&bcast, 255, 255, 255, 255);
	udp_sendto_if(pcb, r, &bcast, 68, &cyw43_state.netif[CYW43_ITF_AP]);
	pbuf_free(r);
}

/* ---- DNS: every name points at the orbs, so a phone's "is there internet?" check lands on the Wi-Fi page ---- */
static void on_dns(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
	(void)arg;
	uint8_t m[512];
	size_t n = pbuf_copy_partial(p, m, sizeof m, 0);
	pbuf_free(p);
	if(n < 17 || (m[2] & 0x80) || m[4] != 0 || m[5] != 1)
		return;                                        /* (one question only) */
	size_t i = 12;
	while(i < n && m[i])
		i += 1 + m[i];
	if(i + 5 > n)
		return;
	uint16_t qtype = (uint16_t)(m[i + 1] << 8 | m[i + 2]);
	size_t qend = i + 5;
	uint8_t o[512];
	memcpy(o, m, qend);
	o[2] = 0x81; o[3] = 0x80;                          /* an answer, recursion available, no error */
	o[6] = 0; o[7] = (qtype == 1 || qtype == 255) ? 1 : 0;
	o[8] = o[9] = o[10] = o[11] = 0;
	size_t k = qend;
	if(o[7])
	{
		static const uint8_t a[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1 };
		memcpy(o + k, a, sizeof a);
		k += sizeof a;
	}
	struct pbuf *r = pbuf_alloc(PBUF_TRANSPORT, (u16_t)k, PBUF_RAM);
	if(!r)
		return;
	memcpy(r->payload, o, k);
	udp_sendto(pcb, r, addr, port);
	pbuf_free(r);
}

static void make_password(void)
{
	static const char abc[] = "abcdefghjkmnpqrstuvwxyz23456789";   /* (no l/1, o/0 to misread) */
	for(int i = 0; i < 10; i++)
		password[i] = abc[get_rand_32() % (sizeof abc - 1)];
	password[10] = 0;
}

void hotspot_start(void)
{
	if(on || scanning)
		return;
	n_nets = 0;
	cyw43_wifi_scan_options_t opts = { 0 };
	scanning = cyw43_wifi_scan(&cyw43_state, &opts, NULL, on_scan) == 0;
	scan_end = make_timeout_time_ms(6000);
	printf("EV HOTSPOT scanning the networks around first\n");
	if(!scanning)
		scan_end = get_absolute_time();
}

/* (from net.c's poll) the scan done: the hotspot opens */
void hotspot_poll(void)
{
	if(!scanning)
		return;
	if(cyw43_wifi_scan_active(&cyw43_state) && !time_reached(scan_end))
		return;
	scanning = false;
	for(int i = 0; i < n_nets; i++)                  /* strongest first */
		for(int j = i + 1; j < n_nets; j++)
			if(nets[j].rssi > nets[i].rssi)
			{
				hotspot_net_t t = nets[i];
				nets[i] = nets[j];
				nets[j] = t;
			}
	if(!password[0])
		make_password();                               /* (one password per power-on: the same if it opens again) */
	memset(pool_mac, 0, sizeof pool_mac);
	clients = 0;
	net_sta_going_off();                           /* (info-orbs.local's record off first: net.c) */
	cyw43_arch_disable_sta_mode();
	cyw43_arch_enable_ap_mode(HOTSPOT_SSID, password, CYW43_AUTH_WPA2_AES_PSK);
	cyw43_arch_lwip_begin();
	dhcp_pcb = udp_new();
	if(dhcp_pcb && udp_bind(dhcp_pcb, IP_ANY_TYPE, 67) == ERR_OK)
		udp_recv(dhcp_pcb, on_dhcp, NULL);
	if(dhcp_pcb)
		ip_set_option(dhcp_pcb, SOF_BROADCAST);
	dns_pcb = udp_new();
	if(dns_pcb && udp_bind(dns_pcb, IP_ANY_TYPE, 53) == ERR_OK)
		udp_recv(dns_pcb, on_dns, NULL);
	cyw43_arch_lwip_end();
	on = true;
	printf("EV HOTSPOT open: \"%s\" (password on the orbs), %d networks found, page http://192.168.4.1/\n", HOTSPOT_SSID, n_nets);
}

void hotspot_stop(void)
{
	if(!on)
	{
		scanning = false;
		return;
	}
	cyw43_arch_lwip_begin();
	if(dhcp_pcb) udp_remove(dhcp_pcb);
	if(dns_pcb) udp_remove(dns_pcb);
	dhcp_pcb = dns_pcb = NULL;
	cyw43_arch_lwip_end();
	cyw43_arch_disable_ap_mode();
	cyw43_arch_enable_sta_mode();
	on = false;
	printf("EV HOTSPOT closed\n");
}
