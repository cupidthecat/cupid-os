#ifndef VGA_GFX_H
#define VGA_GFX_H

#include "../kernel/types.h"

/* VGA Mode 13h: 320x200, 256 colors */
#define VGA_GFX_WIDTH   320
#define VGA_GFX_HEIGHT  200
#define VGA_GFX_BPP     1       /* 1 byte per pixel */
#define VGA_GFX_SIZE    (VGA_GFX_WIDTH * VGA_GFX_HEIGHT)
#define VGA_GFX_FB      0xA0000 /* Physical framebuffer address */

/* ── UI Color Palette (indices 0-15) ──────────────────────────────── */
#define COLOR_BLACK         0
#define COLOR_WINDOW_BG     1   /* Soft pink         */
#define COLOR_TITLEBAR      2   /* Light cyan         */
#define COLOR_HIGHLIGHT     3   /* Pale yellow        */
#define COLOR_TASKBAR       4   /* Soft lavender      */
#define COLOR_BORDER        5   /* Medium gray        */
#define COLOR_TEXT          6   /* Dark gray          */
#define COLOR_TEXT_LIGHT    7   /* White              */
#define COLOR_DESKTOP_BG    8   /* Very light pink    */
#define COLOR_BUTTON        9   /* Soft blue          */
#define COLOR_BUTTON_HOVER  10  /* Brighter blue      */
#define COLOR_TITLE_UNFOC   11  /* Gray (unfocused)   */
#define COLOR_CLOSE_BG      12  /* Close button red   */
#define COLOR_TASKBAR_ACT   13  /* Active taskbar btn */
#define COLOR_TERM_BG       14  /* Terminal bg (dark) */
#define COLOR_CURSOR        15  /* White cursor       */

/* Switch from text mode to Mode 13h */
void vga_set_mode_13h(void);

/* Program the custom color palette */
void vga_init_palette(void);

/* Set a single palette entry (r,g,b each 0-63) */
void vga_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

/* Get direct pointer to framebuffer (or back buffer) */
uint8_t *vga_get_framebuffer(void);

/* Clear entire screen to a single color */
void vga_clear_screen(uint8_t color);

/* Copy back buffer to video memory */
void vga_flip(void);

#endif
