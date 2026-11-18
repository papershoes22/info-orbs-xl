/* The five GC9A01 screens. The start-up is the one the orbs' original firmware ran on these very screens (TFT_eSPI's
   GC9A01_Init.h through ScreenManager.cpp): all five selected together, a reset pulse
   (5 ms high, 20 ms low, then 150 ms), the panel's vendor registers, 16-bit colour, colour inversion on, sleep out,
   display on, then MADCTL 0x08 (setRotation(0), "no rotation": no flips, BGR order). SPI mode 0. */
#include "lcd.h"
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pico/mutex.h"

#define LCD_SPI spi0
#define PIN_SCK 34
#define PIN_MOSI 7
#define PIN_DC 15
#define PIN_RST 14
/* pictures: 37.5 MHz from the XL's 150 MHz clock (step 4, 2026-11-16), a speed another GC9A01 project of ours had
   run for days. Steps 1-3 ran the original firmware's 25 MHz
   (SPI_FREQUENCY 27 MHz in config.system.h). Commands stay at LCD_CMD_HZ. */
#ifdef ORBS_LCD_HZ
#define LCD_HZ ORBS_LCD_HZ                     /* (overclocked: 41.7 MHz from 250 MHz; 40 would give 31.25) */
#else
#define LCD_HZ (40 * 1000 * 1000)
#endif
/* start-up and settings commands go slower (2026-11-16): one photo showed D1 with its colour inversion off
   (black shown white), the others right, and right again after a restart. INVON (0x21) and INVOFF (0x20) are one bit
   apart, so one flipped bit in a shared start-up looks exactly like that (a GUESS, not proven). At 5 MHz the commands
   have five times the margin; pictures stay at full speed. */
#define LCD_CMD_HZ (5 * 1000 * 1000)

static const uint8_t cs_pin[5] = { 22, 16, 46, 17, 31 };   /* D1-D5 (hardware/WIRING.md) */
static int dma_ch = -1;
static uint32_t spi_hz;
/* the pictures' speed: LCD_HZ, or the settings page's Screens > "Screen link" (2026-11-18; the same GC9A01 chip
   ran clean at 75 MHz in another project): 62.5 or 125 MHz from the 250 MHz clock */
static uint32_t pix_hz = LCD_HZ;
/* the screens' bus is shared by both cores (the second one sends the rig's pictures): one at a time */
static mutex_t bus;

static void cs_all(int level)
{
	for(int i = 0; i < 5; i++)
		gpio_put(cs_pin[i], level);
}

/* a command and its data to whichever screens are selected */
static void send(uint8_t c, const uint8_t *d, int n)
{
	gpio_put(PIN_DC, 0);
	spi_write_blocking(LCD_SPI, &c, 1);
	if(n > 0)
	{
		gpio_put(PIN_DC, 1);
		spi_write_blocking(LCD_SPI, d, (size_t)n);
	}
}

/* TFT_eSPI's GC9A01_Init.h, value for value. Each entry: command, count, data...; count 0x80 | ms = a pause after */
static const uint8_t init_seq[] = {
	0xEF, 0, 0xEB, 1, 0x14, 0xFE, 0, 0xEF, 0, 0xEB, 1, 0x14,
	0x84, 1, 0x40, 0x85, 1, 0xFF, 0x86, 1, 0xFF, 0x87, 1, 0xFF, 0x88, 1, 0x0A, 0x89, 1, 0x21, 0x8A, 1, 0x00,
	0x8B, 1, 0x80, 0x8C, 1, 0x01, 0x8D, 1, 0x01, 0x8E, 1, 0xFF, 0x8F, 1, 0xFF,
	0xB6, 2, 0x00, 0x20,
	0x3A, 1, 0x05,
	0x90, 4, 0x08, 0x08, 0x08, 0x08,
	0xBD, 1, 0x06, 0xBC, 1, 0x00,
	0xFF, 3, 0x60, 0x01, 0x04,
	0xC3, 1, 0x13, 0xC4, 1, 0x13, 0xC9, 1, 0x22, 0xBE, 1, 0x11,
	0xE1, 2, 0x10, 0x0E,
	0xDF, 3, 0x21, 0x0C, 0x02,
	0xF0, 6, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A,
	0xF1, 6, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F,
	0xF2, 6, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A,
	0xF3, 6, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F,
	0xED, 2, 0x1B, 0x0B,
	0xAE, 1, 0x77,
	0xCD, 1, 0x63,
	0x70, 9, 0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03,
	0xE8, 1, 0x34,
	0x62, 12, 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70,
	0x63, 12, 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70,
	0x64, 7, 0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07,
	0x66, 10, 0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00,
	0x67, 10, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98,
	0x74, 7, 0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00,
	0x98, 2, 0x3E, 0x07,
	0x35, 0,
	0x21, 0,
	0x11, 0x80 | 120,
	0x29, 0x80 | 20,
	0x36, 1, 0x08,        /* setRotation(0) */
};

