/* RFC 8439 §2.8 ChaCha20-Poly1305 AEAD construction.
 *
 * One-time key (otk) is the first 32 bytes of ChaCha20 keystream at
 * counter=0. Payload is encrypted with ChaCha20 starting at counter=1.
 * The MAC covers AAD || pad16(AAD) || CT || pad16(CT) || aad_len_le64
 * || ct_len_le64. */

#include "chacha20poly1305.h"
#include "chacha20.h"
#include "poly1305.h"
#include "ct.h"

static void store_le64(uint8_t out[8], uint64_t v) {
    int i;
    for (i = 0; i < 8; i++) {
        out[i] = (uint8_t)((v >> (8u * (unsigned)i)) & 0xFFu);
    }
}

static void mac_data(poly1305_ctx_t *ctx,
                     const uint8_t *aad, uint32_t aad_len,
                     const uint8_t *ct,  uint32_t ct_len) {
    static const uint8_t zeros[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint8_t lengths[16];

    if (aad_len > 0u) {
        poly1305_update(ctx, aad, aad_len);
        if ((aad_len & 0xFu) != 0u) {
            poly1305_update(ctx, zeros, 16u - (aad_len & 0xFu));
        }
    }
    if (ct_len > 0u) {
        poly1305_update(ctx, ct, ct_len);
        if ((ct_len & 0xFu) != 0u) {
            poly1305_update(ctx, zeros, 16u - (ct_len & 0xFu));
        }
    }
    store_le64(lengths,     (uint64_t)aad_len);
    store_le64(lengths + 8, (uint64_t)ct_len);
    poly1305_update(ctx, lengths, 16u);
}

void chacha20poly1305_seal(const uint8_t key[32], const uint8_t nonce[12],
                           const uint8_t *aad, uint32_t aad_len,
                           const uint8_t *pt, uint32_t pt_len,
                           uint8_t *ct_out, uint8_t tag_out[16]) {
    uint8_t        otk_block[64];
    poly1305_ctx_t mac;

    chacha20_block(key, 0u, nonce, otk_block);
    chacha20_xor(key, 1u, nonce, pt, ct_out, pt_len);

    poly1305_init(&mac, otk_block);
    mac_data(&mac, aad, aad_len, ct_out, pt_len);
    poly1305_final(&mac, tag_out);

    ct_wipe(otk_block, sizeof(otk_block));
    ct_wipe(&mac, sizeof(mac));
}

int chacha20poly1305_open(const uint8_t key[32], const uint8_t nonce[12],
                          const uint8_t *aad, uint32_t aad_len,
                          const uint8_t *ct, uint32_t ct_len,
                          const uint8_t tag[16], uint8_t *pt_out) {
    uint8_t        otk_block[64];
    uint8_t        computed_tag[16];
    poly1305_ctx_t mac;
    int            ok;

    chacha20_block(key, 0u, nonce, otk_block);
    poly1305_init(&mac, otk_block);
    mac_data(&mac, aad, aad_len, ct, ct_len);
    poly1305_final(&mac, computed_tag);

    ok = poly1305_verify(computed_tag, tag);
    if (ok) {
        chacha20_xor(key, 1u, nonce, ct, pt_out, ct_len);
    }

    ct_wipe(otk_block, sizeof(otk_block));
    ct_wipe(computed_tag, sizeof(computed_tag));
    ct_wipe(&mac, sizeof(mac));
    return ok;
}
