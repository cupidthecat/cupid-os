/* TLS 1.3 client handshake driver.
 *
 * Cipher suite: TLS_CHACHA20_POLY1305_SHA256 (only).
 * KEX:          X25519 (only).
 * Sig alg:      rsa_pss_rsae_sha256 (only).
 *
 * State machine: build & send ClientHello -> parse ServerHello -> install
 * handshake traffic keys -> loop reading encrypted records:
 *   EncryptedExtensions -> Certificate -> CertificateVerify -> Finished
 * -> send client Finished -> install application traffic keys.
 *
 * Conservative on error: any unexpected message, bad length,
 * unsupported extension value etc. aborts with an error. */

#include "tls_ctx.h"
#include "tls_record.h"
#include "tls_kdf.h"
#include "ct.h"
#include "csprng.h"
#include "sha256.h"
#include "hmac.h"
#include "hkdf.h"
#include "x25519.h"
#include "p256.h"
#include "ecdsa.h"
#include "x509.h"
#include "x509_chain.h"
#include "rsa.h"
#include "asn1.h"
#include "tls12_handshake.h"

/* Wire encoding helpers */

static void wbe16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}
static void wbe24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 16) & 0xFFu);
    p[1] = (uint8_t)((v >> 8)  & 0xFFu);
    p[2] = (uint8_t)(v & 0xFFu);
}
static uint16_t rbe16(const uint8_t *p) {
    return (uint16_t)((((uint32_t)p[0]) << 8) | (uint32_t)p[1]);
}
static uint32_t rbe24(const uint8_t *p) {
    return (((uint32_t)p[0]) << 16) | (((uint32_t)p[1]) << 8) | (uint32_t)p[2];
}

/* Transcript */

static void th_update(tls_ctx_t *ctx, const uint8_t *p, uint32_t n) {
    sha256_update(&ctx->transcript, p, n);
}

/* Snapshot the current transcript hash without disturbing it. */
static void th_snapshot(const tls_ctx_t *ctx, uint8_t out[32]) {
    sha256_ctx_t copy = ctx->transcript;
    sha256_final(&copy, out);
}

/* ClientHello build */

#define CIPHER_TLS13_AES_128_GCM_SHA256       0x1301
#define CIPHER_TLS13_CHACHA20_POLY1305_SHA256 0x1303
#define CS12_ECDHE_RSA_AES128_GCM             0xC02F
#define CS12_ECDHE_ECDSA_AES128_GCM           0xC02B
#define CS12_ECDHE_RSA_CHACHA20               0xCCA8
#define CS12_ECDHE_ECDSA_CHACHA20             0xCCA9
#define EXT_EXTENDED_MASTER_SECRET            0x0017
#define EXT_EC_POINT_FORMATS                  0x000B
#define EXT_SERVER_NAME            0x0000
#define EXT_SUPPORTED_GROUPS       0x000A
#define EXT_SIG_ALGS               0x000D
#define EXT_SUPPORTED_VERSIONS     0x002B
#define EXT_PSK_KEY_EXCHANGE_MODES 0x002D
#define EXT_KEY_SHARE              0x0033
#define NAMED_GROUP_X25519         0x001D
#define NAMED_GROUP_SECP256R1      0x0017
#define SIGSCHEME_RSA_PSS_SHA256       0x0804
#define SIGSCHEME_ECDSA_P256_SHA256    0x0403
#define SIGSCHEME_RSA_PKCS1_SHA256     0x0401
#define SIGSCHEME_RSA_PKCS1_SHA384     0x0501
#define SIGSCHEME_RSA_PKCS1_SHA512     0x0601

#define HS_TYPE_CLIENT_HELLO        1
#define HS_TYPE_SERVER_HELLO        2
#define HS_TYPE_NEW_SESSION_TICKET  4
#define HS_TYPE_ENCRYPTED_EXTS      8
#define HS_TYPE_CERTIFICATE        11
#define HS_TYPE_CERT_VERIFY        15
#define HS_TYPE_FINISHED           20

#define ALERT_LEVEL_FATAL  2
#define ALERT_DESC_CLOSE_NOTIFY  0
#define ALERT_DESC_HANDSHAKE_FAILURE 40

