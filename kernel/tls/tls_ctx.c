/* TLS context lifecycle. The full handshake state machine lives in
 * tls_handshake.c; this file is the public surface. */

#include "tls_ctx.h"
#include "ct.h"
#include "csprng.h"
#include "x25519.h"
#include "p256.h"

static uint32_t z_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

int tls_ctx_init(tls_ctx_t *ctx,
                 void *xport_user,
                 tls_xport_send_fn s,
                 tls_xport_recv_fn rcb,
                 const char *hostname,
                 uint64_t now_epoch) {
    uint32_t hl;
    uint32_t i;
    uint8_t *q = (uint8_t *)ctx;

    /* Zero everything. */
    for (i = 0; i < sizeof(*ctx); i++) q[i] = 0u;

    if (hostname == NULL) return TLS_ERR_PROTOCOL;
    hl = z_strlen(hostname);
    if (hl == 0u || hl >= TLS_CTX_MAX_HOSTNAME) return TLS_ERR_PROTOCOL;
    for (i = 0; i < hl; i++) ctx->hostname[i] = hostname[i];
    ctx->hostname[hl] = '\0';
    ctx->hostname_len = hl;

    ctx->now_epoch = now_epoch;
    ctx->state = 0;
    ctx->last_error = TLS_ERR_OK;

    tls_record_init(&ctx->rec, xport_user, s, rcb);
    x509_chain_init(&ctx->chain);

    /* Generate ephemeral X25519 keypair. */
    crypto_random_bytes(ctx->client_priv, 32u);
    x25519(ctx->client_pub, ctx->client_priv, X25519_BASE_POINT);

    /* Generate ephemeral P-256 keypair: random scalar, then derive
     * uncompressed pubkey via scalar * G. Re-roll if the scalar is 0
     * or >= n (vanishingly rare; a few retries are bounded). */
    {
        p256_scalar_t kp;
        p256_jac_t pub_jac;
        p256_aff_t G_aff, pub_aff;
        uint32_t   tries;
        int        ok = 0;

        p256_fe_copy(G_aff.x, P256_GX);
        p256_fe_copy(G_aff.y, P256_GY);
        G_aff.infinity = 0;

        for (tries = 0; tries < 8u; tries++) {
            crypto_random_bytes(ctx->p256_priv, 32u);
            if (p256_scalar_from_be(kp, ctx->p256_priv) == 0
                && !p256_scalar_iszero(kp)) { ok = 1; break; }
        }
        if (!ok) return TLS_ERR_NO_ENTROPY;

        p256_scalar_mul_point(&pub_jac, kp, &G_aff);
        p256_jac_to_affine(&pub_aff, &pub_jac);
        ctx->p256_pub[0] = 0x04u;
        p256_fe_to_be(&ctx->p256_pub[1],  pub_aff.x);
        p256_fe_to_be(&ctx->p256_pub[33], pub_aff.y);
    }

    /* Random nonce for ClientHello. */
    crypto_random_bytes(ctx->client_random, 32u);

    /* Initialize transcript hash. */
    {
        sha256_ctx_t *th = &ctx->transcript;
        sha256_init(th);
    }

    return TLS_ERR_OK;
}

void tls_ctx_destroy(tls_ctx_t *ctx) {
    ct_wipe(ctx, sizeof(*ctx));
}
