/* Draws the first-light pictures on the PC with the XL's own drawing code (src/cards.c, gfx.c, fonts.c), 2026-11-16.
   Writes one 240x240 RGB file per picture into the folder given; tools/preview.sh turns them into one PNG. */
#include <stdio.h>
#include <string.h>
#include "../src/cards.h"
#include "../src/gfx.h"

static void dump(const char *dir, const char *name)
{
	char p[512];
	snprintf(p, sizeof p, "%s/%s.rgb", dir, name);
	FILE *f = fopen(p, "wb");
	if(!f) { perror(p); return; }
	for(int i = 0; i < SCR_W * SCR_H; i++)
	{
		uint16_t v = (uint16_t)((gfx_fb[i] >> 8) | (gfx_fb[i] << 8));   /* (kept byte-swapped for the screen) */
		uint8_t rgb[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31) };
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	if(argc < 2) { fprintf(stderr, "preview OUTDIR\n"); return 2; }
	card_state_t st;
	memset(&st, 0, sizeof st);
	st.last = -1;
	char n[32];
	for(int o = 0; o < N_ORBS; o++) { card_draw(o, &st); snprintf(n, sizeof n, "a%d", o); dump(argv[1], n); }
	st.last = BTN_MIDDLE; st.presses[BTN_MIDDLE] = 3; st.held[BTN_MIDDLE] = true;
	for(int o = 0; o < N_ORBS; o++) { card_draw(o, &st); snprintf(n, sizeof n, "b%d", o); dump(argv[1], n); }
	st.held[BTN_MIDDLE] = false;
	for(int o = 0; o < N_ORBS; o++) { card_draw(o, &st); snprintf(n, sizeof n, "c%d", o); dump(argv[1], n); }
	return 0;
}
