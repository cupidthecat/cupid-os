/* TLS 1.3 record layer.
 *
 * Send: build TLSPlaintext (cleartext) or TLSCiphertext (encrypted),
 *       hand to xport_send.
 * Recv: pull header (5 bytes), then body length, then either return
 *       cleartext or AEAD-open. RFC 8446 §5.
 *
 * Wire format reminders:
 *   Cleartext:  type|0x0303|len(BE)|fragment
 *   Encrypted:  0x17|0x0303|len(BE)|aead(plaintext, type, padding)
 *               where the AEAD nonce is static_iv XOR seq (last 8 bytes)
 *               and AAD is the 5-byte record header.
*/

#include "tls_record.h"
#include "chacha20poly1305.h"
#include "aes_gcm.h"
#include "ct.h"

static uint32_t aead_key_len(uint8_t alg) {
    return (alg == TLS_AEAD_AES_128_GCM) ? 16u : 32u;
}

void tls_record_init(tls_record_state_t *r,
                     void *xport_user,
                     tls_xport_send_fn s,
                     tls_xport_recv_fn rcb) {
    uint32_t i;
    uint8_t *q = (uint8_t *)r;
    for (i = 0; i < sizeof(*r); i++) q[i] = 0u;
    r->xport_user = xport_user;
    r->xport_send = s;
    r->xport_recv = rcb;
}

static void wipe_dir(tls_aead_dir_t *d) {
    ct_wipe(d->key, sizeof(d->key));
    ct_wipe(d->iv,  sizeof(d->iv));
    d->seq = 0;
    d->active = 0;
}

void tls_record_set_aead(tls_record_state_t *r, uint8_t aead_alg) {
    r->aead_alg = aead_alg;
}

void tls_record_set_tls12(tls_record_state_t *r) {
    r->tls12 = 1u;
}

void tls_record_set_send_key(tls_record_state_t *r,
                             const uint8_t *key, uint32_t key_len,
                             const uint8_t iv[12]) {
    uint32_t i;
    wipe_dir(&r->snd);
    for (i = 0; i < key_len && i < sizeof(r->snd.key); i++) r->snd.key[i] = key[i];
    for (i = 0; i < 12u; i++) r->snd.iv[i]  = iv[i];
    r->snd.active = 1u;
    r->snd.seq    = 0;
}

void tls_record_set_recv_key(tls_record_state_t *r,
                             const uint8_t *key, uint32_t key_len,
                             const uint8_t iv[12]) {
    uint32_t i;
    wipe_dir(&r->rcv);
    for (i = 0; i < key_len && i < sizeof(r->rcv.key); i++) r->rcv.key[i] = key[i];
    for (i = 0; i < 12u; i++) r->rcv.iv[i]  = iv[i];
    r->rcv.active = 1u;
    r->rcv.seq    = 0;
}

/* Compose AEAD nonce: static_iv XOR right-aligned seq. RFC 8446 §5.3. */
static void compose_nonce(const uint8_t iv[12], uint64_t seq,
                          uint8_t out[12]) {
    int i;
    for (i = 0; i < 4; i++) out[i] = iv[i];
    for (i = 0; i < 8; i++) {
        uint8_t s = (uint8_t)((seq >> (8 * (7 - i))) & 0xFFu);
        out[4 + i] = (uint8_t)(iv[4 + i] ^ s);
    }
}

/* Read full from transport */

static int xport_full_recv(tls_record_state_t *r,
                           uint8_t *buf, uint32_t want) {
    uint32_t got = 0;
    while (got < want) {
        int n = r->xport_recv(r->xport_user, buf + got, want - got);
        if (n <= 0) return -1;
        got += (uint32_t)n;
    }
    return 0;
}

static int xport_full_send(tls_record_state_t *r,
                           const uint8_t *buf, uint32_t len) {
    uint32_t off = 0;
    while (off < len) {
        int n = r->xport_send(r->xport_user, buf + off, len - off);
        if (n <= 0) return -1;
        off += (uint32_t)n;
    }
    return 0;
}

/* Send */

