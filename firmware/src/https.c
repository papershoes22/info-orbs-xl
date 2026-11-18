#include "https.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "mbedtls/ssl.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/sha256.h"
#include "psa/crypto.h"
#include "orbs_time.h"
#include "ca_roots.h"

#define TIMEOUT_MS 20000

static struct altcp_tls_config *tls_config;
static struct altcp_pcb *pcb;
static volatile https_state_t state = HTTPS_IDLE;
static char host_s[64], req[1024], err_s[128];
static uint16_t port_n = 443;
static uint8_t pin[32];
static bool pinned;
/* a kept-open connection (2026-11-17, Frigate's pictures one after another): HTTP/1.1, the answer's end found by its
   Content-Length, the next request to the same server goes down the same connection (no new TLS handshake: 0.4 s each) */
static bool keep, kept, reused;
static char kept_host[64];
static uint16_t kept_port;
static size_t hdr_len, content_len;           /* content_len 0 = not known (then the end is the server closing) */
static bool hdr_done;
static char *buf;
static size_t cap, len;
static int status;
static const char *body;
static size_t body_len;
static absolute_time_t started;
static unsigned took_ms;
static ip_addr_t addr;
/* how far the last one got (said in a failure) */
static volatile int stage;
static const char *const stage_name[] = { "looking up the name", "connecting (TCP + TLS handshake)", "request sent, waiting",
                                          "receiving" };

mbedtls_ms_time_t mbedtls_ms_time(void)
{
	return (mbedtls_ms_time_t)to_ms_since_boot(get_absolute_time());
}

static mbedtls_time_t orbs_mbedtls_time(mbedtls_time_t *t)
{
	mbedtls_time_t now = (mbedtls_time_t)orbs_time_now();
	if(t)
		*t = now;
	return now;
}

static void fail(const char *why)
{
	memset(req, 0, sizeof req);
	snprintf(err_s, sizeof err_s, "%s (stage: %s, %u bytes)", why, stage_name[stage], (unsigned)len);
	state = HTTPS_FAILED;
}

/* (lwIP context) */
static void close_pcb(void)
{
	if(!pcb)
		return;
	altcp_arg(pcb, NULL);
	altcp_recv(pcb, NULL);
	altcp_err(pcb, NULL);
	altcp_poll(pcb, NULL, 0);
	if(altcp_close(pcb) != ERR_OK)
		altcp_abort(pcb);
	pcb = NULL;
}

static void connect_now(void);

static void finish(void)
{
	memset(req, 0, sizeof req);
	buf[len < cap ? len : cap - 1] = 0;
	char *hdr_end = strstr(buf, "\r\n\r\n");
	if(strncmp(buf, "HTTP/1.", 7) != 0 || !hdr_end)
	{
		fail("no HTTP answer");
		return;
	}
	status = atoi(buf + 9);
	body = hdr_end + 4;
	body_len = len - (size_t)(body - buf);
	took_ms = (unsigned)(absolute_time_diff_us(started, get_absolute_time()) / 1000);
	state = HTTPS_DONE;
}

/* the server closed a kept connection: idle, it's just forgotten; mid-request with nothing back yet, the request goes
   again on a new connection (the server may close an idle one at any time) */
static bool kept_dropped(void)
{
	kept = false;
	if(state != HTTPS_BUSY)
		return true;
	if(reused && len == 0)
	{
		reused = false;
		connect_now();
		return true;
	}
	return false;
}

static err_t on_recv(void *arg, struct altcp_pcb *p, struct pbuf *pb, err_t err)
{
	(void)arg;
	if(!pb)
	{
		close_pcb();
		if(kept_dropped())
			return ERR_OK;
		finish();
		return ERR_OK;
	}
	if(state != HTTPS_BUSY)
	{
		altcp_recved(p, pb->tot_len);         /* (nothing asked for on an idle kept connection: dropped) */
		pbuf_free(pb);
		return ERR_OK;
	}
	stage = 3;
	for(struct pbuf *q = pb; q; q = q->next)
	{
		size_t n = q->len;
		if(len + n > cap - 1)
			n = cap - 1 > len ? cap - 1 - len : 0;
		memcpy(buf + len, q->payload, n);
		len += n;
	}
	altcp_recved(p, pb->tot_len);
	pbuf_free(pb);
	if(len >= cap - 1)
	{
		close_pcb();
		kept = false;
		fail("answer too big for the buffer");
		return ERR_OK;
	}
	if(keep && !hdr_done)
	{
		buf[len] = 0;
		char *e = strstr(buf, "\r\n\r\n");
		if(e)
		{
			hdr_done = true;
			hdr_len = (size_t)(e + 4 - buf);
			for(char *h = strstr(buf, "\r\n"); h && h < e; h = strstr(h + 2, "\r\n"))
				if(strncasecmp(h + 2, "Content-Length:", 15) == 0)
					content_len = (size_t)strtoul(h + 17, NULL, 10);
		}
	}
	if(keep && hdr_done && content_len && len >= hdr_len + content_len)
	{
		kept = true;                          /* the whole answer: the connection stays for the next one */
		snprintf(kept_host, sizeof kept_host, "%s", host_s);
		kept_port = port_n;
		finish();
	}
	return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
	(void)arg;
	pcb = NULL;                                   /* (already freed by lwIP) */
	if(kept_dropped())
		return;
	if(state == HTTPS_BUSY)
	{
		char why[80];
		snprintf(why, sizeof why, "connection failed (lwIP error %d)", (int)err);
		fail(why);
	}
}

