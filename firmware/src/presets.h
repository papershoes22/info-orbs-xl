/* Presets (2026-11-17): up to 8 named copies of the
   settings, each in its own flash sector below the camera login's. Never the secrets: the Wi-Fi name and password and
   the weather key are blanked before a preset is written, and applying one keeps the current Wi-Fi, key, time zone,
   time server, weather place and the set showing. */
#pragma once
#include <stdbool.h>
#include "settings.h"
#ifdef __cplusplus
extern "C" {
#endif

#define N_PRESETS 8
bool preset_name(int slot, char *out, int n);                  /* false if that slot is empty */
bool preset_save(int slot, const char *name, const orbs_settings_t *s);
bool preset_load(int slot, orbs_settings_t *into);             /* into: the current settings, the preset's look over them */
bool preset_delete(int slot);

#ifdef __cplusplus
}
#endif