void lcd_init(void)
{
	mutex_init(&bus);
	spi_hz = spi_init(LCD_SPI, LCD_HZ);
	spi_set_baudrate(LCD_SPI, LCD_CMD_HZ);
	spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
	gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
	/* clock and data reach five screens over the orbs board: stronger, faster edges than the 4 mA default (the ESP32
	   drove these at its default ~20 mA) */
	gpio_set_drive_strength(PIN_SCK, GPIO_DRIVE_STRENGTH_8MA);
	gpio_set_drive_strength(PIN_MOSI, GPIO_DRIVE_STRENGTH_8MA);
	gpio_set_slew_rate(PIN_SCK, GPIO_SLEW_RATE_FAST);
	gpio_set_slew_rate(PIN_MOSI, GPIO_SLEW_RATE_FAST);
	for(int i = 0; i < 5; i++)
	{
		gpio_init(cs_pin[i]);
		gpio_put(cs_pin[i], 1);
		gpio_set_dir(cs_pin[i], GPIO_OUT);
	}
	gpio_init(PIN_DC);
	gpio_put(PIN_DC, 1);
	gpio_set_dir(PIN_DC, GPIO_OUT);
	gpio_init(PIN_RST);
	gpio_put(PIN_RST, 1);
	gpio_set_dir(PIN_RST, GPIO_OUT);

	cs_all(0);                       /* all five at once, as ScreenManager does */
	send(0x00, 0, 0);                /* (TFT_eSPI: a no-op first, to put the bus in a known state) */
	gpio_put(PIN_RST, 1);
	sleep_ms(5);
	gpio_put(PIN_RST, 0);
	sleep_ms(20);
	gpio_put(PIN_RST, 1);
	sleep_ms(150);
	for(size_t i = 0; i < sizeof(init_seq);)
	{
		uint8_t c = init_seq[i++];
		uint8_t n = init_seq[i++];
		if(n & 0x80)
		{
			send(c, 0, 0);
			sleep_ms(n & 0x7F);
		}
		else
		{
			send(c, &init_seq[i], n);
			i += n;
		}
	}
	cs_all(1);
	spi_set_baudrate(LCD_SPI, pix_hz);

	dma_ch = dma_claim_unused_channel(true);
	dma_channel_config cfg = dma_channel_get_default_config((uint)dma_ch);
	channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
	channel_config_set_dreq(&cfg, spi_get_dreq(LCD_SPI, true));
	channel_config_set_read_increment(&cfg, true);
	channel_config_set_write_increment(&cfg, false);
	dma_channel_configure((uint)dma_ch, &cfg, &spi_get_hw(LCD_SPI)->dr, 0, 0, false);
}

uint32_t lcd_spi_hz(void)
{
	return spi_hz;
}

/* the pictures' speed from now on: 0 = LCD_HZ (41.7 MHz), 1 = 62.5 MHz, 2 = 125 MHz (the SPI's clk_peri / 4 and / 2).
   Commands keep LCD_CMD_HZ. Waits for whatever is on the bus to finish first. */
