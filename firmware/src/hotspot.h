/* The setup hotspot (2026-11-17): when the XL can't
   join the home Wi-Fi it opens its own WPA2 network "Info Orbs Setup" with a random password shown on the orbs (only
   someone looking at the orbs can join). A phone that joins gets an address (a small DHCP server here) and every name
   it looks up points at the orbs (a small DNS server here), so an iPhone opens the Wi-Fi page by itself (its "captive
   portal" sheet). There the home Wi-Fi is picked and its password typed: kept in the XL's flash as `orbs wifi` does,
   never shown again. The hotspot closes, the XL joins. Only while the XL can't join: never next to a working Wi-Fi. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define HOTSPOT_SSID "Info Orbs Setup"
#define HOTSPOT_MAX_NETS 16

typedef struct {
	char ssid[33];
	int16_t rssi;
	uint8_t secured;
} hotspot_net_t;

void hotspot_start(void);              /* scans the networks around first, then opens the hotspot */
void hotspot_poll(void);               /* (net.c) the scan done: the hotspot opens */
void hotspot_stop(void);
bool hotspot_on(void);
bool hotspot_busy(void);              /* open, or scanning before it opens */
const char *hotspot_password(void);    /* (shown on the orbs, never sent anywhere) */
int hotspot_networks(const hotspot_net_t **list);   /* the networks found when it opened, strongest first */
uint32_t hotspot_clients(void);        /* addresses handed out since it opened */

#ifdef __cplusplus
}
#endif