/* TLS 1.2 encrypted send path. Different from 1.3 in three ways:
 *   - outer header type carries the real content type;
 *   - plaintext has NO trailing inner-type byte;
 *   - AAD is seq(8) | type(1) | version(2) | pt_len(2) (RFC 5246 §6.2.3.3);
 *   - AES-GCM puts an 8-byte explicit nonce on the wire (RFC 5288 §3);
 *     ChaCha20-Poly1305 reuses the 1.3-style XOR nonce (RFC 7905 §2).*/
static int tls12_send_encrypted(tls_record_state_t *r,
                                uint8_t type,
                                const uint8_t *data, uint32_t len) {
    uint8_t hdr[TLS_REC_HEADER_SIZE];
    uint8_t out[TLS_REC_BUF_SIZE];
    uint8_t aad[13];
    uint8_t nonce[12];
    uint8_t tag[16];
    uint32_t i;
    uint32_t outer_len;
    uint32_t off;

    if (len > TLS_REC_MAX_PLAINTEXT) return -1;

    /* AAD = seq | type | 0x0303 | pt_len */
    for (i = 0; i < 8u; i++) {
        aad[i] = (uint8_t)((r->snd.seq >> (8u * (7u - i))) & 0xFFu);
    }
    aad[8]  = type;
    aad[9]  = 0x03;
    aad[10] = 0x03;
    aad[11] = (uint8_t)((len >> 8) & 0xFFu);
    aad[12] = (uint8_t)(len & 0xFFu);

    if (r->aead_alg == TLS_AEAD_AES_128_GCM) {
        /* explicit nonce = seq num (counter). */
        for (i = 0; i < 4u; i++) nonce[i] = r->snd.iv[i];
        for (i = 0; i < 8u; i++) nonce[4 + i] = aad[i];
        outer_len = 8u + len + 16u;
    } else {
        for (i = 0; i < 4u; i++) nonce[i] = r->snd.iv[i];
        for (i = 0; i < 8u; i++) nonce[4 + i] = (uint8_t)(r->snd.iv[4 + i] ^ aad[i]);
        outer_len = len + 16u;
    }

    hdr[0] = type;
    hdr[1] = 0x03; hdr[2] = 0x03;
    hdr[3] = (uint8_t)((outer_len >> 8) & 0xFFu);
    hdr[4] = (uint8_t)(outer_len & 0xFFu);

    /* Layout: [hdr][?explicit_nonce(8)][ct][tag] */
    for (i = 0; i < TLS_REC_HEADER_SIZE; i++) out[i] = hdr[i];
    off = TLS_REC_HEADER_SIZE;
    if (r->aead_alg == TLS_AEAD_AES_128_GCM) {
        for (i = 0; i < 8u; i++) out[off + i] = nonce[4 + i];
        off += 8u;
    }

    if (r->aead_alg == TLS_AEAD_AES_128_GCM) {
        aes128_gcm_seal(r->snd.key, nonce,
                        aad, sizeof(aad),
                        data, len,
                        out + off, tag);
    } else {
        chacha20poly1305_seal(r->snd.key, nonce,
                              aad, sizeof(aad),
                              data, len,
                              out + off, tag);
    }
    for (i = 0; i < 16u; i++) out[off + len + i] = tag[i];

    r->snd.seq++;
    return xport_full_send(r, out, TLS_REC_HEADER_SIZE + outer_len);
}

