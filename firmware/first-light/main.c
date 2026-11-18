/* Info Orbs on the Pimoroni Pico LiPo 2 XL W: build step 1, "first light" (2026-11-16).
 *
 * Each of the five screens shows its name (D1-D5, left to right), the XL pin that selects it, which way is up and a
 * red/green/blue check. The three buttons are read (GP18 left, GP19 middle, GP20 right: 5 V when pressed through the
 * orbs board, the XL's own pull-downs otherwise; its A4 chip takes 5 V on these pins while powered) and each press
 * shows on the bottom line of every screen and goes to the rig over USB. The Wi-Fi chip is started only to read its
 * network (MAC) address and keep its green LED off: no network is joined, nothing is sent over the air.
 *
 * USB serial (tools/orbs_xl.py in the main firmware's folder), one line each way:
 *   "?"  -> "ORBS 1 build=<v> board=pico_lipo2xl_w spi_hz=<n> mac=<aa:bb:..> up_ms=<n> wd_restart=<0|1> draw_ms=<n>"
 *   "b"  -> "BTN left=<n> middle=<n> right=<n> held=<l><m><r>"   (presses since start-up; held 1/0 each)
 * and by itself: "EV BTN <LEFT|MIDDLE|RIGHT> <DOWN|UP> t=<ms>" for every press and release (after a 35 ms debounce,
 * the original firmware's BUTTON_DEBOUNCE_TIME).
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "cards.h"
#include "gfx.h"
#include "lcd.h"

static const uint8_t btn_pin[N_BTNS] = { 18, 19, 20 };
#define DEBOUNCE_MS 35

/* button events from the 1 ms timer to the main loop */
typedef struct { uint8_t btn, down; uint32_t t; } bev_t;
#define BQ 32
static bev_t bq[BQ];
static volatile uint32_t bq_in, bq_out, bq_lost;
static bool b_state[N_BTNS];
static uint8_t b_count[N_BTNS];

static bool btn_tick(repeating_timer_t *rt)
{
	(void)rt;
	for(int i = 0; i < N_BTNS; i++)
	{
		bool raw = gpio_get(btn_pin[i]);
		if(raw == b_state[i])
		{
			b_count[i] = 0;
			continue;
		}
		if(++b_count[i] < DEBOUNCE_MS)
			continue;
		b_state[i] = raw;
		b_count[i] = 0;
		if(bq_in - bq_out >= BQ)
		{
			bq_lost++;
			continue;
		}
		bq[bq_in % BQ] = (bev_t){ (uint8_t)i, (uint8_t)raw, to_ms_since_boot(get_absolute_time()) };
		__dmb();
		bq_in++;
	}
	return true;
}

static card_state_t st = { .last = -1 };
static uint32_t draw_ms;

/* every orb's picture, drawn one after another into the one frame and sent; rows y0..y1-1 only */
static void show_all(int y0, int y1)
{
	uint32_t t0 = to_ms_since_boot(get_absolute_time());
	for(int o = 0; o < N_ORBS; o++)
	{
		card_draw(o, &st);
		lcd_show_rows(o, gfx_fb, y0, y1);
	}
	draw_ms = to_ms_since_boot(get_absolute_time()) - t0;
}

int main(void)
{
	bool wd = watchdog_caused_reboot();
	stdio_init_all();
	for(int i = 0; i < N_BTNS; i++)
	{
		gpio_init(btn_pin[i]);
		gpio_set_dir(btn_pin[i], GPIO_IN);
		gpio_pull_down(btn_pin[i]);
	}
	static repeating_timer_t timer;
	add_repeating_timer_ms(-1, btn_tick, NULL, &timer);

	lcd_init();
	show_all(0, 240);

	/* the Wi-Fi chip: up only to read its MAC (it comes up on the first command) and to hold its LED off */
	char mac[18] = "unknown";
	if(cyw43_arch_init() == 0)
	{
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
		const uint8_t *m = cyw43_state.mac;
		snprintf(mac, sizeof mac, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
	}
	watchdog_enable(3000, true);           /* stuck for 3 s: start again (and say so in the next "?" answer) */
	printf("EV START build=%s wd_restart=%d\n", ORBS_BUILD, wd);

	char line[16];
	int n = 0;
	for(;;)
	{
		watchdog_update();
		int c = getchar_timeout_us(0);
		if(c >= 0)
		{
			if(c == '\n' || c == '\r' || n >= (int)sizeof line - 1)
				n = 0;
			else
				line[n++] = (char)c;
			if(n == 1 && line[0] == '?')
			{
				printf("ORBS 1 build=%s board=pico_lipo2xl_w spi_hz=%lu mac=%s up_ms=%lu wd_restart=%d draw_ms=%lu\n",
				       ORBS_BUILD, (unsigned long)lcd_spi_hz(), mac, (unsigned long)to_ms_since_boot(get_absolute_time()),
				       wd, (unsigned long)draw_ms);
				n = 0;
			}
			else if(n == 1 && line[0] == 'b')
			{
				printf("BTN left=%lu middle=%lu right=%lu held=%d%d%d lost=%lu\n", (unsigned long)st.presses[0],
				       (unsigned long)st.presses[1], (unsigned long)st.presses[2], st.held[0], st.held[1], st.held[2],
				       (unsigned long)bq_lost);
				n = 0;
			}
		}
		bool dirty = false;
		while(bq_out != bq_in)
		{
			__dmb();
			bev_t e = bq[bq_out % BQ];
			bq_out++;
			st.held[e.btn] = e.down;
			if(e.down)
			{
				st.presses[e.btn]++;
				st.last = e.btn;
			}
			printf("EV BTN %s %s t=%lu\n", btn_name[e.btn], e.down ? "DOWN" : "UP", (unsigned long)e.t);
			dirty = true;
		}
		if(dirty)
			show_all(184, 216);            /* only the bottom line changes */
		else
			sleep_us(500);
	}
}
