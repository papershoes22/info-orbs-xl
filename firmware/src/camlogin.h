/* The login the XL uses to fetch the cameras straight from Frigate when the rig isn't there (step 4, 2026-11-17; best a
   read-only viewer account). Kept in its own flash sector (the one before the settings), with
   Frigate's address and the SHA-256 of its certificate (Frigate's own self-signed one: the XL trusts exactly that
   certificate and no other). Set over USB by the PC (`tools/orbs_xl.py frigate`); the password is never printed or sent back. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
	uint32_t magic, version;
	char host[40];                 /* e.g. "192.168.1.50" */
	uint16_t port;                 /* 8971 */
	uint8_t pad[2];
	char user[32];
	char pass[64];
	uint8_t pin[32];               /* SHA-256 of the server certificate (DER) */
	uint32_t crc;
} camlogin_t;
extern camlogin_t camlogin;
void camlogin_load(void);
bool camlogin_set(void);         /* a login is there */
bool camlogin_save(void);        /* (the caller makes sure no picture DMA reads PSRAM meanwhile: pics_settle) */
void camlogin_wipe(void);
#ifdef __cplusplus
}
#endif
