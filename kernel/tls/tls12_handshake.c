/* TLS 1.2 handshake driver - ECDHE_{RSA,ECDSA}_{AES_128_GCM,CHACHA20_POLY1305}_SHA256.
 *
 * Flow (RFC 5246 §7.3 + RFC 4492 + RFC 5288 / RFC 7905):
 *
 *   --- (caller already sent ClientHello, parsed ServerHello) ---
 *   recv Certificate
 *   recv ServerKeyExchange     (curve_type | named_curve | ECPoint || sigalg | siglen | sig)
 *   recv ServerHelloDone
 *   send ClientKeyExchange     (ECPoint with our pubkey)
 *   compute pre_master = ECDHE shared X coord
 *   compute master_secret = PRF(pre_master, "extended master secret", session_hash, 48)
 *                                  if EMS extension was negotiated, else
 *                          PRF(pre_master, "master secret", client_random||server_random, 48)
 *   compute key_block = PRF(master, "key expansion", server_random||client_random, key_block_len)
 *   send ChangeCipherSpec      (1-byte 0x01 record, type=20, cleartext)
 *   install client write key + iv
 *   send Finished              (handshake type 20, body = PRF(master, "client finished", SHA256(handshake_msgs), 12))
 *   recv ChangeCipherSpec
 *   install server read key + iv
 *   recv Finished              (verify_data = PRF(master, "server finished", SHA256(handshake_msgs_inc_client_finished), 12))
 *
 * Conservative: rejects everything we don't expect - unknown sig algs,
 * unsupported curves, malformed messages.
 */

#include "tls12_handshake.h"
#include "tls_record.h"
#include "tls_kdf.h"
#include "sha256.h"
#include "ct.h"
#include "x25519.h"
#include "p256.h"
#include "ecdsa.h"
#include "x509.h"
#include "x509_chain.h"
#include "rsa.h"
#include "asn1.h"
#include "serial.h"

/* Wire helpers (matches tls_handshake.c) */

static uint16_t rbe16_t(const uint8_t *p) {
    return (uint16_t)((((uint32_t)p[0]) << 8) | (uint32_t)p[1]);
}
static uint32_t rbe24_t(const uint8_t *p) {
    return (((uint32_t)p[0]) << 16) | (((uint32_t)p[1]) << 8) | (uint32_t)p[2];
}
static void wbe24_t(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 16) & 0xFFu);
    p[1] = (uint8_t)((v >> 8)  & 0xFFu);
    p[2] = (uint8_t)(v & 0xFFu);
}

/* Cipher suites we accept in 1.2 */

#define CS12_ECDHE_RSA_AES128_GCM    0xC02F
#define CS12_ECDHE_ECDSA_AES128_GCM  0xC02B
#define CS12_ECDHE_RSA_CHACHA20      0xCCA8
#define CS12_ECDHE_ECDSA_CHACHA20    0xCCA9

#define NG_X25519                    0x001D
#define NG_SECP256R1                 0x0017

#define SIG12_RSA_PKCS1_SHA256       0x0401
#define SIG12_RSA_PSS_SHA256         0x0804
#define SIG12_ECDSA_P256_SHA256      0x0403

#define HS_T_CERTIFICATE             11
#define HS_T_SERVER_KEY_EXCHANGE     12
#define HS_T_SERVER_HELLO_DONE       14
#define HS_T_CLIENT_KEY_EXCHANGE     16
#define HS_T_FINISHED                20

/* Transcript convenience */

static void th_update(tls_ctx_t *ctx, const uint8_t *p, uint32_t n) {
    sha256_update(&ctx->transcript, p, n);
}
static void th_snap(const tls_ctx_t *ctx, uint8_t out[32]) {
    sha256_ctx_t copy = ctx->transcript;
    sha256_final(&copy, out);
}

/* Cleartext handshake reader */
/* TLS 1.2 sends handshake messages as cleartext records (until CCS).
 * We pull records and concatenate them, then peel off one HS message
 * per call.  This is a simplified version of the 1.3 hs_reader. */

typedef struct {
    uint8_t  buf[8192];
    uint32_t len;
} hs12_reader_t;

static void hs12_init(hs12_reader_t *r) { r->len = 0; }

