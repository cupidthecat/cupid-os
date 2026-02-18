/**
 * simd.c - SSE2 accelerated rendering primitives
 */

#include "simd.h"

static bool simd_use_sse2 = false;

static bool simd_cpu_has_cpuid(void) {
    uint32_t before;
    uint32_t after;

    __asm__ volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        : "=r"(before));

    after = before ^ (1u << 21);

    __asm__ volatile(
        "pushl %0\n\t"
        "popfl\n\t"
        :
        : "r"(after)
        : "cc");

    __asm__ volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        : "=r"(after));

    __asm__ volatile(
        "pushl %0\n\t"
        "popfl\n\t"
        :
        : "r"(before)
        : "cc");

    return ((before ^ after) & (1u << 21)) != 0u;
}

static void simd_cpuid(uint32_t leaf,
                       uint32_t *eax, uint32_t *ebx,
                       uint32_t *ecx, uint32_t *edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    __asm__ volatile(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf));
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

void simd_init(void) {
    simd_use_sse2 = false;

#ifdef __SSE2__
    if (!simd_cpu_has_cpuid()) {
        return;
    }

    {
        uint32_t eax;
        uint32_t edx;
        simd_cpuid(1u, &eax, NULL, NULL, &edx);
        (void)eax;
        if ((edx & (1u << 26)) == 0u) {
            return;
        }
    }

    {
        uint32_t cr0;
        uint32_t cr4;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

        cr0 &= ~(1u << 2);   /* clear EM */
        cr0 |= (1u << 1);    /* set MP */
        cr4 |= (1u << 9);    /* OSFXSR */
        cr4 |= (1u << 10);   /* OSXMMEXCPT */

        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    }

    simd_use_sse2 = true;
#endif
}

bool simd_enabled(void) {
    return simd_use_sse2;
}

void simd_context_save(uint8_t *area) {
#ifdef __SSE2__
    if (!simd_use_sse2) {
        (void)area;
        return;
    }
    __asm__ volatile("fxsave (%0)" : : "r"(area) : "memory");
#else
    (void)area;
#endif
}

void simd_context_restore(const uint8_t *area) {
#ifdef __SSE2__
    if (!simd_use_sse2) {
        (void)area;
        return;
    }
    __asm__ volatile("fxrstor (%0)" : : "r"(area) : "memory");
#else
    (void)area;
#endif
}

static void simd_sfence(void) {
#ifdef __SSE2__
    if (!simd_use_sse2) {
        return;
    }
    __asm__ volatile("sfence" ::: "memory");
#endif
}

void simd_memcpy(void *dst, const void *src, uint32_t bytes) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

#ifdef __SSE2__
    if (simd_use_sse2) {
    uint32_t streamed = 0u;

    while (bytes > 0u && (((uint32_t)d) & 15u) != 0u) {
        *d++ = *s++;
        bytes--;
    }

    while (bytes >= 64u) {
        __asm__ volatile(
            "movdqu   (%1), %%xmm0\n\t"
            "movdqu 16(%1), %%xmm1\n\t"
            "movdqu 32(%1), %%xmm2\n\t"
            "movdqu 48(%1), %%xmm3\n\t"
            "movntdq %%xmm0,   (%0)\n\t"
            "movntdq %%xmm1, 16(%0)\n\t"
            "movntdq %%xmm2, 32(%0)\n\t"
            "movntdq %%xmm3, 48(%0)\n\t"
            :
            : "r"(d), "r"(s)
            : "memory", "xmm0", "xmm1", "xmm2", "xmm3");
        d += 64;
        s += 64;
        bytes -= 64u;
        streamed = 1u;
    }

    while (bytes >= 16u) {
        __asm__ volatile(
            "movdqu (%1), %%xmm0\n\t"
            "movntdq %%xmm0, (%0)\n\t"
            :
            : "r"(d), "r"(s)
            : "memory", "xmm0");
        d += 16;
        s += 16;
        bytes -= 16u;
        streamed = 1u;
    }

    if (streamed) {
        simd_sfence();
    }
    }
#endif

    while (bytes-- > 0u) {
        *d++ = *s++;
    }
}

