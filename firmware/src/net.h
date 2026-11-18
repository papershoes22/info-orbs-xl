/* The XL's Wi-Fi (2026-11-16): joins the home network with the name and password the rig sent over USB, keeps
   trying every 10 s if it can't or drops, and asks pool.ntp.org for the time once it's on (then every hour).
   The Wi-Fi chip's own green LED stays off. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void net_init(void);                 /* starts the Wi-Fi chip (station mode); reads its MAC */
void net_poll(void);                 /* from the main loop: joins / rejoins, starts the time client */
bool net_up(void);                   /* joined with an address */
const char *net_state(void);         /* "no-settings", "joining", "up", "failed: ..." (for "?") */
const char *net_ip(void);
const char *net_mac(void);
int net_rssi(void);                  /* dBm, 0 = unknown */
uint32_t net_joins(void);            /* times joined since start-up */
void net_reconnect(void);            /* after new settings */
void net_wifi_saved(void);           /* the hotspot's Wi-Fi page saved a network: close it, join */
bool net_hotspot(void);              /* the setup hotspot is open */
void net_hotspot_test_open(void);    /* TEST: open it now */
void net_sta_going_off(void);       /* (hotspot.c) the home Wi-Fi is about to be turned off */
void net_wifi_saved_test_close(void);   /* TEST: close it, rejoin */
uint32_t net_sntp_restarts(void);
void net_reconnect_time(void);        /* a new time server from the settings page */    /* times the time client was started again (no answer) */
const char *net_ntp_info(void);      /* "ip/reach,ip/reach,ip/reach dns=ip" for "?" */

#ifdef __cplusplus
}
#endif