static int hs12_pull(tls_ctx_t *ctx, hs12_reader_t *r, uint32_t need) {
    uint8_t  rec[TLS_REC_MAX_PLAINTEXT];
    uint32_t rl;
    uint8_t  rt;
    while (r->len < need) {
        int rc = tls_record_recv(&ctx->rec, &rt, rec, sizeof(rec), &rl);
        if (rc < 0) return TLS_ERR_TRANSPORT;
        if (rt == TLS_RT_ALERT) return TLS_ERR_PROTOCOL;
        if (rt != TLS_RT_HANDSHAKE) return TLS_ERR_PROTOCOL;
        if (rl > sizeof(r->buf) - r->len) return TLS_ERR_PROTOCOL;
        {
            uint32_t i;
            for (i = 0; i < rl; i++) r->buf[r->len + i] = rec[i];
            r->len += rl;
        }
    }
    return TLS_ERR_OK;
}

/* Read one handshake message into `out`. Caller is responsible for
 * folding the message (header + body) into the transcript hash via
 * th_update - passing `do_fold = 1` does that automatically; `0` lets
 * the caller take a transcript snapshot before folding (needed for
 * Finished verification). */
static int hs12_read_msg(tls_ctx_t *ctx, hs12_reader_t *r,
                         uint8_t *out, uint32_t out_max,
                         uint8_t *type_out, uint32_t *len_out,
                         int do_fold) {
    uint32_t mlen;
    uint32_t i;
    int      rc;
    rc = hs12_pull(ctx, r, 4u);
    if (rc != TLS_ERR_OK) return rc;
    mlen = rbe24_t(&r->buf[1]);
    if (mlen > out_max) return TLS_ERR_PROTOCOL;
    rc = hs12_pull(ctx, r, 4u + mlen);
    if (rc != TLS_ERR_OK) return rc;
    *type_out = r->buf[0];
    *len_out  = mlen;
    if (do_fold) th_update(ctx, r->buf, 4u + mlen);
    for (i = 0; i < mlen; i++) out[i] = r->buf[4u + i];
    for (i = 4u + mlen; i < r->len; i++) {
        r->buf[i - (4u + mlen)] = r->buf[i];
    }
    r->len -= (4u + mlen);
    return TLS_ERR_OK;
}

/* Certificate parser (1.2: list with no req_ctx prefix) */

static int ingest_certificates(tls_ctx_t *ctx,
                               const uint8_t *body, uint32_t blen) {
    const uint8_t *p = body;
    const uint8_t *end = body + blen;
    uint32_t list_len;
    uint32_t i;

    if (blen < 3u) return TLS_ERR_PARSE;
    list_len = rbe24_t(p); p += 3;
    if ((uint32_t)(end - p) != list_len) return TLS_ERR_PARSE;
    end = p + list_len;
    ctx->cert_buf_used = 0;

    while (p < end) {
        uint32_t cl;
        if ((uint32_t)(end - p) < 3u) return TLS_ERR_PARSE;
        cl = rbe24_t(p); p += 3;
        if ((uint32_t)(end - p) < cl) return TLS_ERR_PARSE;
        if (ctx->cert_buf_used + cl > TLS_CTX_CERT_BUF) return TLS_ERR_OOM;
        for (i = 0; i < cl; i++) {
            ctx->cert_buf[ctx->cert_buf_used + i] = p[i];
        }
        if (x509_chain_add(&ctx->chain,
                           ctx->cert_buf + ctx->cert_buf_used, cl) != X509_OK) {
            return TLS_ERR_CERT;
        }
        ctx->cert_buf_used += cl;
        p += cl;
    }
    if (ctx->chain.n == 0u) return TLS_ERR_CERT;
    return TLS_ERR_OK;
}

/* ServerKeyExchange parsing + sig verify */

/* Returns 0 on OK, sets *server_pub / *server_pub_len from the ECPoint
 * blob (raw bytes, no length prefix), and verifies the signature against
 * the leaf cert's pubkey. */
