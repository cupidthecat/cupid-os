/**
 * vga.c - VBE 640x480 32bpp graphics driver for cupid-os
 *
 * Implements:
 *  - VBE linear framebuffer setup (address stored at 0x0500 by bootloader)
 *  - 32bpp true-color rendering with pastel/soft aesthetic
 *  - Double buffering: all draws go to back buffer, vga_flip() blits to LFB
 */

#include "vga.h"
#include "../kernel/string.h"
#include "../kernel/memory.h"
#include "../kernel/types.h"

/* Linear framebuffer (identity-mapped by paging_init) */
static uint32_t *lfb_ptr = NULL;

/* Back buffer — allocated on heap (2MB heap fits the ~1.2MB buffer) */
static uint32_t *back_buffer = NULL;

/* ── Initialization ───────────────────────────────────────────────── */

void vga_init_vbe(void) {
    uint32_t addr = *(volatile uint32_t *)0x0500U;
    if (addr == 0U) addr = 0xA0000U;
    lfb_ptr = (uint32_t *)addr;

    if (!back_buffer) {
        back_buffer = (uint32_t *)kmalloc(VGA_GFX_SIZE);
    }

    vga_clear_screen(COLOR_BLACK);
}

/* ── Framebuffer access ───────────────────────────────────────────── */

uint32_t *vga_get_framebuffer(void) {
    return back_buffer ? back_buffer : lfb_ptr;
}

void vga_clear_screen(uint32_t color) {
    uint32_t i;
    uint32_t *dst = back_buffer ? back_buffer : lfb_ptr;
    for (i = 0U; i < (uint32_t)VGA_GFX_PIXELS; i++) {
        dst[i] = color;
    }
    if (back_buffer) {
        for (i = 0U; i < (uint32_t)VGA_GFX_PIXELS; i++) {
            lfb_ptr[i] = color;
        }
    }
}

void vga_flip(void) {
    if (back_buffer) {
        memcpy(lfb_ptr, back_buffer, (size_t)VGA_GFX_SIZE);
    }
}