static err_t on_poll(void *arg, struct altcp_pcb *p)
{
	(void)arg;
	(void)p;
	if(state == HTTPS_BUSY && absolute_time_diff_us(started, get_absolute_time()) > TIMEOUT_MS * 1000LL)
	{
		close_pcb();
		fail("no answer in 20 s");
	}
	return ERR_OK;
}

static err_t on_connected(void *arg, struct altcp_pcb *p, err_t err)
{
	(void)arg;
	if(err != ERR_OK)
	{
		close_pcb();
		fail("couldn't connect");
		return ERR_OK;
	}
	stage = 2;
	err_t e = altcp_write(p, req, (u16_t)strlen(req), TCP_WRITE_FLAG_COPY);
	memset(req, 0, sizeof req);                   /* (the path held the key; a login request, the password) */
	if(e == ERR_OK)
		e = altcp_output(p);
	if(e != ERR_OK)
	{
		close_pcb();
		fail("couldn't send the request");
	}
	return ERR_OK;
}

/* a pinned server (Frigate, with its own self-signed certificate): trusted only if its certificate is exactly the one
   whose SHA-256 the rig stored (camlogin.h); then nothing else about it counts (who signed it, the name in it) */
static int verify_pin(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
	(void)ctx;
	if(depth != 0)
		return 0;
	uint8_t h[32];
	if(mbedtls_sha256(crt->raw.p, crt->raw.len, h, 0) == 0 && memcmp(h, pin, 32) == 0)
		*flags = 0;
	else
		*flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
	return 0;
}

static void connect_now(void)
{
	stage = 1;
	kept = false;
	pcb = altcp_tls_new(tls_config, IPADDR_TYPE_V4);
	if(!pcb)
	{
		fail("out of memory for TLS");
		return;
	}
	mbedtls_ssl_set_hostname(altcp_tls_context(pcb), host_s);   /* the name sent (SNI) and checked on the certificate */
	if(pinned)
		mbedtls_ssl_set_verify(altcp_tls_context(pcb), verify_pin, NULL);
	altcp_arg(pcb, NULL);
	altcp_recv(pcb, on_recv);
	altcp_err(pcb, on_err);
	altcp_poll(pcb, on_poll, 2);
	if(altcp_connect(pcb, &addr, port_n, on_connected) != ERR_OK)
	{
		close_pcb();
		fail("couldn't start the connection");
	}
}

static void on_dns(const char *name, const ip_addr_t *a, void *arg)
{
	(void)name;
	(void)arg;
	if(!a)
	{
		fail("the server's name didn't resolve (DNS)");
		return;
	}
	addr = *a;
	connect_now();
}

