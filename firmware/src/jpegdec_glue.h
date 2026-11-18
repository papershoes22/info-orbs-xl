/* JPEGDEC for the second core (jpegdec_glue.c) */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* a decoded block of rows (native RGB565); return 0 to stop */
typedef int (*orbs_jd_draw_fn)(int x, int y, int w, int h, uint16_t *px, void *user);
int orbs_jd_open(const uint8_t *jpg, uint32_t len, orbs_jd_draw_fn fn, void *user, int *w, int *h, int *mcu_h);
int orbs_jd_decode(void);            /* 1 = OK (and it's closed) */
void orbs_jd_decode_abandon(void);   /* opened but not decoded */
int orbs_jd_error(void);
#ifdef __cplusplus
}
#endif