int tls_record_send(tls_record_state_t *r,
                    uint8_t type,
                    const uint8_t *data, uint32_t len) {
    uint8_t hdr[TLS_REC_HEADER_SIZE];
    uint8_t out[TLS_REC_BUF_SIZE];
    uint32_t pt_len;
    uint32_t ct_len;
    uint32_t i;

    if (r->snd.active && r->tls12) {
        return tls12_send_encrypted(r, type, data, len);
    }
    if (r->snd.active) {
        /* Inner plaintext = data || real_type || pad(0). No padding for v1. */
        pt_len = len + 1u;
        if (pt_len > TLS_REC_MAX_PLAINTEXT) return -1;
        ct_len = pt_len + 16u;
        if (ct_len > TLS_REC_MAX_CIPHERTEXT) return -1;

        /* Outer header: opaque type = application_data, version 0x0303. */
        hdr[0] = TLS_RT_APPLICATION_DATA;
        hdr[1] = 0x03; hdr[2] = 0x03;
        hdr[3] = (uint8_t)((ct_len >> 8) & 0xFFu);
        hdr[4] = (uint8_t)(ct_len & 0xFFu);

        {
            uint8_t inner[TLS_REC_MAX_PLAINTEXT];
            uint8_t nonce[12];
            uint8_t tag[16];

            for (i = 0; i < len; i++) inner[i] = data[i];
            inner[len] = type;

            compose_nonce(r->snd.iv, r->snd.seq, nonce);

            if (r->aead_alg == TLS_AEAD_AES_128_GCM) {
                aes128_gcm_seal(r->snd.key, nonce,
                                hdr, TLS_REC_HEADER_SIZE,
                                inner, pt_len,
                                out + TLS_REC_HEADER_SIZE, tag);
            } else {
                chacha20poly1305_seal(r->snd.key, nonce,
                                      hdr, TLS_REC_HEADER_SIZE,
                                      inner, pt_len,
                                      out + TLS_REC_HEADER_SIZE,
                                      tag);
            }
            (void)aead_key_len;
            for (i = 0; i < TLS_REC_HEADER_SIZE; i++) out[i] = hdr[i];
            for (i = 0; i < 16u; i++)
                out[TLS_REC_HEADER_SIZE + pt_len + i] = tag[i];

            r->snd.seq++;
            ct_wipe(inner, len + 1u);
        }

        return xport_full_send(r, out, TLS_REC_HEADER_SIZE + ct_len);
    }

    /* Cleartext path. */
    if (len > TLS_REC_MAX_PLAINTEXT) return -1;
    hdr[0] = type;
    hdr[1] = 0x03; hdr[2] = 0x03;
    hdr[3] = (uint8_t)((len >> 8) & 0xFFu);
    hdr[4] = (uint8_t)(len & 0xFFu);
    if (xport_full_send(r, hdr, TLS_REC_HEADER_SIZE) != 0) return -1;
    if (len > 0u) {
        if (xport_full_send(r, data, len) != 0) return -1;
    }
    return 0;
}

/* Recv */

/* TLS 1.2 encrypted recv. Outer type is the real content type; AAD
 * shape and explicit-nonce handling per the comment above the send
 * function.*/
static int tls12_recv_encrypted(tls_record_state_t *r,
                                uint8_t type, uint32_t body_len,
                                uint8_t *type_out,
                                uint8_t *buf, uint32_t buf_max,
                                uint32_t *len_out) {
    uint8_t  cipher[TLS_REC_MAX_CIPHERTEXT];
    uint8_t  aad[13];
    uint8_t  nonce[12];
    uint32_t pt_len;
    uint32_t off = 0;
    uint32_t i;
    int      ok;

    if (body_len > TLS_REC_MAX_CIPHERTEXT) return -2;
    if (xport_full_recv(r, cipher, body_len) != 0) return -1;

    if (r->aead_alg == TLS_AEAD_AES_128_GCM) {
        if (body_len < 8u + 16u) return -2;
        for (i = 0; i < 4u; i++) nonce[i] = r->rcv.iv[i];
        for (i = 0; i < 8u; i++) nonce[4 + i] = cipher[i];
        off = 8u;
        pt_len = body_len - 8u - 16u;
    } else {
        if (body_len < 16u) return -2;
        for (i = 0; i < 8u; i++) {
            uint8_t s = (uint8_t)((r->rcv.seq >> (8u * (7u - i))) & 0xFFu);
            nonce[4 + i] = (uint8_t)(r->rcv.iv[4 + i] ^ s);
        }
        for (i = 0; i < 4u; i++) nonce[i] = r->rcv.iv[i];
        pt_len = body_len - 16u;
    }
    if (pt_len > buf_max) return -2;

    for (i = 0; i < 8u; i++) {
        aad[i] = (uint8_t)((r->rcv.seq >> (8u * (7u - i))) & 0xFFu);
    }
    aad[8]  = type;
    aad[9]  = 0x03;
    aad[10] = 0x03;
    aad[11] = (uint8_t)((pt_len >> 8) & 0xFFu);
    aad[12] = (uint8_t)(pt_len & 0xFFu);

    if (r->aead_alg == TLS_AEAD_AES_128_GCM) {
        ok = aes128_gcm_open(r->rcv.key, nonce,
                             aad, sizeof(aad),
                             cipher + off, pt_len,
                             cipher + off + pt_len,
                             buf);
    } else {
        ok = chacha20poly1305_open(r->rcv.key, nonce,
                                   aad, sizeof(aad),
                                   cipher + off, pt_len,
                                   cipher + off + pt_len,
                                   buf);
    }
    ct_wipe(cipher, sizeof(cipher));
    if (!ok) return -3;
    r->rcv.seq++;
    *type_out = type;
    *len_out  = pt_len;
    return 0;
}

