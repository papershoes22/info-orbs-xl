/* The settings page's web server on the XL (2026-11-17, web/API.md): plain HTTP on port 80, the home network only, no
   login, never the Wi-Fi password or the keys. lwIP's own TCP calls fill each request; the main loop
   answers it (web_poll), so nothing about the orbs is touched from the network's interrupt. */
#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <stddef.h>
extern "C" {
#include "settings.h"
}

void web_init(void);                 /* once Wi-Fi is up: listens on port 80 */
void web_poll(uint32_t now_ms);      /* main loop: answers requests, sends what's waiting */
uint32_t web_requests(void);         /* answered since start-up (for "?") */
bool web_geocode_busy(void);         /* the page's place search is using the HTTPS client */
bool web_sending(const uint8_t *p, size_t len);   /* a connection is still sending from that memory */

/* ---- what main.cpp gives the server ---- */
void web_status(JsonObject st);                                        /* the page's "status" */
bool web_apply(const orbs_settings_t &next);                           /* a checked change: shown at once, saved soon */
bool web_action(JsonObjectConst a, char *why, size_t n);               /* show / dark / test / restart / defaults */
/* what orb `orb` shows now: a JPEG somewhere (*ext) or a BMP made into buf; returns the length, 0 if nothing */
size_t web_orb_picture(int orb, uint8_t *buf, size_t cap, const uint8_t **ext, const char **ctype);
bool web_https_free(void);                                             /* no weather or Frigate fetch running */
void web_wifi_save(const char *ssid, const char *pass);                /* the hotspot page: keep it, then join */
void web_presets(JsonArray out);                                       /* the presets: [{slot, name} or null] */
bool web_preset(JsonObjectConst a, char *why, size_t n);               /* save / apply / delete one */
