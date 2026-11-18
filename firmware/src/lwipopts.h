/* lwIP settings for the orbs' XL (2026-11-16): the Pico SDK examples' values plus the network time client (SNTP, pool.ntp.org by default). */
#pragma once
#define NO_SYS 1
#define LWIP_SOCKET 0
#define MEM_LIBC_MALLOC 0                  /* (threadsafe_background: no malloc from interrupts) */
#define MEM_ALIGNMENT 4
#define MEM_SIZE (88 * 1024)                 /* (also mbedTLS's memory since 2026-11-17: mbedtls_config.h; its peak 54 KB measured) */
#define ALTCP_MBEDTLS_PLATFORM_ALLOC_STATS 1  /* (its most in use: "?" PICS tls_peak) */
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_ARP_QUEUE 10
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 16)   /* (SNTP, DNS retries, TLS, mDNS: room to spare) */
#define PBUF_POOL_SIZE 32
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define TCP_MSS 1460
/* bigger than one 16 KB TLS record (2026-11-16: at 8 x MSS = 11.7 KB the first record of OpenWeatherMap's answer could
   never arrive whole, so nothing was ever decrypted: the fetch hung after the request) */
#define TCP_WND (16 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETCONN 0
#define MEM_STATS 0
#define SYS_STATS 0
#define MEMP_STATS 0
#define LINK_STATS 0
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_DHCP 1
#define LWIP_IPV4 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
/* HTTPS (src/https.c): lwIP's TLS layer on mbedTLS; the server's certificate MUST check out (lwIP's default only
   warns) */
#define LWIP_ALTCP 1
#define LWIP_ALTCP_TLS 1
#define LWIP_ALTCP_TLS_MBEDTLS 1
#define ALTCP_MBEDTLS_AUTHMODE MBEDTLS_SSL_VERIFY_REQUIRED
#define MEMP_NUM_TCP_PCB 16                    /* (the settings page's browsers too, 2026-11-17) */
/* network time */
#define SNTP_SERVER_DNS 1
#define SNTP_STARTUP_DELAY 0
#define SNTP_UPDATE_DELAY (3600 * 1000)       /* again every hour */
/* three pool names, 3 s each (2026-11-16: after a load the first asks got no answer for 70 s; lwIP asked the
   same address again and again, 15 s apart). Which ones answer shows in "?" (ntp=) */
#define SNTP_MAX_SERVERS 3
#define SNTP_RECV_TIMEOUT 3000
#define SNTP_MONITOR_SERVER_REACHABILITY 1
#ifdef __cplusplus
extern "C" {
#endif
void orbs_sntp_set(unsigned long sec, unsigned long us);
#ifdef __cplusplus
}
#endif
#define SNTP_SET_SYSTEM_TIME_US(sec, us) orbs_sntp_set((unsigned long)(sec), (unsigned long)(us))
/* the settings page's address on the home network: http://info-orbs.local (mDNS, 2026-11-17) */
#define LWIP_MDNS_RESPONDER 1
#define LWIP_IGMP 1
#define LWIP_NUM_NETIF_CLIENT_DATA 1
#define MDNS_MAX_SERVICES 1
#define MEMP_NUM_UDP_PCB 8