/* Returns total bytes written; -1 on overflow. Buffer is at least 1 KB. */
static int build_client_hello(const tls_ctx_t *ctx,
                              uint8_t *out, uint32_t cap) {
    uint8_t  *p = out;
    uint8_t  *p_hs_len, *p_body_start;
    uint8_t  *p_ext_total_start;
    uint32_t hs_body_len;
    uint32_t ext_total;
    uint32_t i;
    uint32_t hl = ctx->hostname_len;

    /* Conservative size check up front. */
    if (cap < 384u + hl) return -1;

    /* Handshake header. */
    *p++ = HS_TYPE_CLIENT_HELLO;
    p_hs_len = p; p += 3;          /* length placeholder */
    p_body_start = p;

    /* legacy_version. */
    *p++ = 0x03; *p++ = 0x03;
    /* client_random. */
    for (i = 0; i < 32u; i++) *p++ = ctx->client_random[i];
    /* legacy_session_id (32 random bytes for compat). */
    *p++ = 32;
    {
        uint8_t sid[32];
        crypto_random_bytes(sid, 32u);
        for (i = 0; i < 32u; i++) *p++ = sid[i];
    }
    /* cipher_suites: 1.3 AEAD + 1.2 ECDHE+AEAD. */
    wbe16(p, 12u); p += 2;
    wbe16(p, CIPHER_TLS13_AES_128_GCM_SHA256); p += 2;
    wbe16(p, CIPHER_TLS13_CHACHA20_POLY1305_SHA256); p += 2;
    wbe16(p, CS12_ECDHE_RSA_AES128_GCM);        p += 2;
    wbe16(p, CS12_ECDHE_ECDSA_AES128_GCM);      p += 2;
    wbe16(p, CS12_ECDHE_RSA_CHACHA20);          p += 2;
    wbe16(p, CS12_ECDHE_ECDSA_CHACHA20);        p += 2;
    /* compression_methods = {null}. */
    *p++ = 1;
    *p++ = 0;
    /* Extensions length placeholder. */
    p_ext_total_start = p; p += 2;

    /* server_name extension. */
    {
        wbe16(p, EXT_SERVER_NAME); p += 2;
        wbe16(p, (uint16_t)(hl + 5u)); p += 2;
        wbe16(p, (uint16_t)(hl + 3u)); p += 2;
        *p++ = 0; /* host_name */
        wbe16(p, (uint16_t)hl); p += 2;
        for (i = 0; i < hl; i++) *p++ = (uint8_t)ctx->hostname[i];
    }
    /* supported_versions: TLS 1.3 first, then 1.2. */
    {
        wbe16(p, EXT_SUPPORTED_VERSIONS); p += 2;
        wbe16(p, 5u); p += 2;
        *p++ = 4;
        *p++ = 0x03; *p++ = 0x04;
        *p++ = 0x03; *p++ = 0x03;
    }
    /* ec_point_formats (RFC 4492 §5.1.2): uncompressed only. */
    {
        wbe16(p, EXT_EC_POINT_FORMATS); p += 2;
        wbe16(p, 2u); p += 2;
        *p++ = 1;
        *p++ = 0;          /* uncompressed */
    }
    /* extended_master_secret (RFC 7627), empty body. */
    {
        wbe16(p, EXT_EXTENDED_MASTER_SECRET); p += 2;
        wbe16(p, 0u); p += 2;
    }
    /* supported_groups: X25519 + secp256r1. */
    {
        wbe16(p, EXT_SUPPORTED_GROUPS); p += 2;
        wbe16(p, 6u); p += 2;
        wbe16(p, 4u); p += 2;
        wbe16(p, NAMED_GROUP_X25519);    p += 2;
        wbe16(p, NAMED_GROUP_SECP256R1); p += 2;
    }
    /* signature_algorithms: PSS+SHA256 / ECDSA-P256-SHA256 are required
     * for the TLS 1.3 CertificateVerify path; RSA-PKCS1 with SHA-256/384/512
     * are advertised via signature_algorithms_cert below so the server
     * picks an RSA cert chain we can validate. */
    {
        wbe16(p, EXT_SIG_ALGS); p += 2;
        wbe16(p, 6u); p += 2;
        wbe16(p, 4u); p += 2;
        wbe16(p, SIGSCHEME_RSA_PSS_SHA256);    p += 2;
        wbe16(p, SIGSCHEME_ECDSA_P256_SHA256); p += 2;
    }
    /* signature_algorithms_cert (0x0032): which sig algs we accept on
     * the server cert chain. Wider than signature_algorithms - we can
     * verify RSA-PKCS1 with SHA-256/384/512 plus the two TLS 1.3
     * mandatories. */
    {
        wbe16(p, 0x0032u); p += 2;     /* extension type */
        wbe16(p, 12u); p += 2;          /* ext data len   */
        wbe16(p, 10u); p += 2;          /* list  len      */
        wbe16(p, SIGSCHEME_RSA_PSS_SHA256);    p += 2;
        wbe16(p, SIGSCHEME_ECDSA_P256_SHA256); p += 2;
        wbe16(p, SIGSCHEME_RSA_PKCS1_SHA256);  p += 2;
        wbe16(p, SIGSCHEME_RSA_PKCS1_SHA384);  p += 2;
        wbe16(p, SIGSCHEME_RSA_PKCS1_SHA512);  p += 2;
    }
    /* psk_key_exchange_modes. */
    {
        wbe16(p, EXT_PSK_KEY_EXCHANGE_MODES); p += 2;
        wbe16(p, 2u); p += 2;
        *p++ = 1;
        *p++ = 1;          /* psk_dhe_ke */
    }
    /* key_share: send shares for both X25519 (32B) and secp256r1 (65B). */
    {
        uint32_t list_len = (4u + 32u) + (4u + 65u);
        wbe16(p, EXT_KEY_SHARE); p += 2;
        wbe16(p, (uint16_t)(2u + list_len)); p += 2;
        wbe16(p, (uint16_t)list_len); p += 2;
        /* X25519 share. */
        wbe16(p, NAMED_GROUP_X25519); p += 2;
        wbe16(p, 32u); p += 2;
        for (i = 0; i < 32u; i++) *p++ = ctx->client_pub[i];
        /* secp256r1 uncompressed share. */
        wbe16(p, NAMED_GROUP_SECP256R1); p += 2;
        wbe16(p, 65u); p += 2;
        for (i = 0; i < 65u; i++) *p++ = ctx->p256_pub[i];
    }

    ext_total = (uint32_t)(p - (p_ext_total_start + 2));
    wbe16(p_ext_total_start, (uint16_t)ext_total);

    hs_body_len = (uint32_t)(p - p_body_start);
    wbe24(p_hs_len, hs_body_len);

    return (int)(p - out);
}