void simd_memset32(uint32_t *dst, uint32_t color, uint32_t count) {
#ifdef __SSE2__
    if (simd_use_sse2) {
    uint32_t streamed = 0u;

    while (count > 0u && (((uint32_t)dst) & 15u) != 0u) {
        *dst++ = color;
        count--;
    }

    if (count >= 4u) {
        __asm__ volatile(
            "movd %0, %%xmm0\n\t"
            "pshufd $0x00, %%xmm0, %%xmm0\n\t"
            :
            : "r"(color)
            : "xmm0");

        while (count >= 4u) {
            __asm__ volatile(
                "movntdq %%xmm0, (%0)\n\t"
                :
                : "r"(dst)
                : "memory");
            dst += 4;
            count -= 4u;
            streamed = 1u;
        }
    }

    if (streamed) {
        simd_sfence();
    }
    }
#endif

    while (count-- > 0u) {
        *dst++ = color;
    }
}

void simd_blit_rect(uint32_t *dst, const uint32_t *src,
                    uint32_t dst_stride, uint32_t src_stride,
                    uint32_t w, uint32_t h) {
    while (h-- > 0u) {
        simd_memcpy(dst, src, w * 4u);
        dst += dst_stride;
        src += src_stride;
    }
}

void simd_fill_rect(uint32_t *fb, uint32_t stride,
                    int x, int y, int w, int h, uint32_t color) {
    fb += (uint32_t)y * stride + (uint32_t)x;
    while (h-- > 0) {
        simd_memset32(fb, color, (uint32_t)w);
        fb += stride;
    }
}