static int parse_server_kex(tls_ctx_t *ctx,
                            const uint8_t *body, uint32_t blen,
                            uint8_t server_pub[65], uint32_t *server_pub_len) {
    const uint8_t *p = body;
    const uint8_t *end = body + blen;
    const uint8_t *params_start;
    uint32_t       params_len;
    uint8_t        ec_curve_type;
    uint16_t       named_curve;
    uint8_t        pub_len_byte;
    uint16_t       sigscheme;
    uint16_t       sig_len;
    const uint8_t *sig;
    uint8_t        signed_blob[32 + 32 + 4 + 65];
    uint32_t       sb_len = 0;
    uint8_t        hash[32];
    uint32_t       i;

    if (blen < 4u) return TLS_ERR_PARSE;
    params_start = p;
    ec_curve_type = *p++;
    if (ec_curve_type != 3u) return TLS_ERR_PROTOCOL;  /* named_curve */
    named_curve = rbe16_t(p); p += 2;
    if (named_curve != NG_X25519 && named_curve != NG_SECP256R1)
        return TLS_ERR_PROTOCOL;

    pub_len_byte = *p++;
    if ((uint32_t)(end - p) < pub_len_byte) return TLS_ERR_PARSE;

    if (named_curve == NG_X25519) {
        if (pub_len_byte != 32u) return TLS_ERR_PROTOCOL;
    } else {
        if (pub_len_byte != 65u) return TLS_ERR_PROTOCOL;
        if (p[0] != 0x04u) return TLS_ERR_PROTOCOL;
    }
    for (i = 0; i < pub_len_byte; i++) server_pub[i] = p[i];
    *server_pub_len = pub_len_byte;
    p += pub_len_byte;
    params_len = (uint32_t)(p - params_start);
    ctx->selected_group = named_curve;

    /* signature_algorithm + signature. */
    if ((uint32_t)(end - p) < 4u) return TLS_ERR_PARSE;
    sigscheme = rbe16_t(p); p += 2;
    sig_len   = rbe16_t(p); p += 2;
    if ((uint32_t)(end - p) != sig_len) return TLS_ERR_PARSE;
    sig = p;
    ctx->selected_sigalg = sigscheme;

    /* signed content = client_random | server_random | params */
    for (i = 0; i < 32u; i++) signed_blob[sb_len + i] = ctx->client_random[i];
    sb_len += 32u;
    for (i = 0; i < 32u; i++) signed_blob[sb_len + i] = ctx->server_random[i];
    sb_len += 32u;
    for (i = 0; i < params_len; i++) signed_blob[sb_len + i] = params_start[i];
    sb_len += params_len;

    {
        sha256_ctx_t s;
        sha256_init(&s);
        sha256_update(&s, signed_blob, sb_len);
        sha256_final(&s, hash);
    }

    {
        const x509_pubkey_t *pk = &ctx->chain.certs[0].pubkey;
        int ok = 0;
        if (sigscheme == SIG12_RSA_PSS_SHA256) {
            if (pk->type != X509_PK_RSA) return TLS_ERR_BAD_SIGNATURE;
            ok = rsa_pss_verify_sha256(pk->rsa.modulus, pk->rsa.modulus_len,
                                       pk->rsa.exponent, pk->rsa.exponent_len,
                                       sig, sig_len, hash, 32u);
        } else if (sigscheme == SIG12_RSA_PKCS1_SHA256) {
            if (pk->type != X509_PK_RSA) return TLS_ERR_BAD_SIGNATURE;
            ok = rsa_pkcs1v15_verify_sha256(pk->rsa.modulus, pk->rsa.modulus_len,
                                            pk->rsa.exponent, pk->rsa.exponent_len,
                                            sig, sig_len, hash);
        } else if (sigscheme == SIG12_ECDSA_P256_SHA256) {
            p256_aff_t Q;
            const uint8_t *rb = NULL, *sb = NULL;
            uint32_t rl = 0, sl = 0;
            asn1_cur_t outer, seq;
            if (pk->type != X509_PK_EC_P256) return TLS_ERR_BAD_SIGNATURE;
            if (p256_pub_from_uncompressed(&Q, pk->ec.point, pk->ec.point_len) != 0)
                return TLS_ERR_BAD_SIGNATURE;
            asn1_init(&outer, sig, sig_len);
            if (asn1_open(&outer, ASN1_TAG_SEQUENCE, &seq) != 0) return TLS_ERR_BAD_SIGNATURE;
            if (asn1_read_uint(&seq, &rb, &rl) != 0) return TLS_ERR_BAD_SIGNATURE;
            if (asn1_read_uint(&seq, &sb, &sl) != 0) return TLS_ERR_BAD_SIGNATURE;
            ok = (ecdsa_p256_verify(&Q, hash, 32u, rb, rl, sb, sl) == 0) ? 1 : 0;
        } else {
            return TLS_ERR_BAD_SIGNATURE;
        }
        if (!ok) return TLS_ERR_BAD_SIGNATURE;
    }
    return TLS_ERR_OK;
}

/* ECDHE */

