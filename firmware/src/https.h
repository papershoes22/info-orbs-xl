/* One HTTPS request at a time from the XL (2026-11-16): lwIP's TLS layer with mbedTLS, the server's certificate required to
   chain to one of src/ca_roots.h and to match the host name. Started and polled from the main loop; the network
   callbacks only fill the buffer. The request's path (which holds the weather key) is never printed. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { HTTPS_IDLE, HTTPS_BUSY, HTTPS_DONE, HTTPS_FAILED } https_state_t;

bool https_start(const char *host, const char *path, char *buf, size_t cap);   /* false if one is already running */
/* the general form (2026-11-17, Frigate): another port, POST with a JSON body, extra header lines (each ending "\r\n"),
   and a pinned server: pin = the SHA-256 of the one certificate to trust instead of the root certificates */
#include <stdint.h>
typedef struct {
	const char *host;
	uint16_t port;
	const char *method, *path, *headers, *body;
	const uint8_t *pin;
	bool keep;                             /* HTTP/1.1, the connection kept for the next request to the same server */
} https_req_t;
bool https_request(const https_req_t *r, char *buf, size_t cap);
const char *https_header(const char *name, size_t *len);   /* a header of the last answer, NULL if none */
https_state_t https_poll(void);            /* main loop; DONE / FAILED stay until https_start or https_reset */
int https_status(void);                    /* the HTTP status code (200, 401, ...), 0 if none */
const char *https_body(size_t *len);       /* the body (NUL-terminated) once DONE */
const char *https_error(void);             /* why it FAILED, in plain words */
unsigned https_ms(void);                   /* how long the last one took */
void https_reset(void);
void https_close(void);                    /* closes a kept connection (not while a request runs) */
bool https_kept(void);

#ifdef __cplusplus
}
#endif