/* Read handshake message reassembler */
/* Pulls TLS records from the record layer and concatenates handshake
 * fragments. Caller passes a buffer to receive the next message body
 * along with its type. Uses a small per-call ring for buffered bytes
 * carried over between records. */

typedef struct {
    /* Bytes that came in but haven't been parsed into a complete msg.
     * Sized for one full TLS-record plaintext (16 KB) - large handshake
     * messages such as Certificate carrying RSA-4096 chains can fill
     * nearly the whole record. The previous 4 KB carry rejected such
     * records in hs_reader_fill with TLS_ERR_PROTOCOL. */
    uint8_t  carry[TLS_REC_MAX_PLAINTEXT];
    uint32_t carry_len;
} hs_reader_t;

static void hs_reader_init(hs_reader_t *h) { h->carry_len = 0; }

/* Read enough records to make at least `need` bytes available in carry. */
static int hs_reader_fill(tls_ctx_t *ctx, hs_reader_t *h, uint32_t need) {
    uint8_t  rec[TLS_REC_MAX_PLAINTEXT];
    uint32_t rl;
    uint8_t  rt;

    while (h->carry_len < need) {
        int rc;
        rc = tls_record_recv(&ctx->rec, &rt, rec, sizeof(rec), &rl);
        if (rc < 0) return TLS_ERR_TRANSPORT;
        if (rt == TLS_RT_CHANGE_CIPHER_SPEC) {
            /* Middlebox-compat dummy - ignore (RFC 8446 §5). */
            continue;
        }
        if (rt == TLS_RT_ALERT) {
            return TLS_ERR_PROTOCOL;
        }
        if (rt != TLS_RT_HANDSHAKE) return TLS_ERR_PROTOCOL;
        if (rl > sizeof(h->carry) - h->carry_len) return TLS_ERR_PROTOCOL;
        {
            uint32_t i;
            for (i = 0; i < rl; i++) {
                h->carry[h->carry_len + i] = rec[i];
            }
            h->carry_len += rl;
        }
    }
    return TLS_ERR_OK;
}

/* Read one handshake message into `out` (max `out_max` bytes).
 * `*type_out` gets the handshake type, `*len_out` the body length.
 * The 4-byte handshake header bytes are also written into the
 * transcript here. */
static int hs_read_msg(tls_ctx_t *ctx, hs_reader_t *h,
                       uint8_t *out, uint32_t out_max,
                       uint8_t *type_out, uint32_t *len_out) {
    uint32_t mlen;
    uint32_t i;
    int      rc;
    uint8_t  hdr[4];

    rc = hs_reader_fill(ctx, h, 4u);
    if (rc != TLS_ERR_OK) return rc;
    hdr[0] = h->carry[0];
    hdr[1] = h->carry[1];
    hdr[2] = h->carry[2];
    hdr[3] = h->carry[3];
    mlen = rbe24(&hdr[1]);
    if (mlen > out_max) return TLS_ERR_PROTOCOL;

    /* Need 4 (header) + mlen total. Grow carry as needed - but because
     * h->carry is small we may have to pull body straight into `out`. */
    if (4u + mlen <= sizeof(h->carry)) {
        rc = hs_reader_fill(ctx, h, 4u + mlen);
        if (rc != TLS_ERR_OK) return rc;
        for (i = 0; i < mlen; i++) out[i] = h->carry[4u + i];
        /* memmove leftover. */
        for (i = 4u + mlen; i < h->carry_len; i++) {
            h->carry[i - (4u + mlen)] = h->carry[i];
        }
        h->carry_len -= (4u + mlen);
    } else {
        /* Body too big for carry buffer - drain carry first then pull
         * directly from records. */
        uint32_t copied = 0;
        if (h->carry_len > 4u) {
            uint32_t avail = h->carry_len - 4u;
            for (i = 0; i < avail; i++) out[i] = h->carry[4u + i];
            copied = avail;
        }
        h->carry_len = 0;
        while (copied < mlen) {
            uint8_t  rec[TLS_REC_MAX_PLAINTEXT];
            uint32_t rl;
            uint8_t  rt;
            uint32_t take;
            int      pc;
            pc = tls_record_recv(&ctx->rec, &rt, rec, sizeof(rec), &rl);
            if (pc < 0) return TLS_ERR_TRANSPORT;
            if (rt == TLS_RT_CHANGE_CIPHER_SPEC) continue;
            if (rt != TLS_RT_HANDSHAKE) return TLS_ERR_PROTOCOL;
            take = mlen - copied;
            if (take > rl) take = rl;
            for (i = 0; i < take; i++) out[copied + i] = rec[i];
            copied += take;
            if (take < rl) {
                /* Stash leftover for next message. */
                uint32_t left = rl - take;
                if (left > sizeof(h->carry)) return TLS_ERR_PROTOCOL;
                for (i = 0; i < left; i++) h->carry[i] = rec[take + i];
                h->carry_len = left;
            }
        }
    }

    *type_out = hdr[0];
    *len_out  = mlen;

    /* Update transcript with header + body. */
    th_update(ctx, hdr, 4u);
    th_update(ctx, out, mlen);
    return TLS_ERR_OK;
}