static int compute_ecdhe(tls_ctx_t *ctx,
                         const uint8_t *server_pub, uint32_t server_pub_len) {
    if (ctx->selected_group == NG_X25519) {
        if (server_pub_len != 32u) return TLS_ERR_PROTOCOL;
        x25519(ctx->ecdhe_shared, ctx->client_priv, server_pub);
    } else if (ctx->selected_group == NG_SECP256R1) {
        p256_aff_t S;
        p256_jac_t Z_jac;
        p256_aff_t Z_aff;
        p256_scalar_t s;
        if (server_pub_len != 65u) return TLS_ERR_PROTOCOL;
        if (p256_pub_from_uncompressed(&S, server_pub, 65u) != 0)
            return TLS_ERR_PROTOCOL;
        if (p256_scalar_from_be(s, ctx->p256_priv) != 0
            || p256_scalar_iszero(s)) return TLS_ERR_PROTOCOL;
        p256_scalar_mul_point(&Z_jac, s, &S);
        if (p256_jac_is_infinity(&Z_jac)) return TLS_ERR_PROTOCOL;
        p256_jac_to_affine(&Z_aff, &Z_jac);
        p256_fe_to_be(ctx->ecdhe_shared, Z_aff.x);
    } else {
        return TLS_ERR_PROTOCOL;
    }
    {
        uint32_t i;
        uint8_t any = 0;
        for (i = 0; i < 32u; i++) any |= ctx->ecdhe_shared[i];
        if (any == 0u) return TLS_ERR_PROTOCOL;
    }
    return TLS_ERR_OK;
}

/* Build ClientKeyExchange */

static int send_client_kex(tls_ctx_t *ctx) {
    uint8_t  msg[4 + 1 + 65];
    uint32_t mlen;
    uint32_t i;
    uint8_t  pub[65];
    uint32_t pl;

    if (ctx->selected_group == NG_X25519) {
        for (i = 0; i < 32u; i++) pub[i] = ctx->client_pub[i];
        pl = 32u;
    } else {
        for (i = 0; i < 65u; i++) pub[i] = ctx->p256_pub[i];
        pl = 65u;
    }

    msg[0] = HS_T_CLIENT_KEY_EXCHANGE;
    wbe24_t(msg + 1, pl + 1u);
    msg[4] = (uint8_t)pl;
    for (i = 0; i < pl; i++) msg[5 + i] = pub[i];
    mlen = 4u + 1u + pl;

    th_update(ctx, msg, mlen);
    if (tls_record_send(&ctx->rec, TLS_RT_HANDSHAKE, msg, mlen) != 0)
        return TLS_ERR_TRANSPORT;
    return TLS_ERR_OK;
}

/* Cipher-specific layout */

static uint32_t cs12_key_len(uint16_t cs) {
    return (cs == CS12_ECDHE_RSA_AES128_GCM ||
            cs == CS12_ECDHE_ECDSA_AES128_GCM) ? 16u : 32u;
}

static uint32_t cs12_iv_len(uint16_t cs) {
    return (cs == CS12_ECDHE_RSA_AES128_GCM ||
            cs == CS12_ECDHE_ECDSA_AES128_GCM) ? 4u : 12u;
}

static uint8_t cs12_aead_alg(uint16_t cs) {
    return (cs == CS12_ECDHE_RSA_AES128_GCM ||
            cs == CS12_ECDHE_ECDSA_AES128_GCM)
            ? TLS_AEAD_AES_128_GCM : TLS_AEAD_CHACHA20_POLY1305;
}

/* Main driver */

