/* The first-light picture for each orb (build step 1, 2026-11-16): its name (D1-D5), the XL pin that selects it,
   which way is up, a colour check and the last button pressed. Plain C, drawn by the XL and by tools/preview.c. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define N_ORBS 5
enum { BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, N_BTNS };

typedef struct {
	int last;                 /* the last button pressed (BTN_*), -1 = none yet */
	bool held[N_BTNS];        /* held down right now */
	uint32_t presses[N_BTNS]; /* presses counted since start-up */
} card_state_t;

extern const char *const orb_name[N_ORBS];     /* "D1".."D5": the orbs board's own screen names, left to right */
extern const uint8_t orb_cs_pin[N_ORBS];       /* the XL pin that selects each one (xl-case/WIRING.md) */
extern const char *const btn_name[N_BTNS];

void card_draw(int orb, const card_state_t *st);   /* into gfx_fb */