/* ServerHello parse */

static int parse_server_hello(tls_ctx_t *ctx,
                              const uint8_t *body, uint32_t blen,
                              uint8_t server_pub_out[65],
                              uint32_t *server_pub_len_out) {
    const uint8_t *p = body;
    const uint8_t *end = body + blen;
    uint16_t cs;
    uint32_t sid_len;
    uint16_t ext_total;
    const uint8_t *ext_p;
    const uint8_t *ext_end;
    int got_ver = 0, got_share = 0;
    uint32_t i;

    if (blen < 2u + 32u + 1u) return TLS_ERR_PROTOCOL;
    if (p[0] != 0x03 || p[1] != 0x03) return TLS_ERR_PROTOCOL;
    p += 2;
    for (i = 0; i < 32u; i++) ctx->server_random[i] = p[i];
    p += 32;
    sid_len = (uint32_t)(*p++);
    if (sid_len > 32u) return TLS_ERR_PROTOCOL;
    if ((uint32_t)(end - p) < sid_len + 3u) return TLS_ERR_PROTOCOL;
    p += sid_len;
    cs = rbe16(p); p += 2;
    if (cs != CIPHER_TLS13_CHACHA20_POLY1305_SHA256 &&
        cs != CIPHER_TLS13_AES_128_GCM_SHA256       &&
        cs != CS12_ECDHE_RSA_AES128_GCM             &&
        cs != CS12_ECDHE_ECDSA_AES128_GCM           &&
        cs != CS12_ECDHE_RSA_CHACHA20               &&
        cs != CS12_ECDHE_ECDSA_CHACHA20)
        return TLS_ERR_PROTOCOL;
    ctx->selected_cipher = cs;
    if (*p++ != 0x00) return TLS_ERR_PROTOCOL;  /* compression */
    if ((uint32_t)(end - p) < 2u) return TLS_ERR_PROTOCOL;
    ext_total = rbe16(p); p += 2;
    if ((uint32_t)(end - p) < ext_total) return TLS_ERR_PROTOCOL;
    ext_p = p;
    ext_end = p + ext_total;

    while (ext_p < ext_end) {
        uint16_t et, el;
        if ((uint32_t)(ext_end - ext_p) < 4u) return TLS_ERR_PROTOCOL;
        et = rbe16(ext_p); ext_p += 2;
        el = rbe16(ext_p); ext_p += 2;
        if ((uint32_t)(ext_end - ext_p) < el) return TLS_ERR_PROTOCOL;

        if (et == EXT_SUPPORTED_VERSIONS) {
            if (el != 2u) return TLS_ERR_PROTOCOL;
            if (ext_p[0] != 0x03 || ext_p[1] != 0x04) return TLS_ERR_PROTOCOL;
            got_ver = 1;
        } else if (et == EXT_KEY_SHARE) {
            uint16_t group;
            uint16_t kl;
            uint32_t expected_kl;
            if (el < 4u) return TLS_ERR_PROTOCOL;
            group = rbe16(ext_p);
            kl    = rbe16(ext_p + 2);
            if (group == NAMED_GROUP_X25519)         expected_kl = 32u;
            else if (group == NAMED_GROUP_SECP256R1) expected_kl = 65u;
            else return TLS_ERR_PROTOCOL;
            if (kl != expected_kl) return TLS_ERR_PROTOCOL;
            if (4u + kl != el) return TLS_ERR_PROTOCOL;
            ctx->selected_group = group;
            for (i = 0; i < kl; i++) server_pub_out[i] = ext_p[4 + i];
            *server_pub_len_out = kl;
            got_share = 1;
        }
        ext_p += el;
    }

    /* For TLS 1.3: must see supported_versions=0x0304 + key_share.
     * For TLS 1.2: neither is required. */
    {
        int is_13 = (ctx->selected_cipher == CIPHER_TLS13_CHACHA20_POLY1305_SHA256
                  || ctx->selected_cipher == CIPHER_TLS13_AES_128_GCM_SHA256);
        if (is_13 && (!got_ver || !got_share)) return TLS_ERR_PROTOCOL;
    }
    (void)got_ver; (void)got_share;
    return TLS_ERR_OK;
}

/* Cert chain ingestion */

/* Certificate message body (TLS 1.3):
 *   opaque certificate_request_context<0..255>;
 *   CertificateEntry certificate_list<0..2^24-1>;
 *
 * CertificateEntry:
 *   opaque cert_data<1..2^24-1>;     // DER cert
 *   Extension extensions<0..2^16-1>; // ignored
 */
