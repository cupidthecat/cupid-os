#ifndef CUPID_TLS_RECORD_H
#define CUPID_TLS_RECORD_H

#include "../types.h"

/* TLS 1.3 record layer (RFC 8446 §5).
 *
 * Cipher suite is hardcoded: TLS_CHACHA20_POLY1305_SHA256 — 32-byte
 * key, 12-byte static IV, 16-byte AEAD tag, sequence-number XOR'd
 * into the last 8 bytes of the IV per RFC 8446 §5.3.
 *
 * Transport is abstracted behind two callbacks so the record layer
 * neither depends on kernel/tcp.h nor on a particular fd type. The
 * callbacks must perform full sends/recvs (loop on partials internally
 * if necessary) and return 0 on connection drop or negative on error;
 * a positive value is treated as bytes transferred.
 */

#define TLS_REC_HEADER_SIZE         5u
#define TLS_REC_MAX_PLAINTEXT   16384u           /* 2^14 */
#define TLS_REC_MAX_CIPHERTEXT  (16384u + 256u)  /* §5.2 */
#define TLS_REC_BUF_SIZE        (TLS_REC_MAX_CIPHERTEXT + TLS_REC_HEADER_SIZE)

#define TLS_RT_INVALID            0
#define TLS_RT_CHANGE_CIPHER_SPEC 20
#define TLS_RT_ALERT              21
#define TLS_RT_HANDSHAKE          22
#define TLS_RT_APPLICATION_DATA   23

/* AEAD suite selector. Determines key length and the seal/open primitive. */
#define TLS_AEAD_CHACHA20_POLY1305  0
#define TLS_AEAD_AES_128_GCM        1

typedef int (*tls_xport_send_fn)(void *user, const uint8_t *buf, uint32_t len);
typedef int (*tls_xport_recv_fn)(void *user, uint8_t *buf, uint32_t len);

typedef struct {
    uint8_t  key[32];            /* up to 32 (chacha); only key_len bytes used */
    uint8_t  iv[12];
    uint64_t seq;
    uint8_t  active;             /* 1 = encrypt/decrypt, 0 = pass-through */
} tls_aead_dir_t;

typedef struct {
    void *xport_user;
    tls_xport_send_fn xport_send;
    tls_xport_recv_fn xport_recv;

    tls_aead_dir_t snd;
    tls_aead_dir_t rcv;

    /* Selected AEAD for both directions (TLS 1.3 picks one per session). */
    uint8_t  aead_alg;            /* TLS_AEAD_* */
    uint8_t  tls12;               /* 1 = TLS 1.2 wire format, 0 = 1.3 */

    /* Inbound assembly: bytes already pulled from transport but not yet
     * consumed by the record reader. We hold at most one full record. */
    uint8_t  in_buf[TLS_REC_BUF_SIZE];
    uint32_t in_len;
} tls_record_state_t;

void tls_record_init(tls_record_state_t *r,
                     void *xport_user,
                     tls_xport_send_fn s,
                     tls_xport_recv_fn rcb);

/* Pick AEAD suite for this connection. Must be called before any
 * tls_record_set_*_key (the suite determines the expected key length). */
void tls_record_set_aead(tls_record_state_t *r, uint8_t aead_alg);

/* Switch the record layer into TLS 1.2 wire format (different AAD
 * shape, plaintext does not carry a trailing inner type byte, AES-GCM
 * adds an 8-byte explicit nonce on the wire). */
void tls_record_set_tls12(tls_record_state_t *r);

/* Install AEAD send / recv keys. `key_len` must match the active suite
 * (32 for ChaCha20-Poly1305, 16 for AES-128-GCM). Resets the
 * corresponding seq to 0 and sets active=1. */
void tls_record_set_send_key(tls_record_state_t *r,
                             const uint8_t *key, uint32_t key_len,
                             const uint8_t iv[12]);
void tls_record_set_recv_key(tls_record_state_t *r,
                             const uint8_t *key, uint32_t key_len,
                             const uint8_t iv[12]);

/* Send `len` bytes of `data` as a single record of type `type`. If the
 * current direction is active the body is AEAD-sealed; otherwise it is
 * sent in the clear (used during handshake before the first
 * EncryptedExtensions message). `len` must be <= TLS_REC_MAX_PLAINTEXT.
 *
 * Returns 0 on success, -1 on transport error or oversize input. */
int tls_record_send(tls_record_state_t *r,
                    uint8_t type,
                    const uint8_t *data, uint32_t len);

/* Receive next record. On success returns 0 and sets *type_out + writes
 * the plaintext into buf (capacity buf_max). *len_out gets the
 * plaintext length. If the record direction is active, the inner
 * plaintext is unwrapped per RFC 8446 §5.2 (the trailing real type
 * byte goes to *type_out, padding zeros are stripped).
 *
 * Returns -1 on transport drop, -2 on parse / size error, -3 on AEAD
 * tag mismatch. */
int tls_record_recv(tls_record_state_t *r,
                    uint8_t *type_out,
                    uint8_t *buf, uint32_t buf_max,
                    uint32_t *len_out);

#endif
