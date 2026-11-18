#include "cards.h"
#include "gfx.h"
#include <stdio.h>

const char *const orb_name[N_ORBS] = { "D1", "D2", "D3", "D4", "D5" };
const uint8_t orb_cs_pin[N_ORBS] = { 22, 16, 46, 17, 31 };
const char *const btn_name[N_BTNS] = { "LEFT", "MIDDLE", "RIGHT" };

/* one colour per orb for its edge ring (D3 = the clock's default green, #4aff00) */
static const uint16_t orb_col[N_ORBS] = {
	RGB(255, 90, 78), RGB(255, 176, 46), RGB(74, 255, 0), RGB(46, 197, 255), RGB(180, 123, 255),
};

#define WHITE RGB(255, 255, 255)
#define GREY RGB(150, 160, 175)
#define DIM RGB(95, 104, 120)

/* a bar with round ends, cx +- half_w, from y to y + h */
static void pill(int cx, int y, int half_w, int h, uint16_t c)
{
	float r = h / 2.0f;
	gfx_rect(cx - half_w + (int)r, y, 2 * half_w - 2 * (int)r, h, c);
	gfx_disc(cx - half_w + r, y + r, r, c);
	gfx_disc(cx + half_w - r, y + r, r, c);
}

void card_draw(int orb, const card_state_t *st)
{
	char s[32];
	gfx_backdrop();
	/* the edge: a ring touching the screen's rim all the way round shows the picture is centred, none of it cut off */
	gfx_ring(120, 120, 112.5f, 119.5f, orb_col[orb]);
	/* which way is up */
	gfx_tri(120, 12, 111, 25, 129, 25, WHITE);
	gfx_text_c(&font_info, 120, 27, "first light", DIM);
	/* the orb's name, as printed on the orbs board */
	gfx_text_mid(&font_name, 120, 88, orb_name[orb], WHITE, 'D');
	snprintf(s, sizeof s, "select GP%d", orb_cs_pin[orb]);
	gfx_text_c(&font_info, 120, 132, s, GREY);
	/* colour check: red, green, blue, each over its letter */
	static const uint16_t dot[3] = { RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255) };
	static const char *const letter[3] = { "R", "G", "B" };
	for(int i = 0; i < 3; i++)
	{
		gfx_disc(96 + 24 * i, 158, 8, dot[i]);
		gfx_text_c(&font_info, 96 + 24 * i, 168, letter[i], GREY);
	}
	/* the buttons: the last one pressed and how many times; lit while it's held */
	if(st->last < 0)
		gfx_text_c(&font_lab, 120, 187, "press a button", GREY);
	else
	{
		snprintf(s, sizeof s, "%s %lu", btn_name[st->last], (unsigned long)st->presses[st->last]);
		if(st->held[st->last])
		{
			pill(120, 189, 56, 22, orb_col[orb]);
			gfx_text_c(&font_lab, 120, 187, s, RGB(10, 12, 16));
		}
		else
			gfx_text_c(&font_lab, 120, 187, s, WHITE);
	}
}