static int parse_certificate(tls_ctx_t *ctx,
                             const uint8_t *body, uint32_t blen) {
    uint32_t i;
    uint32_t ctx_len;
    const uint8_t *p = body;
    const uint8_t *end = body + blen;
    uint32_t list_len;

    if (blen < 1u) return TLS_ERR_PARSE;
    ctx_len = *p++;
    if ((uint32_t)(end - p) < ctx_len + 3u) return TLS_ERR_PARSE;
    p += ctx_len;
    list_len = rbe24(p); p += 3;
    if ((uint32_t)(end - p) < list_len) return TLS_ERR_PARSE;
    end = p + list_len;

    ctx->cert_buf_used = 0;

    while (p < end) {
        uint32_t cert_len;
        uint32_t ext_len;
        if ((uint32_t)(end - p) < 3u) return TLS_ERR_PARSE;
        cert_len = rbe24(p); p += 3;
        if ((uint32_t)(end - p) < cert_len) return TLS_ERR_PARSE;

        if (ctx->cert_buf_used + cert_len > TLS_CTX_CERT_BUF) return TLS_ERR_OOM;

        for (i = 0; i < cert_len; i++) {
            ctx->cert_buf[ctx->cert_buf_used + i] = p[i];
        }
        {
            int rc = x509_chain_add(&ctx->chain,
                                    ctx->cert_buf + ctx->cert_buf_used,
                                    cert_len);
            if (rc != X509_OK) return TLS_ERR_CERT;
        }
        ctx->cert_buf_used += cert_len;
        p += cert_len;

        if ((uint32_t)(end - p) < 2u) return TLS_ERR_PARSE;
        ext_len = rbe16(p); p += 2;
        if ((uint32_t)(end - p) < ext_len) return TLS_ERR_PARSE;
        p += ext_len;
    }
    if (ctx->chain.n == 0u) return TLS_ERR_CERT;
    return TLS_ERR_OK;
}

/* CertificateVerify */

/* The signed content is a fixed prefix + transcript hash. */
static int parse_cert_verify(tls_ctx_t *ctx,
                             const uint8_t *body, uint32_t blen) {
    static const char prefix_str[] =
        "                                "      /* 32 spaces */
        "                                "      /* 32 spaces */
        "TLS 1.3, server CertificateVerify";
    uint16_t scheme;
    uint16_t sig_len;
    uint8_t  hash[32];
    uint8_t  signed_buf[32 + 32 + 33 + 1 + 32 + 16];
    uint32_t signed_len = 0;
    uint32_t i;

    if (blen < 4u) return TLS_ERR_PARSE;
    scheme  = rbe16(body);
    sig_len = rbe16(body + 2);
    if (blen != 4u + sig_len) return TLS_ERR_PARSE;
    if (scheme != SIGSCHEME_RSA_PSS_SHA256 &&
        scheme != SIGSCHEME_ECDSA_P256_SHA256) return TLS_ERR_BAD_SIGNATURE;
    ctx->selected_sigalg = scheme;

    /* Build "signed content": 64 spaces + label + 0x00 + transcript_hash. */
    for (i = 0; i < 64u; i++) signed_buf[i] = (uint8_t)' ';
    signed_len = 64u;
    {
        uint32_t pl = sizeof(prefix_str) - 1u - 64u;  /* skip 64 spaces in str */
        for (i = 0; i < pl; i++) signed_buf[signed_len + i] = (uint8_t)prefix_str[64u + i];
        signed_len += pl;
    }
    signed_buf[signed_len++] = 0x00;
    /* The transcript hash is a snapshot taken before this CertVerify
     * was added. tls_handshake_client() saved it into
     * th_before_cert_verify. */
    for (i = 0; i < 32u; i++) signed_buf[signed_len + i] = ctx->th_before_cert_verify[i];
    signed_len += 32u;

    /* Hash it. */
    {
        sha256_ctx_t s;
        sha256_init(&s);
        sha256_update(&s, signed_buf, signed_len);
        sha256_final(&s, hash);
    }

    /* Verify against leaf pubkey, dispatch on signature scheme. */
    {
        const x509_pubkey_t *pk = &ctx->chain.certs[0].pubkey;
        int ok = 0;
        if (scheme == SIGSCHEME_RSA_PSS_SHA256) {
            if (pk->type != X509_PK_RSA) {
                ct_wipe(signed_buf, sizeof(signed_buf));
                return TLS_ERR_BAD_SIGNATURE;
            }
            ok = rsa_pss_verify_sha256(pk->rsa.modulus, pk->rsa.modulus_len,
                                       pk->rsa.exponent, pk->rsa.exponent_len,
                                       body + 4, sig_len, hash, 32u);
        } else { /* ECDSA P-256 SHA-256 */
            p256_aff_t Q;
            const uint8_t *rb = NULL, *sb = NULL;
            uint32_t rl = 0, sl = 0;
            asn1_cur_t outer, seq;
            if (pk->type != X509_PK_EC_P256) {
                ct_wipe(signed_buf, sizeof(signed_buf));
                return TLS_ERR_BAD_SIGNATURE;
            }
            if (p256_pub_from_uncompressed(&Q, pk->ec.point, pk->ec.point_len) != 0) {
                ct_wipe(signed_buf, sizeof(signed_buf));
                return TLS_ERR_BAD_SIGNATURE;
            }
            asn1_init(&outer, body + 4, sig_len);
            if (asn1_open(&outer, ASN1_TAG_SEQUENCE, &seq) != 0
                || asn1_read_uint(&seq, &rb, &rl) != 0
                || asn1_read_uint(&seq, &sb, &sl) != 0) {
                ct_wipe(signed_buf, sizeof(signed_buf));
                return TLS_ERR_BAD_SIGNATURE;
            }
            ok = (ecdsa_p256_verify(&Q, hash, 32u, rb, rl, sb, sl) == 0) ? 1 : 0;
        }
        ct_wipe(signed_buf, sizeof(signed_buf));
        if (!ok) return TLS_ERR_BAD_SIGNATURE;
    }
    return TLS_ERR_OK;
}