uint32_t lcd_set_link(int level)
{
	uint32_t hz = level == 2 ? 125000000u : level == 1 ? 62500000u : (uint32_t)LCD_HZ;
	mutex_enter_blocking(&bus);
	pix_hz = hz;
	spi_hz = spi_set_baudrate(LCD_SPI, pix_hz);
	mutex_exit(&bus);
	return spi_hz;
}

/* ---- per-orb trim (settings version 4): each channel through a small table per orb ---- */
static bool trim_on[5];
static uint8_t trim_r[5][32], trim_g[5][64], trim_b[5][32];

void lcd_set_trim(int orb, int bright_pc, int r_pc, int g_pc, int b_pc)
{
	if(orb < 0 || orb >= 5)
		return;
	int kr = bright_pc * r_pc, kg = bright_pc * g_pc, kb = bright_pc * b_pc;   /* (out of 10000) */
	trim_on[orb] = !(kr == 10000 && kg == 10000 && kb == 10000);
	for(int v = 0; v < 64; v++)
	{
		if(v < 32)
		{
			trim_r[orb][v] = (uint8_t)((v * kr + 5000) / 10000);
			trim_b[orb][v] = (uint8_t)((v * kb + 5000) / 10000);
		}
		trim_g[orb][v] = (uint8_t)((v * kg + 5000) / 10000);
	}
}

bool lcd_trimmed(int orb) { return orb >= 0 && orb < 5 && trim_on[orb]; }

/* byte-swapped RGB565 pixels (as the screens take them), trimmed in place */
void __not_in_flash_func(lcd_trim)(int orb, uint16_t *px, uint32_t n)
{
	if(orb < 0 || orb >= 5 || !trim_on[orb])
		return;
	const uint8_t *tr = trim_r[orb], *tg = trim_g[orb], *tb = trim_b[orb];
	for(uint32_t i = 0; i < n; i++)
	{
		uint16_t v = (uint16_t)(px[i] >> 8 | px[i] << 8);
		v = (uint16_t)(tr[v >> 11] << 11 | tg[(v >> 5) & 63] << 5 | tb[v & 31]);
		px[i] = (uint16_t)(v >> 8 | v << 8);
	}
}

static void show_rows_cs(int orb, const uint16_t *fb, int y0, int y1);
static volatile int stream_orb = -1;         /* the screen a streamed picture is going to, -1 = none */

void lcd_show_rows(int orb, const uint16_t *fb, int y0, int y1)
{
	if(orb < 0 || orb >= 5)
		return;
	show_rows_cs(orb, fb, y0, y1);
}

/* the same picture to all five screens at once (every select line down, as the ESP32 drew the Matrix) */
void lcd_show_rows_all(const uint16_t *fb, int y0, int y1)
{
	show_rows_cs(-1, fb, y0, y1);
}

void orbs_step(uint32_t n);

