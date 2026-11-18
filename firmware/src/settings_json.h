/* The settings as the page's JSON (web/API.md), and changes from the page checked against the same rules as the stand-in
   (web/standin.py) before anything is kept: all or nothing (2026-11-17). Never the Wi-Fi password or the key. */
#pragma once
#include <ArduinoJson.h>
extern "C" {
#include "settings.h"
}

#define N_CAMS_ALL 7
extern const char *const SET_KEYS[N_SETS_SAVED];     /* clock weather hourly sky matrix rig music trove cams viz, then the
                                                        new sets (part 3) and Messages */
extern const char *const SET_TITLES[N_SETS_SAVED];
extern const bool SET_IS_RIG[N_SETS_SAVED];
uint32_t settings_sets_known(void);                  /* the sets the page lists: the XL's own and the rig's built ones */
extern uint32_t sets_from_rig;                       /* the rig's sets its catalogue says are built ("C") */
int set_key_index_any(const char *key);              /* any set's number, known or not (-1 if none) */
#define N_THEMES 8
extern const char *const THEMES[N_THEMES];           /* own ember ocean forest mono neon sunset ice */
#define N_ACTIONS 15
extern const char *const ACTIONS[N_ACTIONS];         /* a button's action (0xC0 + set = show that set) */
extern const char *const CAM_IDS[N_CAMS_ALL];         /* the order of rig/faces.py CAMS_ALL and frigate_cams.cpp */
extern const char *const CAM_TITLES[N_CAMS_ALL];

void settings_to_json(const orbs_settings_t &s, JsonObject out);
/* the patch onto `s` (a copy: the caller keeps it only if this says true); errors: {"clock.color": "why"} */
bool settings_patch(orbs_settings_t &s, JsonObjectConst patch, JsonObject errors);
int set_key_index(const char *key);                  /* -1 if none */