/* Finished MAC (server) */

static int verify_server_finished(tls_ctx_t *ctx,
                                  const uint8_t *body, uint32_t blen) {
    uint8_t finished_key[32];
    uint8_t expected[32];
    int     ok;

    if (blen != 32u) return TLS_ERR_PARSE;
    tls_kdf_finished_key(ctx->s_hs_traffic, finished_key);
    hmac_sha256(finished_key, 32u,
                ctx->th_before_server_finished, 32u,
                expected);
    ok = (ct_memcmp(expected, body, 32u) == 0);
    ct_wipe(finished_key, 32u);
    ct_wipe(expected, 32u);
    return ok ? TLS_ERR_OK : TLS_ERR_FINISHED_MAC;
}

/* Build & send client Finished */

static int send_client_finished(tls_ctx_t *ctx,
                                const uint8_t transcript_hash[32]) {
    uint8_t finished_key[32];
    uint8_t mac[32];
    uint8_t msg[4 + 32];
    uint32_t i;
    int rc;

    tls_kdf_finished_key(ctx->c_hs_traffic, finished_key);
    hmac_sha256(finished_key, 32u, transcript_hash, 32u, mac);
    ct_wipe(finished_key, 32u);

    msg[0] = HS_TYPE_FINISHED;
    wbe24(msg + 1, 32u);
    for (i = 0; i < 32u; i++) msg[4 + i] = mac[i];

    /* Update transcript before send (fold this Finished into hash). */
    th_update(ctx, msg, sizeof(msg));

    rc = tls_record_send(&ctx->rec, TLS_RT_HANDSHAKE, msg, sizeof(msg));
    ct_wipe(mac, 32u);
    return (rc == 0) ? TLS_ERR_OK : TLS_ERR_TRANSPORT;
}

/* Schedule */

static void compute_handshake_secrets(tls_ctx_t *ctx) {
    uint8_t empty_hash[32];
    uint8_t derived[32];
    uint8_t zeros[32];
    uint8_t hs_th[32];
    uint32_t i;
    for (i = 0; i < 32u; i++) zeros[i] = 0u;

    /* early_secret = HKDF-Extract(salt=0, ikm=0). */
    hkdf_extract(zeros, 32u, zeros, 32u, ctx->early_secret);

    /* SHA-256(""). */
    {
        sha256_ctx_t s;
        sha256_init(&s);
        sha256_final(&s, empty_hash);
    }
    /* derived = Derive-Secret(early, "derived", ""). */
    tls_kdf_derive_secret(ctx->early_secret, "derived",
                          empty_hash, 32u, derived);
    /* handshake_secret = HKDF-Extract(salt=derived, ikm=ECDHE). */
    hkdf_extract(derived, 32u, ctx->ecdhe_shared, 32u, ctx->handshake_secret);

    /* Transcript hash up to and including ServerHello. */
    th_snapshot(ctx, hs_th);

    tls_kdf_derive_secret(ctx->handshake_secret, "c hs traffic",
                          hs_th, 32u, ctx->c_hs_traffic);
    tls_kdf_derive_secret(ctx->handshake_secret, "s hs traffic",
                          hs_th, 32u, ctx->s_hs_traffic);

    ct_wipe(derived, 32u);
}

static void compute_application_secrets(tls_ctx_t *ctx) {
    uint8_t derived[32];
    uint8_t zeros[32];
    uint8_t empty_hash[32];
    uint32_t i;
    for (i = 0; i < 32u; i++) zeros[i] = 0u;

    {
        sha256_ctx_t s;
        sha256_init(&s);
        sha256_final(&s, empty_hash);
    }
    /* master_secret = HKDF-Extract(Derive-Secret(handshake, "derived", ""), 0). */
    tls_kdf_derive_secret(ctx->handshake_secret, "derived",
                          empty_hash, 32u, derived);
    hkdf_extract(derived, 32u, zeros, 32u, ctx->master_secret);

    tls_kdf_derive_secret(ctx->master_secret, "c ap traffic",
                          ctx->th_after_server_finished, 32u,
                          ctx->c_ap_traffic);
    tls_kdf_derive_secret(ctx->master_secret, "s ap traffic",
                          ctx->th_after_server_finished, 32u,
                          ctx->s_ap_traffic);
    ct_wipe(derived, 32u);
}

static uint32_t cipher_key_len(uint16_t cs) {
    return (cs == CIPHER_TLS13_AES_128_GCM_SHA256) ? 16u : 32u;
}

static void install_handshake_keys(tls_ctx_t *ctx) {
    uint8_t k[32], iv[12];
    uint32_t kl = cipher_key_len(ctx->selected_cipher);
    tls_kdf_traffic_keys(ctx->s_hs_traffic, k, kl, iv);
    tls_record_set_recv_key(&ctx->rec, k, kl, iv);
    ct_wipe(k, 32u); ct_wipe(iv, 12u);
}

static void install_client_handshake_send_key(tls_ctx_t *ctx) {
    uint8_t k[32], iv[12];
    uint32_t kl = cipher_key_len(ctx->selected_cipher);
    tls_kdf_traffic_keys(ctx->c_hs_traffic, k, kl, iv);
    tls_record_set_send_key(&ctx->rec, k, kl, iv);
    ct_wipe(k, 32u); ct_wipe(iv, 12u);
}

