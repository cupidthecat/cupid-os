#ifndef CUPID_TLS_CTX_H
#define CUPID_TLS_CTX_H

#include "types.h"
#include "sha256.h"
#include "tls_record.h"
#include "x509_chain.h"

#define TLS_CTX_MAX_HOSTNAME 256u
#define TLS_CTX_CERT_BUF     16384u

/* TLS context - one per active connection. Allocated by socket_setsockopt
 * via kmalloc; zero-initialized; carries everything the handshake and
 * the application data path need.*/

#define TLS_ERR_OK                0
#define TLS_ERR_TRANSPORT        -1
#define TLS_ERR_PARSE            -2
#define TLS_ERR_BAD_RECORD       -3
#define TLS_ERR_BAD_TAG          -4
#define TLS_ERR_PROTOCOL         -5
#define TLS_ERR_CERT             -6
#define TLS_ERR_FINISHED_MAC     -7
#define TLS_ERR_BAD_SIGNATURE    -8
#define TLS_ERR_NO_ENTROPY       -9
#define TLS_ERR_OOM             -10
#define TLS_ERR_HANDSHAKE_STATE -11

typedef struct {
    int state;            /* opaque to callers; see tls_handshake.c */
    int last_error;

    /* Record layer holds keys + xport callbacks. */
    tls_record_state_t rec;

    /* Negotiated parameters (set after ServerHello). */
    uint16_t selected_cipher;     /* 0x1301=AES_128_GCM, 0x1303=CHACHA20 */
    uint16_t selected_group;      /* 0x001D=X25519, 0x0017=secp256r1 */
    uint16_t selected_sigalg;     /* 0x0804=rsa_pss_rsae_sha256, 0x0403=ecdsa_p256_sha256 */

    /* X25519 ephemeral keypair + shared secret. */
    uint8_t client_priv[32];
    uint8_t client_pub[32];
    uint8_t ecdhe_shared[32];

    /* P-256 ephemeral keypair (only the 32-byte X coordinate of the
     * shared secret is used as the input to HKDF - same as X25519).*/
    uint8_t  p256_priv[32];
    uint8_t  p256_pub[65];        /* 0x04 || X || Y, SEC1 uncompressed */

    /* Random nonces. */
    uint8_t client_random[32];
    uint8_t server_random[32];

    /* Running transcript hash. */
    sha256_ctx_t transcript;

    /* Schedule. */
    uint8_t early_secret[32];
    uint8_t handshake_secret[32];
    uint8_t master_secret[32];
    uint8_t c_hs_traffic[32];
    uint8_t s_hs_traffic[32];
    uint8_t c_ap_traffic[32];
    uint8_t s_ap_traffic[32];

    /* Snapshot of transcript hash at "after Certificate" (used by
     * CertificateVerify) and "after server Finished".*/
    uint8_t th_before_cert_verify[32];
    uint8_t th_before_server_finished[32];
    uint8_t th_after_server_finished[32];

    /* Server cert chain. The DER bytes for each cert remain inside
     * cert_buf; x509_chain_t holds spans into it.*/
    x509_chain_t chain;
    uint8_t  cert_buf[TLS_CTX_CERT_BUF];
    uint32_t cert_buf_used;

    /* Server hostname (NUL-terminated). */
    char hostname[TLS_CTX_MAX_HOSTNAME];
    uint32_t hostname_len;

    /* RTC epoch for cert validity. */
    uint64_t now_epoch;

    /* App-data spillover. A single TLS 1.3 record can carry up to 16 KB
     * of plaintext but callers typically pass small recv buffers (4 KB).
     * tls_app_recv decrypts into app_buf, returns up to buf_max bytes,
     * and stashes the rest here for the next call.*/
    uint8_t  app_buf[TLS_REC_MAX_PLAINTEXT];
    uint32_t app_buf_off;
    uint32_t app_buf_len;
} tls_ctx_t;

/* Initialize an already-allocated context. xport callbacks send/recv
 * over TCP. `hostname` is NUL-terminated. `now` is current Unix epoch
 * for validity checks.*/
int tls_ctx_init(tls_ctx_t *ctx,
                 void *xport_user,
                 tls_xport_send_fn s,
                 tls_xport_recv_fn rcb,
                 const char *hostname,
                 uint64_t now_epoch);

/* Drive a full TLS 1.3 client handshake to completion. Returns
 * TLS_ERR_OK on success or a TLS_ERR_* / X509_ERR_* (negative) on
 * failure. ctx->last_error is also set.*/
int tls_handshake_client(tls_ctx_t *ctx);

/* Read decrypted application data into buf (max buf_max bytes).
 * Returns the byte count, 0 on close_notify, or TLS_ERR_* (<0).*/
int tls_app_recv(tls_ctx_t *ctx, uint8_t *buf, uint32_t buf_max);

/* Send application data. Returns 0 on success, TLS_ERR_* on failure. */
int tls_app_send(tls_ctx_t *ctx, const uint8_t *buf, uint32_t len);

/* Send a close_notify alert. Best-effort. */
void tls_close_notify(tls_ctx_t *ctx);

/* Wipe all secrets and zero the context. */
void tls_ctx_destroy(tls_ctx_t *ctx);

#endif
