/* mbedTLS 3.6 for the orbs' XL (2026-11-16): a TLS 1.2 client, for HTTPS to api.openweathermap.org; TLS 1.3 too since
   2026-11-17 (Frigate's port 8971 answers TLS 1.3 only: "alert protocol version" to 1.2; 1.3 needs mbedTLS's
   PSA crypto, started in https.c).
   ECDHE with RSA or ECDSA certificates, AES-GCM, SHA-2; certificates checked against the roots in src/ca_roots.h
   (lwipopts.h makes the check required); the time for certificate dates comes from the XL's network clock. */
#pragma once
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_PLATFORM_C
/* mbedTLS takes its memory from lwIP's own heap (altcp_tls_mbedtls_mem.c switches that on when this is set), never
   from malloc: the TLS work runs in the network's interrupt, and malloc there while the main loop was inside malloc
   (the clock's fonts) deadlocked on malloc's lock: the watchdog restarts ~10 s after start-up (2026-11-16,
   2026-11-17 twice; last step 40 = drawing the Clock). */
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_TIME_ALT              /* mbedtls_platform_set_time(): orbs_time_now() (https.c) */
#define MBEDTLS_PLATFORM_MS_TIME_ALT           /* mbedtls_ms_time(): milliseconds since start-up (https.c) */
#define MBEDTLS_ENTROPY_HARDWARE_ALT           /* the RP2350's random source (pico_mbedtls.c, pico_rand) */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_AES_C
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_TLS1_3
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED
#define MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE
#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_HKDF_C
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE        /* (TLS 1.3 needs it; it keeps the server certificate for the handshake) */
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT          /* (RSA-PSS signatures: TLS 1.3 needs them for RSA certificates, OpenWeatherMap's and Frigate's) */
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384       /* (a server may send full 16 KB records) */
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096
#define MBEDTLS_ERROR_C
/* lwIP's TLS layer (the SDK's altcp_tls_mbedtls.c) reads a few of mbedTLS's internal fields, as the SDK's examples allow */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
