/**
 * vga.c - VBE 640x480 32bpp graphics driver for cupid-os
 *
 * Double-buffered rendering:
 *  - All drawing goes to a heap-allocated back buffer (cached RAM, fast)
 *  - vga_flip() memcpy's to the *hidden* LFB page, then swaps Y_OFFSET
 *    atomically — no tearing, no rendering to uncached VRAM
 */

#include "vga.h"
#include "../kernel/memory.h"
#include "../kernel/ports.h"
#include "../kernel/string.h"
#include "../kernel/types.h"

/* Bochs VBE I/O */
#define VBE_PORT_INDEX 0x01CE
#define VBE_PORT_DATA 0x01CF
#define VBE_IDX_VIRT_HEIGHT 7
#define VBE_IDX_Y_OFFSET 9

static inline void vbe_write(uint16_t idx, uint16_t val) {
  outw(VBE_PORT_INDEX, idx);
  outw(VBE_PORT_DATA, val);
}

/* LFB base (identity-mapped, uncached VRAM) */
static uint32_t *lfb_ptr = NULL;

/* Which LFB page is currently displayed (0 = y_offset 0, 1 = y_offset 480) */
static int displayed_page = 0;

/* Heap back buffer — all rendering goes here (fast cached RAM) */
static uint32_t *back_buffer = NULL;

/* ── Initialization ───────────────────────────────────────────────── */

void vga_init_vbe(void) {
  uint32_t addr = *(volatile uint32_t *)0x0500U;
  if (addr == 0U)
    addr = 0xA0000U;
  lfb_ptr = (uint32_t *)addr;

  /* Set virtual height to 2× so both LFB pages exist */
  vbe_write(VBE_IDX_VIRT_HEIGHT, (uint16_t)(VGA_GFX_HEIGHT * 2));

  /* Display page 0 initially */
  displayed_page = 0;
  vbe_write(VBE_IDX_Y_OFFSET, 0);

  /* Allocate heap back buffer for fast rendering */
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
  /* NOTE: Do NOT write directly to LFB pages here!
   * That would cause the clear color to flash on screen
   * before the frame is fully drawn. The back_buffer
   * gets copied to LFB atomically during vga_flip(). */
}

uint32_t *vga_get_display_buffer(void) {
  return lfb_ptr + (displayed_page ? (uint32_t)VGA_GFX_PIXELS : 0U);
}

void vga_flip(void) {
  if (!back_buffer)
    return;

  /* Copy rendered frame to the hidden LFB page (not currently displayed) */
  uint32_t *hidden = lfb_ptr + (displayed_page ? 0U : (uint32_t)VGA_GFX_PIXELS);
  memcpy(hidden, back_buffer, (size_t)VGA_GFX_SIZE);

  /* Wait for VSync (vertical retrace) to prevent tearing */
  /* Port 0x3DA bit 3 = vertical retrace in progress */
  /* First wait for any current retrace to end */
  while ((inb(0x3DA) & 0x08)) {
  }
  /* Then wait for new retrace to start */
  while (!(inb(0x3DA) & 0x08)) {
  }

  /* Atomically show the page we just filled */
  displayed_page ^= 1;
  vbe_write(VBE_IDX_Y_OFFSET, displayed_page ? (uint16_t)VGA_GFX_HEIGHT : 0U);
}