static void show_rows_cs(int orb, const uint16_t *fb, int y0, int y1)
{
	if(y0 < 0 || y1 > 240 || y1 <= y0)
		return;
	orbs_step(50);                             /* (waiting for the screens' bus) */
	mutex_enter_blocking(&bus);
	orbs_step(51);                             /* (sending rows) */
	if(orb < 0)
		cs_all(0);
	else
		gpio_put(cs_pin[orb], 0);
	const uint8_t caset[4] = { 0, 0, 0, 239 };
	const uint8_t raset[4] = { 0, (uint8_t)y0, 0, (uint8_t)(y1 - 1) };
	send(0x2A, caset, 4);
	send(0x2B, raset, 4);
	send(0x2C, 0, 0);
	gpio_put(PIN_DC, 1);
	if(orb >= 0 && trim_on[orb])
	{
		/* a trimmed orb: each row copied, trimmed, sent (two row buffers: one goes out while the next is made) */
		static uint16_t row[2][240];
		int cur = 0;
		for(int y = y0; y < y1; y++, cur ^= 1)
		{
			memcpy(row[cur], fb + y * 240, sizeof row[cur]);
			lcd_trim(orb, row[cur], 240);
			dma_channel_wait_for_finish_blocking((uint)dma_ch);
			dma_channel_set_read_addr((uint)dma_ch, row[cur], false);
			dma_channel_set_trans_count((uint)dma_ch, 240u * 2u, true);
		}
		dma_channel_wait_for_finish_blocking((uint)dma_ch);
	}
	else
	{
		dma_channel_set_read_addr((uint)dma_ch, fb + y0 * 240, false);
		dma_channel_set_trans_count((uint)dma_ch, (uint32_t)(y1 - y0) * 240u * 2u, true);
		dma_channel_wait_for_finish_blocking((uint)dma_ch);
	}
	while(spi_is_busy(LCD_SPI))
		tight_loop_contents();
	/* the receive side filled with junk while sending: empty it and clear its overrun flag */
	while(spi_is_readable(LCD_SPI))
		(void)spi_get_hw(LCD_SPI)->dr;
	spi_get_hw(LCD_SPI)->icr = SPI_SSPICR_RORIC_BITS;
	if(orb < 0)
		cs_all(1);
	else
		gpio_put(cs_pin[orb], 1);
	mutex_exit(&bus);
	orbs_step(52);
}

/* a picture streamed to one screen, for the second core (pics.cpp): the window is set once, then each strip of rows
   goes out by DMA as soon as it's decoded, while the next strip decodes. The bus stays taken from lcd_stream_begin
   to lcd_stream_end. */
void lcd_stream_begin(int orb, int w, int h)
{
	mutex_enter_blocking(&bus);
	stream_orb = orb;
	gpio_put(cs_pin[orb], 0);
	const uint8_t caset[4] = { 0, 0, 0, (uint8_t)(w - 1) }, raset[4] = { 0, 0, 0, (uint8_t)(h - 1) };
	send(0x2A, caset, 4);
	send(0x2B, raset, 4);
	send(0x2C, 0, 0);
	gpio_put(PIN_DC, 1);
}

/* the same for a rectangle anywhere on the screen (the live faces' changed parts) */
void lcd_stream_begin_rect(int orb, int x, int y, int w, int h)
{
	mutex_enter_blocking(&bus);
	stream_orb = orb;
	gpio_put(cs_pin[orb], 0);
	const uint8_t caset[4] = { 0, (uint8_t)x, 0, (uint8_t)(x + w - 1) }, raset[4] = { 0, (uint8_t)y, 0, (uint8_t)(y + h - 1) };
	send(0x2A, caset, 4);
	send(0x2B, raset, 4);
	send(0x2C, 0, 0);
	gpio_put(PIN_DC, 1);
}

void lcd_stream_rows(const uint16_t *px, uint32_t n_pixels)
{
	dma_channel_wait_for_finish_blocking((uint)dma_ch);
	dma_channel_set_read_addr((uint)dma_ch, px, false);
	dma_channel_set_trans_count((uint)dma_ch, n_pixels * 2u, true);
}

void lcd_stream_wait(void)
{
	dma_channel_wait_for_finish_blocking((uint)dma_ch);
}

void lcd_stream_end(void)
{
	dma_channel_wait_for_finish_blocking((uint)dma_ch);
	while(spi_is_busy(LCD_SPI))
		tight_loop_contents();
	while(spi_is_readable(LCD_SPI))
		(void)spi_get_hw(LCD_SPI)->dr;
	spi_get_hw(LCD_SPI)->icr = SPI_SSPICR_RORIC_BITS;
	gpio_put(cs_pin[stream_orb], 1);
	stream_orb = -1;
	mutex_exit(&bus);
}

/* every screen's own settings again, at the slow clock (main loop, every 5 s, between pictures): sleep out, 16-bit
   colour, normal mode, inversion on, display on (all five at once), then each orb's MADCTL (its own turn). Each leaves a screen that already has it
   as it was (no flicker); a screen that lost one gets it back. */