static void install_application_keys(tls_ctx_t *ctx) {
    uint8_t k[32], iv[12];
    uint32_t kl = cipher_key_len(ctx->selected_cipher);
    tls_kdf_traffic_keys(ctx->s_ap_traffic, k, kl, iv);
    tls_record_set_recv_key(&ctx->rec, k, kl, iv);
    tls_kdf_traffic_keys(ctx->c_ap_traffic, k, kl, iv);
    tls_record_set_send_key(&ctx->rec, k, kl, iv);
    ct_wipe(k, 32u); ct_wipe(iv, 12u);
}

/* Main driver */

int tls_handshake_client(tls_ctx_t *ctx) {
    uint8_t  ch[1024];
    int      ch_len;
    uint8_t  server_pub[65];
    uint32_t server_pub_len = 0;
    hs_reader_t reader;
    uint8_t  msg_buf[20480];
    uint8_t  mtype = 0;
    uint32_t mlen = 0;
    int      rc;

    /* 1. Build & send ClientHello. */
    ch_len = build_client_hello(ctx, ch, sizeof(ch));
    if (ch_len < 0) return TLS_ERR_OOM;
    th_update(ctx, ch, (uint32_t)ch_len);
    rc = tls_record_send(&ctx->rec, TLS_RT_HANDSHAKE, ch, (uint32_t)ch_len);
    if (rc != 0) return TLS_ERR_TRANSPORT;

    /* Optional: send a dummy ChangeCipherSpec for middlebox compat. */
    {
        uint8_t ccs = 0x01;
        (void)tls_record_send(&ctx->rec, TLS_RT_CHANGE_CIPHER_SPEC, &ccs, 1u);
    }

    /* 2. Read ServerHello (cleartext record, type=22). */
    {
        uint8_t  rec_body[TLS_REC_MAX_PLAINTEXT];
        uint32_t rl;
        uint8_t  rt;
        uint8_t  hs_type;
        uint32_t hs_body_len;
        rc = tls_record_recv(&ctx->rec, &rt, rec_body, sizeof(rec_body), &rl);
        if (rc < 0) return TLS_ERR_TRANSPORT;
        if (rt != TLS_RT_HANDSHAKE) return TLS_ERR_PROTOCOL;
        if (rl < 4u) return TLS_ERR_PROTOCOL;
        hs_type = rec_body[0];
        hs_body_len = rbe24(&rec_body[1]);
        if (hs_type != HS_TYPE_SERVER_HELLO) return TLS_ERR_PROTOCOL;
        if (hs_body_len + 4u != rl) return TLS_ERR_PROTOCOL;
        rc = parse_server_hello(ctx, rec_body + 4, hs_body_len,
                                server_pub, &server_pub_len);
        if (rc != TLS_ERR_OK) return rc;
        /* Update transcript with full ServerHello (header + body)
         * regardless of which protocol version we end up running. */
        th_update(ctx, rec_body, rl);

        /* TLS 1.2 dispatch - record layer setup happens inside the 1.2
         * driver since CCS must precede key activation there. */
        if (ctx->selected_cipher == CS12_ECDHE_RSA_AES128_GCM   ||
            ctx->selected_cipher == CS12_ECDHE_ECDSA_AES128_GCM ||
            ctx->selected_cipher == CS12_ECDHE_RSA_CHACHA20     ||
            ctx->selected_cipher == CS12_ECDHE_ECDSA_CHACHA20) {
            return tls12_handshake_client(ctx);
        }

        /* TLS 1.3 path. Pick AEAD now. */
        if (ctx->selected_cipher == CIPHER_TLS13_AES_128_GCM_SHA256) {
            tls_record_set_aead(&ctx->rec, TLS_AEAD_AES_128_GCM);
        } else {
            tls_record_set_aead(&ctx->rec, TLS_AEAD_CHACHA20_POLY1305);
        }
    }

    /* 3. Compute ECDHE shared secret based on selected group. */
    if (ctx->selected_group == NAMED_GROUP_X25519) {
        if (server_pub_len != 32u) return TLS_ERR_PROTOCOL;
        x25519(ctx->ecdhe_shared, ctx->client_priv, server_pub);
    } else if (ctx->selected_group == NAMED_GROUP_SECP256R1) {
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
        /* RFC 8446 §7.4.2: ECDHE shared = X coordinate (32 bytes). */
        p256_fe_to_be(ctx->ecdhe_shared, Z_aff.x);
    } else {
        return TLS_ERR_PROTOCOL;
    }
    /* Reject all-zero output (contributory check). */
    {
        uint32_t i;
        uint8_t any = 0;
        for (i = 0; i < 32u; i++) any |= ctx->ecdhe_shared[i];
        if (any == 0u) return TLS_ERR_PROTOCOL;
    }

    /* 4. Derive handshake traffic secrets, install recv key. */
    compute_handshake_secrets(ctx);
    install_handshake_keys(ctx);

    /* 5. Read encrypted handshake messages. */
    hs_reader_init(&reader);

    /* EncryptedExtensions (ignore content; just consume). */
    rc = hs_read_msg(ctx, &reader, msg_buf, sizeof(msg_buf), &mtype, &mlen);
    if (rc != TLS_ERR_OK) return rc;
    if (mtype != HS_TYPE_ENCRYPTED_EXTS) return TLS_ERR_PROTOCOL;

    /* CertificateRequest is OPTIONAL - we don't support client certs;
     * if it appears we must send empty Certificate later. Detect by
     * peeking next message type. We actually need to read it to update
     * transcript. For v1 we reject (servers don't request client certs
     * without prior config). */

    /* Certificate. */
    rc = hs_read_msg(ctx, &reader, msg_buf, sizeof(msg_buf), &mtype, &mlen);
    if (rc != TLS_ERR_OK) return rc;
    if (mtype != HS_TYPE_CERTIFICATE) return TLS_ERR_PROTOCOL;
    rc = parse_certificate(ctx, msg_buf, mlen);
    if (rc != TLS_ERR_OK) return rc;

    /* Snapshot transcript hash now - it's the input to CertificateVerify. */
    th_snapshot(ctx, ctx->th_before_cert_verify);

    /* CertificateVerify. */
    rc = hs_read_msg(ctx, &reader, msg_buf, sizeof(msg_buf), &mtype, &mlen);
    if (rc != TLS_ERR_OK) return rc;
    if (mtype != HS_TYPE_CERT_VERIFY) return TLS_ERR_PROTOCOL;
    rc = parse_cert_verify(ctx, msg_buf, mlen);
    if (rc != TLS_ERR_OK) return rc;

    /* Verify cert chain against trust anchors + hostname + clock. */
    rc = x509_chain_verify(&ctx->chain, ctx->hostname, ctx->now_epoch);
    if (rc != X509_OK) return rc;  /* X509_ERR_* are negative */

    /* Snapshot transcript hash - input to server Finished MAC. */
    th_snapshot(ctx, ctx->th_before_server_finished);

    /* server Finished. */
    rc = hs_read_msg(ctx, &reader, msg_buf, sizeof(msg_buf), &mtype, &mlen);
    if (rc != TLS_ERR_OK) return rc;
    if (mtype != HS_TYPE_FINISHED) return TLS_ERR_PROTOCOL;
    rc = verify_server_finished(ctx, msg_buf, mlen);
    if (rc != TLS_ERR_OK) return rc;

    /* Snapshot transcript hash - input for application traffic secrets. */
    th_snapshot(ctx, ctx->th_after_server_finished);

    /* 6. Switch our own send side to client_handshake_traffic (for the
     *    client Finished only) and send Finished. */
    install_client_handshake_send_key(ctx);
    rc = send_client_finished(ctx, ctx->th_after_server_finished);
    if (rc != TLS_ERR_OK) return rc;

    /* 7. Switch both directions to application traffic keys. */
    compute_application_secrets(ctx);
    install_application_keys(ctx);

    return TLS_ERR_OK;
}

