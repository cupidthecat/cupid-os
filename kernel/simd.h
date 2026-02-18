/**
 * simd.h - SSE2 accelerated rendering primitives for cupid-os
 *
 * Public APIs use plain C types only. SIMD internals stay in simd.c.
 */
#ifndef KERNEL_SIMD_H
#define KERNEL_SIMD_H

#include "types.h"

void simd_init(void);
bool simd_enabled(void);
void simd_context_save(uint8_t *area);
void simd_context_restore(const uint8_t *area);

void simd_memcpy(void *dst, const void *src, uint32_t bytes);
void simd_memset32(uint32_t *dst, uint32_t color, uint32_t count);

void simd_blit_rect(uint32_t *dst, const uint32_t *src,
                    uint32_t dst_stride, uint32_t src_stride,
                    uint32_t w, uint32_t h);

void simd_fill_rect(uint32_t *fb, uint32_t stride,
                    int x, int y, int w, int h, uint32_t color);

void simd_blend_row(uint32_t *dst, const uint32_t *src,
                    uint32_t count, uint8_t alpha);

void simd_add_rows(uint32_t *dst, const uint32_t *src, uint32_t count);

void simd_blur_h_pass(uint32_t *dst, const uint32_t *src,
                      int w, int h, int radius);
void simd_blur_v_pass(uint32_t *dst, const uint32_t *src,
                      int w, int h, int radius);

#ifdef SIMD_BENCH
void simd_benchmark(void);
#endif

#endif