static uint32_t refreshes;
/* each orb's turn (2026-11-17, the settings page): the screen's own MADCTL, so nothing is drawn differently. 0x08 = the
   original's setRotation(0) (BGR); 90 = MV|MX, 180 = MX|MY, 270 = MV|MY, as TFT_eSPI's GC9A01 rotations */
static const uint8_t madctl_for[4] = { 0x08, 0x68, 0xC8, 0xA8 };
static uint8_t madctl[5] = { 0x08, 0x08, 0x08, 0x08, 0x08 };

void lcd_set_rotation(int orb, int quarter_turns)
{
	if(orb < 0 || orb >= 5)
		return;
	madctl[orb] = madctl_for[quarter_turns & 3];
	mutex_enter_blocking(&bus);
	spi_set_baudrate(LCD_SPI, LCD_CMD_HZ);
	gpio_put(cs_pin[orb], 0);
	send(0x36, &madctl[orb], 1);
	gpio_put(cs_pin[orb], 1);
	spi_set_baudrate(LCD_SPI, pix_hz);
	mutex_exit(&bus);
}

void lcd_refresh(void)
{
	static const uint8_t colmod = 0x05;
	mutex_enter_blocking(&bus);
	spi_set_baudrate(LCD_SPI, LCD_CMD_HZ);
	cs_all(0);
	send(0x11, 0, 0);
	send(0x3A, &colmod, 1);
	send(0x13, 0, 0);
	send(0x21, 0, 0);
	send(0x29, 0, 0);
	cs_all(1);
	for(int o = 0; o < 5; o++)                 /* (each orb's own turn) */
	{
		gpio_put(cs_pin[o], 0);
		send(0x36, &madctl[o], 1);
		gpio_put(cs_pin[o], 1);
	}
	spi_set_baudrate(LCD_SPI, pix_hz);
	mutex_exit(&bus);
	refreshes++;
}

uint32_t lcd_refreshes(void)
{
	return refreshes;
}

/* TEST ONLY: turns one screen's colour inversion off, as a photo showed D1 */
void lcd_test_invoff(int orb)
{
	if(orb < 0 || orb >= 5)
		return;
	mutex_enter_blocking(&bus);
	spi_set_baudrate(LCD_SPI, LCD_CMD_HZ);
	gpio_put(cs_pin[orb], 0);
	send(0x20, 0, 0);
	gpio_put(cs_pin[orb], 1);
	spi_set_baudrate(LCD_SPI, pix_hz);
	mutex_exit(&bus);
}

/* one small rectangle of an orb's picture straight to its screen (the visualizer's bar segments: 11 x 6 pixels, a
   hundred or so a frame), by plain SPI writes */
void lcd_show_rect(int orb, const uint16_t *fb, int x, int y, int w, int h)
{
	if(orb < 0 || orb >= 5 || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > 240 || y + h > 240)
		return;
	mutex_enter_blocking(&bus);
	gpio_put(cs_pin[orb], 0);
	const uint8_t caset[4] = { 0, (uint8_t)x, 0, (uint8_t)(x + w - 1) };
	const uint8_t raset[4] = { 0, (uint8_t)y, 0, (uint8_t)(y + h - 1) };
	send(0x2A, caset, 4);
	send(0x2B, raset, 4);
	send(0x2C, 0, 0);
	gpio_put(PIN_DC, 1);
	for(int j = 0; j < h; j++)
	{
		if(trim_on[orb])
		{
			uint16_t row[240];
			memcpy(row, fb + (y + j) * 240 + x, (size_t)w * 2);
			lcd_trim(orb, row, (uint32_t)w);
			spi_write_blocking(LCD_SPI, (const uint8_t *)row, (size_t)w * 2);
		}
		else
			spi_write_blocking(LCD_SPI, (const uint8_t *)(fb + (y + j) * 240 + x), (size_t)w * 2);
	}
	gpio_put(cs_pin[orb], 1);
	mutex_exit(&bus);
}