int tls_record_recv(tls_record_state_t *r,
                    uint8_t *type_out,
                    uint8_t *buf, uint32_t buf_max,
                    uint32_t *len_out) {
    uint8_t  hdr[TLS_REC_HEADER_SIZE];
    uint8_t  type;
    uint32_t body_len;
    uint8_t  cipher[TLS_REC_MAX_CIPHERTEXT];
    uint32_t i;

    if (xport_full_recv(r, hdr, TLS_REC_HEADER_SIZE) != 0) return -1;
    type = hdr[0];
    body_len = ((uint32_t)hdr[3] << 8) | (uint32_t)hdr[4];

    if (r->rcv.active && r->tls12) {
        if (type == TLS_RT_CHANGE_CIPHER_SPEC) {
            /* CCS arrives in cleartext even after switch - return as-is. */
            if (body_len > buf_max) return -2;
            if (body_len > 0u) {
                if (xport_full_recv(r, buf, body_len) != 0) return -1;
            }
            *type_out = type;
            *len_out  = body_len;
            return 0;
        }
        return tls12_recv_encrypted(r, type, body_len,
                                    type_out, buf, buf_max, len_out);
    }
    if (r->rcv.active && type == TLS_RT_APPLICATION_DATA) {
        uint8_t  nonce[12];
        uint32_t pt_len;
        int      ok;
        uint8_t  inner_type;

        if (body_len < 16u) return -2;
        if (body_len > TLS_REC_MAX_CIPHERTEXT) return -2;

        if (xport_full_recv(r, cipher, body_len) != 0) return -1;
        pt_len = body_len - 16u;
        if (pt_len > buf_max + 1u) return -2;

        compose_nonce(r->rcv.iv, r->rcv.seq, nonce);
        if (r->aead_alg == TLS_AEAD_AES_128_GCM) {
            ok = aes128_gcm_open(r->rcv.key, nonce,
                                 hdr, TLS_REC_HEADER_SIZE,
                                 cipher, pt_len,
                                 cipher + pt_len,
                                 cipher);
        } else {
            ok = chacha20poly1305_open(r->rcv.key, nonce,
                                       hdr, TLS_REC_HEADER_SIZE,
                                       cipher, pt_len,
                                       cipher + pt_len,
                                       cipher);
        }
        if (!ok) {
            ct_wipe(cipher, sizeof(cipher));
            return -3;
        }
        r->rcv.seq++;

        /* Strip trailing zero padding to find the real type byte. */
        if (pt_len == 0u) { ct_wipe(cipher, sizeof(cipher)); return -2; }
        while (pt_len > 0u && cipher[pt_len - 1u] == 0u) pt_len--;
        if (pt_len == 0u) { ct_wipe(cipher, sizeof(cipher)); return -2; }

        inner_type = cipher[pt_len - 1u];
        pt_len--;

        if (pt_len > buf_max) { ct_wipe(cipher, sizeof(cipher)); return -2; }
        for (i = 0; i < pt_len; i++) buf[i] = cipher[i];
        ct_wipe(cipher, sizeof(cipher));

        *type_out = inner_type;
        *len_out  = pt_len;
        return 0;
    }

    /* Cleartext path. */
    if (body_len > TLS_REC_MAX_PLAINTEXT) return -2;
    if (body_len > buf_max) return -2;
    if (body_len > 0u) {
        if (xport_full_recv(r, buf, body_len) != 0) return -1;
    }
    *type_out = type;
    *len_out  = body_len;
    return 0;
}
