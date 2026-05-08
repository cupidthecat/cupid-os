/* ChaCha20-based CSPRNG for CupidOS.
 *
 * Replaces the LCG previously living in devfs.c. The state is a 32-byte
 * key + 12-byte nonce. Each call rolls the key forward via fast key
 * erasure (DJB-style): a fresh 64-byte ChaCha20 block is produced; the
 * first 32 bytes become the next key, the second 32 are the output.
 * That makes past output unrecoverable from a state compromise.
 *
 * Seeding pulls entropy from RDRAND when CPUID reports it (with a
 * sanity check to defend against emulators that hardwire it to a
 * constant), and falls back to RDTSC jitter otherwise.
 */

#include "csprng.h"
#include "chacha20.h"
#include "../../drivers/serial.h"

static uint8_t  g_key[32];
static uint8_t  g_nonce[12];
static uint32_t g_bytes_emitted;
static int      g_initialized;

#define CSPRNG_RESEED_BYTES (1u << 20)   /* reseed every 1 MiB */

/* low-level CPU helpers */

static uint64_t rdtsc64(void) {
    uint32_t lo = 0, hi = 0;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static int cpu_has_rdrand(void) {
    uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ __volatile__("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "0"(eax));
    (void)ebx; (void)edx;
    return (ecx & (1u << 30)) ? 1 : 0;
}

static int rdrand32(uint32_t *out) {
    uint32_t v = 0;
    uint8_t  ok = 0;
    int i;
    for (i = 0; i < 10; i++) {
        __asm__ __volatile__("rdrand %0; setc %1"
            : "=r"(v), "=qm"(ok));
        if (ok) {
            *out = v;
            return 1;
        }
    }
    return 0;
}

static int rdrand_sanity(void) {
    uint32_t a = 0, b = 0;
    if (!rdrand32(&a)) return 0;
    if (!rdrand32(&b)) return 0;
    return (a != b) ? 1 : 0;
}

/* entropy absorb */

/* Fold caller entropy into (g_key, g_nonce). Strategy:
 *   1. Build a 64-byte buffer seeded by rdtsc + caller bytes.
 *   2. XOR a ChaCha20 keystream block (using current state) over it.
 *   3. Run ChaCha20 a second time using the diffused buffer as a
 *      fresh key; the 64-byte output supplies our new key + nonce.
 * No SHA dependency - Phase 1 lives below the hash module. */
static void absorb(const uint8_t *entropy, uint32_t len) {
    uint8_t  buf[64];
    uint8_t  ks[64];
    uint8_t  out[64];
    uint8_t  newnonce[12];
    uint64_t t;
    int      i;
    uint32_t j;

    t = rdtsc64();
    for (i = 0; i < 8; i++) {
        buf[i] = (uint8_t)((t >> (8 * (unsigned)i)) & 0xFFu);
    }
    for (i = 8; i < 64; i++) {
        buf[i] = (uint8_t)(buf[i - 8] ^ (uint8_t)i);
    }
    for (j = 0; j < len; j++) {
        unsigned idx = (unsigned)(j & 63u);
        buf[idx] = (uint8_t)(buf[idx] ^ entropy[j]);
    }

    chacha20_block(g_key, 0u, g_nonce, ks);
    for (i = 0; i < 64; i++) buf[i] = (uint8_t)(buf[i] ^ ks[i]);

    for (i = 0; i < 12; i++) newnonce[i] = buf[32 + i];
    chacha20_block(buf, 1u, newnonce, out);

    for (i = 0; i < 32; i++) g_key[i]   = out[i];
    for (i = 0; i < 12; i++) g_nonce[i] = out[32 + i];
}

/* seeding */

static void seed_from_rdrand(void) {
    uint8_t  buf[64];
    int      i, j;
    for (i = 0; i < 16; i++) {
        uint32_t v = 0;
        (void)rdrand32(&v);
        for (j = 0; j < 4; j++) {
            buf[i * 4 + j] = (uint8_t)((v >> (8u * (unsigned)j)) & 0xFFu);
        }
    }
    absorb(buf, 64u);
}

static void seed_from_rdtsc_jitter(void) {
    uint8_t buf[512];   /* 64 samples x 8 bytes */
    int     i, j;
    for (i = 0; i < 64; i++) {
        uint64_t t1 = rdtsc64();
        volatile int k;
        for (k = 0; k < 100; k++) { /* burn cycles to amplify jitter */ }
        {
            uint64_t t2 = rdtsc64();
            uint64_t v  = t1 ^ (t2 * 0x9E3779B97F4A7C15ull);
            for (j = 0; j < 8; j++) {
                buf[i * 8 + j] = (uint8_t)((v >> (8u * (unsigned)j)) & 0xFFu);
            }
        }
    }
    absorb(buf, sizeof(buf));
}

static void reseed(void) {
    if (cpu_has_rdrand() && rdrand_sanity()) {
        seed_from_rdrand();
    } else {
        seed_from_rdtsc_jitter();
    }
    g_bytes_emitted = 0;
}

/* public API */

void csprng_init(void) {
    int i;
    for (i = 0; i < 32; i++) g_key[i]   = 0;
    for (i = 0; i < 12; i++) g_nonce[i] = 0;
    g_bytes_emitted = 0;
    g_initialized   = 1;

    if (cpu_has_rdrand() && rdrand_sanity()) {
        seed_from_rdrand();
        serial_printf("[csprng] seeded from RDRAND\n");
    } else {
        seed_from_rdtsc_jitter();
        serial_printf("[csprng] seeded from rdtsc jitter (no RDRAND)\n");
    }
}

void crypto_random_add_entropy(const uint8_t *buf, uint32_t len) {
    if (!g_initialized) return;
    absorb(buf, len);
}

void crypto_random_bytes(uint8_t *buf, uint32_t len) {
    uint32_t off;

    if (!g_initialized) {
        uint32_t i;
        for (i = 0; i < len; i++) buf[i] = 0;
        return;
    }

    if (g_bytes_emitted >= CSPRNG_RESEED_BYTES) {
        reseed();
    }

    off = 0;
    while (off < len) {
        uint8_t  block[64];
        uint32_t n;
        uint32_t i;
        int      k;

        chacha20_block(g_key, 0u, g_nonce, block);

        /* Fast key erasure: top 32 bytes become next key. */
        for (k = 0; k < 32; k++) g_key[k] = block[k];

        /* Increment nonce as a 96-bit little-endian counter. */
        for (k = 0; k < 12; k++) {
            g_nonce[k] = (uint8_t)(g_nonce[k] + 1u);
            if (g_nonce[k] != 0) break;
        }

        n = len - off;
        if (n > 32u) n = 32u;
        for (i = 0; i < n; i++) buf[off + i] = block[32 + i];
        off += n;
    }

    g_bytes_emitted += len;
}