void simd_blend_row(uint32_t *dst, const uint32_t *src,
                    uint32_t count, uint8_t alpha) {
    uint32_t ia = 255u - (uint32_t)alpha;

#ifdef __SSE2__
    if (simd_use_sse2 && count >= 4u) {
        while (count >= 4u) {
            __asm__ volatile(
                "movd %2, %%xmm5\n\t"
                "movd %3, %%xmm6\n\t"
                "movd %4, %%xmm7\n\t"
                "punpcklwd %%xmm5, %%xmm5\n\t"
                "punpcklwd %%xmm6, %%xmm6\n\t"
                "punpcklwd %%xmm7, %%xmm7\n\t"
                "pshufd $0x00, %%xmm5, %%xmm5\n\t"
                "pshufd $0x00, %%xmm6, %%xmm6\n\t"
                "pshufd $0x00, %%xmm7, %%xmm7\n\t"
                "pxor %%xmm4, %%xmm4\n\t"
                "movdqu (%1), %%xmm0\n\t"
                "movdqu (%0), %%xmm1\n\t"
                "movdqa %%xmm0, %%xmm2\n\t"
                "punpcklbw %%xmm4, %%xmm2\n\t"
                "movdqa %%xmm1, %%xmm3\n\t"
                "punpcklbw %%xmm4, %%xmm3\n\t"
                "pmullw %%xmm5, %%xmm2\n\t"
                "pmullw %%xmm6, %%xmm3\n\t"
                "paddw %%xmm3, %%xmm2\n\t"
                "paddw %%xmm7, %%xmm2\n\t"
                "psrlw $8, %%xmm2\n\t"
                "punpckhbw %%xmm4, %%xmm0\n\t"
                "punpckhbw %%xmm4, %%xmm1\n\t"
                "pmullw %%xmm5, %%xmm0\n\t"
                "pmullw %%xmm6, %%xmm1\n\t"
                "paddw %%xmm1, %%xmm0\n\t"
                "paddw %%xmm7, %%xmm0\n\t"
                "psrlw $8, %%xmm0\n\t"
                "packuswb %%xmm0, %%xmm2\n\t"
                "movdqu %%xmm2, (%0)\n\t"
                :
                : "r"(dst), "r"(src), "r"((uint32_t)alpha), "r"(ia), "r"(128u)
                : "memory", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7");
            src += 4;
            dst += 4;
            count -= 4u;
        }
    }
#endif

    while (count-- > 0u) {
        uint32_t s = *src++;
        uint32_t d = *dst;
        uint32_t r = (((s >> 16) & 0xFFu) * (uint32_t)alpha +
                      ((d >> 16) & 0xFFu) * ia + 128u) >> 8;
        uint32_t g = (((s >> 8) & 0xFFu) * (uint32_t)alpha +
                      ((d >> 8) & 0xFFu) * ia + 128u) >> 8;
        uint32_t b = ((s & 0xFFu) * (uint32_t)alpha +
                      (d & 0xFFu) * ia + 128u) >> 8;
        *dst++ = (r << 16) | (g << 8) | b;
    }
}

void simd_add_rows(uint32_t *dst, const uint32_t *src, uint32_t count) {
#ifdef __SSE2__
    if (simd_use_sse2) {
        while (count >= 4u) {
            __asm__ volatile(
                "movdqu (%1), %%xmm0\n\t"
                "movdqu (%0), %%xmm1\n\t"
                "paddusb %%xmm0, %%xmm1\n\t"
                "movdqu %%xmm1, (%0)\n\t"
                :
                : "r"(dst), "r"(src)
                : "memory", "xmm0", "xmm1");
            dst += 4;
            src += 4;
            count -= 4u;
        }
    }
#endif

    while (count-- > 0u) {
        uint32_t d = *dst;
        uint32_t s = *src++;
        uint32_t r = ((d >> 16) & 0xFFu) + ((s >> 16) & 0xFFu);
        uint32_t g = ((d >> 8) & 0xFFu) + ((s >> 8) & 0xFFu);
        uint32_t b = (d & 0xFFu) + (s & 0xFFu);
        if (r > 255u) r = 255u;
        if (g > 255u) g = 255u;
        if (b > 255u) b = 255u;
        *dst++ = (r << 16) | (g << 8) | b;
    }
}

void simd_blur_h_pass(uint32_t *dst, const uint32_t *src,
                      int w, int h, int radius) {
    int row;
    int col;
    int ksize = 2 * radius + 1;

    for (row = 0; row < h; row++) {
        const uint32_t *srow = src + (uint32_t)row * (uint32_t)w;
        uint32_t *drow = dst + (uint32_t)row * (uint32_t)w;
        int sr = 0;
        int sg = 0;
        int sb = 0;
        int k;

        for (k = -radius; k <= radius; k++) {
            int sc = k < 0 ? 0 : (k >= w ? w - 1 : k);
            uint32_t px = srow[sc];
            sr += (int)((px >> 16) & 0xFFu);
            sg += (int)((px >> 8) & 0xFFu);
            sb += (int)(px & 0xFFu);
        }

        for (col = 0; col < w; col++) {
            drow[col] = ((uint32_t)(sr / ksize) << 16) |
                        ((uint32_t)(sg / ksize) << 8) |
                        (uint32_t)(sb / ksize);

            {
                int rm = col - radius;
                uint32_t px = srow[rm < 0 ? 0 : rm];
                sr -= (int)((px >> 16) & 0xFFu);
                sg -= (int)((px >> 8) & 0xFFu);
                sb -= (int)(px & 0xFFu);
            }
            {
                int ad = col + radius + 1;
                uint32_t px = srow[ad >= w ? w - 1 : ad];
                sr += (int)((px >> 16) & 0xFFu);
                sg += (int)((px >> 8) & 0xFFu);
                sb += (int)(px & 0xFFu);
            }
        }
    }
}

void simd_blur_v_pass(uint32_t *dst, const uint32_t *src,
                      int w, int h, int radius) {
    int row;
    int col;
    int ksize = 2 * radius + 1;

    for (col = 0; col < w; col++) {
        int sr = 0;
        int sg = 0;
        int sb = 0;
        int k;

        for (k = -radius; k <= radius; k++) {
            int sr_row = k < 0 ? 0 : (k >= h ? h - 1 : k);
            uint32_t px = src[(uint32_t)sr_row * (uint32_t)w + (uint32_t)col];
            sr += (int)((px >> 16) & 0xFFu);
            sg += (int)((px >> 8) & 0xFFu);
            sb += (int)(px & 0xFFu);
        }

        for (row = 0; row < h; row++) {
            dst[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                ((uint32_t)(sr / ksize) << 16) |
                ((uint32_t)(sg / ksize) << 8) |
                (uint32_t)(sb / ksize);

            {
                int rm = row - radius;
                uint32_t px = src[(uint32_t)(rm < 0 ? 0 : rm) *
                                  (uint32_t)w + (uint32_t)col];
                sr -= (int)((px >> 16) & 0xFFu);
                sg -= (int)((px >> 8) & 0xFFu);
                sb -= (int)(px & 0xFFu);
            }
            {
                int ad = row + radius + 1;
                uint32_t px = src[(uint32_t)(ad >= h ? h - 1 : ad) *
                                  (uint32_t)w + (uint32_t)col];
                sr += (int)((px >> 16) & 0xFFu);
                sg += (int)((px >> 8) & 0xFFu);
                sb += (int)(px & 0xFFu);
            }
        }
    }
}

#ifdef SIMD_BENCH
#include "string.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"

void simd_benchmark(void) {
    static uint32_t bench_buf[256u * 1024u];
    static uint32_t bench_src[256u * 1024u];
    static uint32_t blend_dst[256u];
    static uint32_t blend_src[256u];
    uint32_t i;
    uint32_t t0;
    uint32_t t1;
    uint32_t ok;

    for (i = 0; i < 256u * 1024u; i++) {
        bench_src[i] = 0x00AABBCCu;
        bench_buf[i] = 0u;
    }

    simd_memcpy(bench_buf, bench_src, 1024u * 1024u);
    ok = 1u;
    for (i = 0; i < 256u * 1024u; i++) {
        if (bench_buf[i] != bench_src[i]) {
            ok = 0u;
            break;
        }
    }
    serial_printf("simd_memcpy correctness: %s\n", ok ? "PASS" : "FAIL");

    simd_memset32(bench_buf, 0xDEADBEEFu, 256u * 1024u);
    ok = 1u;
    for (i = 0; i < 256u * 1024u; i++) {
        if (bench_buf[i] != 0xDEADBEEFu) {
            ok = 0u;
            break;
        }
    }
    serial_printf("simd_memset32 correctness: %s\n", ok ? "PASS" : "FAIL");

    for (i = 0; i < 256u; i++) {
        blend_src[i] = (i * 1234567u) & 0x00FFFFFFu;
        blend_dst[i] = (~blend_src[i]) & 0x00FFFFFFu;
    }

    {
        static uint32_t ref[256u];
        const uint32_t alpha = 96u;
        const uint32_t ia = 255u - alpha;
        for (i = 0; i < 256u; i++) {
            uint32_t s = blend_src[i];
            uint32_t d = blend_dst[i];
            uint32_t r = (((s >> 16) & 0xFFu) * alpha +
                          ((d >> 16) & 0xFFu) * ia + 128u) >> 8;
            uint32_t g = (((s >> 8) & 0xFFu) * alpha +
                          ((d >> 8) & 0xFFu) * ia + 128u) >> 8;
            uint32_t b = ((s & 0xFFu) * alpha + (d & 0xFFu) * ia + 128u) >> 8;
            ref[i] = (r << 16) | (g << 8) | b;
        }
        simd_blend_row(blend_dst, blend_src, 256u, (uint8_t)alpha);
        ok = 1u;
        for (i = 0; i < 256u; i++) {
            if ((blend_dst[i] & 0x00FFFFFFu) != (ref[i] & 0x00FFFFFFu)) {
                ok = 0u;
                break;
            }
        }
        serial_printf("simd_blend_row correctness: %s\n", ok ? "PASS" : "FAIL");
    }

    t0 = timer_get_uptime_ms();
    for (i = 0; i < 100u; i++) {
        simd_memcpy(bench_buf, bench_src, 1024u * 1024u);
    }
    t1 = timer_get_uptime_ms();
    serial_printf("simd_memcpy 100x 1MB: %ums total, %uus/frame\n",
                  t1 - t0, (t1 - t0) * 10u);
}
#endif
