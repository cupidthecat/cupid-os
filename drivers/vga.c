/**
 * vga.c - VGA Mode 13h graphics driver for cupid-os
 *
 * Implements:
 *  - Mode 13h switching (320x200, 256 colors)
 *  - Programmable palette with pastel/soft aesthetic
 *  - Double buffering via heap-allocated back buffer
 */

#include "vga.h"
#include "../kernel/ports.h"
#include "../kernel/string.h"
#include "../kernel/memory.h"
#include "../kernel/types.h"

/* ── VGA register ports ───────────────────────────────────────────── */
#define VGA_DAC_WRITE_INDEX  0x3C8
#define VGA_DAC_DATA         0x3C9

/* Back buffer (allocated on heap) */
static uint8_t *back_buffer = NULL;

/* ── Mode switching ───────────────────────────────────────────────── */

void vga_set_mode_13h(void) {
    /*
     * Mode 13h is now set by the bootloader via BIOS INT 10h (AX=0x0013)
     * before entering protected mode.  This is the most reliable method
     * because the VGA BIOS handles all register programming.
     *
     * Here we only need to set up the back buffer and clear the screen.
     * The palette is programmed separately in vga_init_palette().
     */

    /* Allocate back buffer */
    if (!back_buffer) {
        back_buffer = (uint8_t *)kmalloc(VGA_GFX_SIZE);
    }

    /* Clear both buffers */
    vga_clear_screen(COLOR_BLACK);
}

/* ── Palette ──────────────────────────────────────────────────────── */

void vga_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(VGA_DAC_WRITE_INDEX, index);
    outb(VGA_DAC_DATA, r);
    outb(VGA_DAC_DATA, g);
    outb(VGA_DAC_DATA, b);
}

void vga_init_palette(void) {
    /* Pastel / soft aesthetic palette */
    /*                       idx   R    G    B   (0-63 each)  */
    vga_set_palette_color(  0,   0,   0,   0); /* Black          */
    vga_set_palette_color(  1,  63,  52,  55); /* Soft pink       */
    vga_set_palette_color(  2,  44,  58,  63); /* Light cyan      */
    vga_set_palette_color(  3,  63,  62,  48); /* Pale yellow     */
    vga_set_palette_color(  4,  50,  44,  60); /* Soft lavender   */
    vga_set_palette_color(  5,  36,  36,  38); /* Medium gray     */
    vga_set_palette_color(  6,  16,  16,  18); /* Dark gray text  */
    vga_set_palette_color(  7,  63,  63,  63); /* White           */
    vga_set_palette_color(  8,  60,  50,  54); /* Very light pink */
    vga_set_palette_color(  9,  40,  48,  60); /* Soft blue       */
    vga_set_palette_color( 10,  46,  54,  63); /* Brighter blue   */
    vga_set_palette_color( 11,  42,  42,  44); /* Gray unfocused  */
    vga_set_palette_color( 12,  58,  32,  34); /* Close btn red   */
    vga_set_palette_color( 13,  52,  48,  60); /* Active taskbar  */
    vga_set_palette_color( 14,   6,   6,   8); /* Terminal bg     */
    vga_set_palette_color( 15,  63,  63,  63); /* Cursor white    */
}

/* ── Framebuffer access ───────────────────────────────────────────── */

uint8_t *vga_get_framebuffer(void) {
    return back_buffer ? back_buffer : (uint8_t *)VGA_GFX_FB;
}

void vga_clear_screen(uint8_t color) {
    if (back_buffer) {
        memset(back_buffer, (int)color, VGA_GFX_SIZE);
    }
    memset((void *)VGA_GFX_FB, (int)color, VGA_GFX_SIZE);
}

void vga_flip(void) {
    if (back_buffer) {
        memcpy((void *)VGA_GFX_FB, back_buffer, VGA_GFX_SIZE);
    }
}