bool https_request(const https_req_t *r, char *b, size_t c)
{
	if(state == HTTPS_BUSY)
		return false;
	if(!tls_config)
	{
		mbedtls_platform_set_time(orbs_mbedtls_time);
		/* the config first: making it points mbedTLS's memory at lwIP's heap, so the PSA set-up after it uses that too
		   (all of it under lwIP's lock) */
		cyw43_arch_lwip_begin();
		tls_config = altcp_tls_create_config_client((const u8_t *)ca_roots_pem, sizeof ca_roots_pem);
		bool psa_ok = tls_config && psa_crypto_init() == PSA_SUCCESS;   /* (TLS 1.3's key schedule runs on PSA crypto) */
		cyw43_arch_lwip_end();
		if(!tls_config)
		{
			fail("the root certificates didn't load");
			return true;
		}
		if(!psa_ok)
		{
			fail("the crypto library didn't start");
			return true;
		}
	}
	bool same = kept && pcb && strcmp(kept_host, r->host) == 0 && kept_port == (r->port ? r->port : 443) && r->keep;
	if(!same && pcb)
	{
		cyw43_arch_lwip_begin();                 /* (a kept connection to another server: closed first, one at a time) */
		close_pcb();
		cyw43_arch_lwip_end();
		kept = false;
	}
	snprintf(host_s, sizeof host_s, "%s", r->host);
	port_n = r->port ? r->port : 443;
	pinned = r->pin != NULL;
	if(pinned)
		memcpy(pin, r->pin, 32);
	keep = r->keep;
	const char *ver = keep ? "HTTP/1.1" : "HTTP/1.0", *conn = keep ? "keep-alive" : "close";
	int n;
	if(r->body)
		n = snprintf(req, sizeof req, "%s %s %s\r\nHost: %s\r\nUser-Agent: info-orbs-xl\r\nConnection: %s\r\n%s"
		             "Content-Type: application/json\r\nContent-Length: %u\r\n\r\n%s", r->method ? r->method : "POST", r->path, ver,
		             r->host, conn, r->headers ? r->headers : "", (unsigned)strlen(r->body), r->body);
	else
		n = snprintf(req, sizeof req, "%s %s %s\r\nHost: %s\r\nUser-Agent: info-orbs-xl\r\nConnection: %s\r\n%s\r\n",
		             r->method ? r->method : "GET", r->path, ver, r->host, conn, r->headers ? r->headers : "");
	if(n >= (int)sizeof req)
	{
		memset(req, 0, sizeof req);
		fail("request too long");
		return true;
	}
	buf = b;
	cap = c;
	len = 0;
	status = 0;
	body = NULL;
	body_len = 0;
	err_s[0] = 0;
	stage = 0;
	hdr_done = false;
	hdr_len = content_len = 0;
	started = get_absolute_time();
	state = HTTPS_BUSY;
	if(same)
	{
		/* down the kept connection */
		reused = true;
		stage = 2;
		cyw43_arch_lwip_begin();
		err_t e = altcp_write(pcb, req, (u16_t)strlen(req), TCP_WRITE_FLAG_COPY);
		if(e == ERR_OK)
			e = altcp_output(pcb);
		/* (req stays until the answer ends: if the server had already dropped the connection, it goes again) */
		if(e != ERR_OK)
		{
			close_pcb();                          /* (it went away meanwhile: a new connection) */
			reused = false;
			connect_now();
		}
		cyw43_arch_lwip_end();
		return true;
	}
	reused = false;
	cyw43_arch_lwip_begin();
	err_t e = dns_gethostbyname(host_s, &addr, on_dns, NULL);
	if(e == ERR_OK)
		connect_now();
	else if(e != ERR_INPROGRESS)
		fail("DNS lookup couldn't start");
	cyw43_arch_lwip_end();
	return true;
}

bool https_start(const char *host, const char *path, char *b, size_t c)
{
	https_req_t r = { host, 443, "GET", path, NULL, NULL, NULL };
	return https_request(&r, b, c);
}

/* a header of the last answer (its value, up to the line's end), NULL if it isn't there */
const char *https_header(const char *name, size_t *vlen)
{
	if(state != HTTPS_DONE || !body)
		return NULL;
	size_t nl = strlen(name);
	for(const char *p = strstr(buf, "\r\n"); p && p + 2 < body; p = strstr(p + 2, "\r\n"))
	{
		const char *h = p + 2;
		if(strncasecmp(h, name, nl) == 0 && h[nl] == ':')
		{
			h += nl + 1;
			while(*h == ' ')
				h++;
			const char *e = strstr(h, "\r\n");
			if(vlen)
				*vlen = e ? (size_t)(e - h) : strlen(h);
			return h;
		}
	}
	return NULL;
}

https_state_t https_poll(void)
{
	if(state == HTTPS_BUSY && absolute_time_diff_us(started, get_absolute_time()) > (TIMEOUT_MS + 5000) * 1000LL)
	{
		cyw43_arch_lwip_begin();
		close_pcb();
		cyw43_arch_lwip_end();
		fail("no answer in 25 s");
	}
	return state;
}

int https_status(void) { return status; }
const char *https_body(size_t *l) { if(l) *l = body_len; return body; }
const char *https_error(void) { return err_s; }
unsigned https_ms(void) { return took_ms; }
void https_reset(void) { if(state != HTTPS_BUSY) state = HTTPS_IDLE; }

void https_close(void)
{
	if(state == HTTPS_BUSY)
		return;
	cyw43_arch_lwip_begin();
	close_pcb();
	cyw43_arch_lwip_end();
	kept = false;
}

bool https_kept(void) { return kept && pcb; }