int tls12_handshake_client(tls_ctx_t *ctx) {
    uint8_t  msg_buf[20480];
    uint8_t  mtype = 0;
    uint32_t mlen = 0;
    int      rc;
    hs12_reader_t reader;
    uint8_t  server_pub[65];
    uint32_t server_pub_len = 0;
    uint8_t  master_secret[48];
    uint8_t  key_block[88];
    uint16_t cs = ctx->selected_cipher;
    uint32_t kl = cs12_key_len(cs);
    uint32_t il = cs12_iv_len(cs);
    uint32_t kb_len = 2u * kl + 2u * il;

    hs12_init(&reader);

    /* 1. Certificate. */
    rc = hs12_read_msg(ctx, &reader, msg_buf, sizeof(msg_buf), &mtype, &mlen, 1);
    if (rc != TLS_ERR_OK) {
        serial_printf("[tls12] cert-read rc=%d\n", rc);
        return rc;
    }
    if (mtype != HS_T_CERTIFICATE) {
        serial_printf("[tls12] cert-type mtype=%u expected=%u\n",
                      (unsigned)mtype, (unsigned)HS_T_CERTIFICATE);
        return TLS_ERR_PROTOCOL;
    }
    rc = ingest_certificates(ctx, msg_buf, mlen);
    if (rc != TLS_ERR_OK) {
        serial_printf("[tls12] cert-ingest rc=%d mlen=%u\n", rc, (unsigned)mlen);
        return rc;
    }

    /* 2. ServerKeyExchange. */
    rc = hs12_read_msg(ctx, &reader, msg_buf, sizeof(msg_buf), &mtype, &mlen, 1);
    if (rc != TLS_ERR_OK) {
        serial_printf("[tls12] skex-read rc=%d\n", rc);
        return rc;
    }
    if (mtype != HS_T_SERVER_KEY_EXCHANGE) {
        serial_printf("[tls12] skex-type mtype=%u expected=%u\n",
                      (unsigned)mtype, (unsigned)HS_T_SERVER_KEY_EXCHANGE);
        return TLS_ERR_PROTOCOL;
    }
    rc = parse_server_kex(ctx, msg_buf, mlen, server_pub, &server_pub_len);
    if (rc != TLS_ERR_OK) {
        serial_printf("[tls12] skex-parse rc=%d mlen=%u\n", rc, (unsigned)mlen);
        return rc;
    }

    /* 3. Verify cert chain. */
    rc = x509_chain_verify(&ctx->chain, ctx->hostname, ctx->now_epoch);
    if (rc != X509_OK) {
        serial_printf("[tls12] chain-verify rc=%d\n", rc);
        return rc;
    }

    /* 4. ServerHelloDone. */
    rc = hs12_read_msg(ctx, &reader, msg_buf, sizeof(msg_buf), &mtype, &mlen, 1);
    if (rc != TLS_ERR_OK) {
        serial_printf("[tls12] shd-read rc=%d\n", rc);
        return rc;
    }
    if (mtype != HS_T_SERVER_HELLO_DONE || mlen != 0u) {
        serial_printf("[tls12] shd-type mtype=%u mlen=%u\n",
                      (unsigned)mtype, (unsigned)mlen);
        return TLS_ERR_PROTOCOL;
    }

    /* 5. Compute pre_master_secret = ECDHE shared. */
    rc = compute_ecdhe(ctx, server_pub, server_pub_len);
    if (rc != TLS_ERR_OK) {
        serial_printf("[tls12] ecdhe rc=%d pub_len=%u\n",
                      rc, (unsigned)server_pub_len);
        return rc;
    }

    /* 6. Send ClientKeyExchange (folds itself into transcript). */
    rc = send_client_kex(ctx);
    if (rc != TLS_ERR_OK) {
        serial_printf("[tls12] ckex-send rc=%d\n", rc);
        return rc;
    }

    /* 7. master_secret = PRF(pre_master, "extended master secret",
     *                        SHA256(handshake_msgs_through_CKE), 48).
     * We always emit the EMS extension in ClientHello so any modern
     * server (Mozilla MIN policy) is fine; if the server didn't echo
     * EMS we fall back to the standard label.
     *
     * We don't currently track whether the server confirmed EMS - the
     * extension was offered and a 1.2 server that omits it is permitted
     * to use the legacy schedule.  Use legacy for maximum compat. */
    {
        uint8_t seed[64];
        uint32_t i;
        for (i = 0; i < 32u; i++) seed[i]      = ctx->client_random[i];
        for (i = 0; i < 32u; i++) seed[32 + i] = ctx->server_random[i];
        tls12_prf(ctx->ecdhe_shared, 32u,
                  "master secret",
                  seed, 64u,
                  master_secret, 48u);
    }

    /* 8. key_block = PRF(master, "key expansion",
     *                    server_random || client_random,
     *                    key_block_len). */
    {
        uint8_t seed[64];
        uint32_t i;
        for (i = 0; i < 32u; i++) seed[i]      = ctx->server_random[i];
        for (i = 0; i < 32u; i++) seed[32 + i] = ctx->client_random[i];
        tls12_prf(master_secret, 48u,
                  "key expansion",
                  seed, 64u,
                  key_block, kb_len);
    }

    /* 9. Configure AEAD + 1.2 wire format. Keys NOT yet installed -
     *    CCS must go out cleartext first. */
    tls_record_set_aead(&ctx->rec, cs12_aead_alg(cs));
    tls_record_set_tls12(&ctx->rec);

    /* 10. Send ChangeCipherSpec cleartext. */
    {
        uint8_t ccs = 0x01u;
        if (tls_record_send(&ctx->rec, TLS_RT_CHANGE_CIPHER_SPEC, &ccs, 1u) != 0) {
            serial_printf("[tls12] ccs-send fail\n");
            return TLS_ERR_TRANSPORT;
        }
    }

    /* 11. Install client send key + IV (key_block layout:
     *      client_write_key | server_write_key | client_iv | server_iv). */
    {
        uint8_t pad_iv[12];
        uint32_t i;
        for (i = 0; i < 12u; i++) pad_iv[i] = 0u;
        for (i = 0; i < il; i++) pad_iv[i] = key_block[2u * kl + i];
        tls_record_set_send_key(&ctx->rec, key_block + 0u, kl, pad_iv);
    }

    /* 12. Send client Finished:
     *     verify_data = PRF(master, "client finished",
     *                       SHA256(handshake_msgs), 12). */
    {
        uint8_t snap[32];
        uint8_t verify[12];
        uint8_t fin_msg[4 + 12];
        uint32_t i;

        th_snap(ctx, snap);
        tls12_prf(master_secret, 48u, "client finished", snap, 32u, verify, 12u);
        fin_msg[0] = HS_T_FINISHED;
        wbe24_t(fin_msg + 1, 12u);
        for (i = 0; i < 12u; i++) fin_msg[4 + i] = verify[i];
        th_update(ctx, fin_msg, sizeof(fin_msg));
        if (tls_record_send(&ctx->rec, TLS_RT_HANDSHAKE,
                            fin_msg, sizeof(fin_msg)) != 0) {
            serial_printf("[tls12] client-fin-send fail\n");
            return TLS_ERR_TRANSPORT;
        }
    }

    /* 13. Recv ChangeCipherSpec (cleartext - recv side not yet keyed). */
    {
        uint8_t  rec_body[16];
        uint32_t rl;
        uint8_t  rt;
        rc = tls_record_recv(&ctx->rec, &rt, rec_body, sizeof(rec_body), &rl);
        if (rc < 0) {
            serial_printf("[tls12] ccs-recv rc=%d\n", rc);
            return TLS_ERR_TRANSPORT;
        }
        if (rt != TLS_RT_CHANGE_CIPHER_SPEC) {
            serial_printf("[tls12] ccs-recv rt=%u expected=%u\n",
                          (unsigned)rt, (unsigned)TLS_RT_CHANGE_CIPHER_SPEC);
            return TLS_ERR_PROTOCOL;
        }
    }

    /* 14. Install server recv key + IV. */
    {
        uint8_t pad_iv[12];
        uint32_t i;
        for (i = 0; i < 12u; i++) pad_iv[i] = 0u;
        for (i = 0; i < il; i++) pad_iv[i] = key_block[2u * kl + il + i];
        tls_record_set_recv_key(&ctx->rec, key_block + kl, kl, pad_iv);
    }

    /* 15. Recv Finished and verify. Snapshot transcript BEFORE folding
     *     this Finished message in. */
    rc = hs12_read_msg(ctx, &reader, msg_buf, sizeof(msg_buf), &mtype, &mlen, 0);
    if (rc != TLS_ERR_OK) {
        serial_printf("[tls12] fin-read rc=%d\n", rc);
        return rc;
    }
    if (mtype != HS_T_FINISHED || mlen != 12u) {
        serial_printf("[tls12] fin-type mtype=%u mlen=%u\n",
                      (unsigned)mtype, (unsigned)mlen);
        return TLS_ERR_PROTOCOL;
    }
    {
        uint8_t snap_pre[32];
        uint8_t expected[12];
        uint32_t i;
        uint8_t hdr[4];
        th_snap(ctx, snap_pre);
        tls12_prf(master_secret, 48u, "server finished",
                  snap_pre, 32u, expected, 12u);
        for (i = 0; i < 12u; i++) {
            if (expected[i] != msg_buf[i]) {
                ct_wipe(master_secret, sizeof(master_secret));
                ct_wipe(key_block,     sizeof(key_block));
                return TLS_ERR_FINISHED_MAC;
            }
        }
        /* Now fold (in case future post-handshake messages care). */
        hdr[0] = HS_T_FINISHED;
        wbe24_t(hdr + 1, 12u);
        th_update(ctx, hdr, 4u);
        th_update(ctx, msg_buf, 12u);
        ct_wipe(expected, sizeof(expected));
    }

    ct_wipe(master_secret, sizeof(master_secret));
    ct_wipe(key_block,     sizeof(key_block));
    return TLS_ERR_OK;
}