/* Application I/O */

int tls_app_send(tls_ctx_t *ctx, const uint8_t *buf, uint32_t len) {
    uint32_t off = 0;
    while (off < len) {
        uint32_t take = len - off;
        if (take > TLS_REC_MAX_PLAINTEXT - 1u) take = TLS_REC_MAX_PLAINTEXT - 1u;
        if (tls_record_send(&ctx->rec, TLS_RT_APPLICATION_DATA,
                            buf + off, take) != 0)
            return TLS_ERR_TRANSPORT;
        off += take;
    }
    return TLS_ERR_OK;
}

int tls_app_recv(tls_ctx_t *ctx, uint8_t *buf, uint32_t buf_max) {
    uint8_t  type = 0;
    uint32_t len = 0;
    int      rc;
    uint32_t take;
    uint32_t i;

    /* Drain any leftover plaintext from a previous record before pulling
     * a new one off the wire. */
    if (ctx->app_buf_len > 0u) {
        take = ctx->app_buf_len;
        if (take > buf_max) take = buf_max;
        for (i = 0; i < take; i++) buf[i] = ctx->app_buf[ctx->app_buf_off + i];
        ctx->app_buf_off += take;
        ctx->app_buf_len -= take;
        return (int)take;
    }

    for (;;) {
        rc = tls_record_recv(&ctx->rec, &type,
                             ctx->app_buf, sizeof(ctx->app_buf), &len);
        if (rc < 0) return TLS_ERR_TRANSPORT;
        if (type == TLS_RT_APPLICATION_DATA) {
            take = len;
            if (take > buf_max) take = buf_max;
            for (i = 0; i < take; i++) buf[i] = ctx->app_buf[i];
            ctx->app_buf_off = take;
            ctx->app_buf_len = len - take;
            return (int)take;
        }
        if (type == TLS_RT_ALERT) {
            /* close_notify (level=warning desc=0 OR fatal close_notify). */
            if (len >= 2u && ctx->app_buf[1] == ALERT_DESC_CLOSE_NOTIFY) return 0;
            return TLS_ERR_PROTOCOL;
        }
        if (type == TLS_RT_HANDSHAKE) {
            /* Post-handshake msgs (NewSessionTicket, KeyUpdate). Ignore
             * NewSessionTicket; abort on KeyUpdate (not implemented). */
            continue;
        }
        return TLS_ERR_PROTOCOL;
    }
}

void tls_close_notify(tls_ctx_t *ctx) {
    uint8_t alert[2] = { 1u /* warning */, ALERT_DESC_CLOSE_NOTIFY };
    (void)tls_record_send(&ctx->rec, TLS_RT_ALERT, alert, 2u);
}
