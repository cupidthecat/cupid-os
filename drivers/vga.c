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
#include "../drivers/timer.h"

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

/* Heap back buffer — all rendering goes here (fast cached RAM) */
static uint32_t *back_buffer = NULL;
/* VSync wait disabled — we use single-buffer rendering which avoids both
 * the Y_OFFSET flip overhead and the VSync busy-wait latency. */
static bool vga_wait_vsync = false;

/* Frame-rate cap: timestamp of the last vga_flip() call (ms). */
static uint32_t last_flip_ms = 0;

/* Returns true if at least ~16ms have passed since the last flip (≈60 fps cap).
 * Use this before expensive render work to skip frames that would overshoot
 * the display refresh budget. */
bool vga_flip_ready(void) {
  uint32_t now = timer_get_uptime_ms();
  return (now - last_flip_ms) >= 16u;
}

/* Dirty tracking for partial present (union of marked regions). */
static bool dirty_full = true;
static bool dirty_active = false;
static int dirty_x0 = 0;
static int dirty_y0 = 0;
static int dirty_x1 = 0;
static int dirty_y1 = 0;

void vga_mark_dirty(int x, int y, int w, int h) {
  int x0, y0, x1, y1;

  if (dirty_full)
    return;
  if (w <= 0 || h <= 0)
    return;

  x0 = x;
  y0 = y;
  x1 = x + w;
  y1 = y + h;

  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > VGA_GFX_WIDTH)
    x1 = VGA_GFX_WIDTH;
  if (y1 > VGA_GFX_HEIGHT)
    y1 = VGA_GFX_HEIGHT;
  if (x1 <= x0 || y1 <= y0)
    return;

  if (!dirty_active) {
    dirty_x0 = x0;
    dirty_y0 = y0;
    dirty_x1 = x1;
    dirty_y1 = y1;
    dirty_active = true;
    return;
  }

  if (x0 < dirty_x0)
    dirty_x0 = x0;
  if (y0 < dirty_y0)
    dirty_y0 = y0;
  if (x1 > dirty_x1)
    dirty_x1 = x1;
  if (y1 > dirty_y1)
    dirty_y1 = y1;
}

void vga_mark_dirty_full(void) {
  dirty_full = true;
  dirty_active = false;
  dirty_x0 = 0;
  dirty_y0 = 0;
  dirty_x1 = VGA_GFX_WIDTH;
  dirty_y1 = VGA_GFX_HEIGHT;
}

/* ── Initialization ───────────────────────────────────────────────── */

void vga_init_vbe(void) {
  uint32_t addr = *(volatile uint32_t *)0x0500U;
  if (addr == 0U)
    addr = 0xA0000U;
  lfb_ptr = (uint32_t *)addr;

  /* Single-buffer mode: use only page 0, Y_OFFSET stays at 0 forever.
   * This avoids Y_OFFSET port-I/O flips which trigger full QEMU display
   * re-renders (including expensive software scaling in fullscreen). */
  vbe_write(VBE_IDX_Y_OFFSET, 0);

  /* Allocate heap back buffer for fast rendering */
  if (!back_buffer) {
    back_buffer = (uint32_t *)kmalloc(VGA_GFX_SIZE);
  }

  vga_clear_screen(COLOR_BLACK);
  vga_mark_dirty_full();
}

/* ── Framebuffer access ───────────────────────────────────────────── */

uint32_t *vga_get_framebuffer(void) {
  return back_buffer ? back_buffer : lfb_ptr;
}

void vga_clear_screen(uint32_t color) {
  uint32_t *dst = back_buffer ? back_buffer : lfb_ptr;
  uint32_t n = (uint32_t)VGA_GFX_PIXELS;
  /* Unrolled 8-pixel fill for better throughput */
  while (n >= 8U) {
    dst[0] = color; dst[1] = color; dst[2] = color; dst[3] = color;
    dst[4] = color; dst[5] = color; dst[6] = color; dst[7] = color;
    dst += 8; n -= 8U;
  }
  while (n-- > 0U) { *dst++ = color; }
  vga_mark_dirty_full();
  /* NOTE: Do NOT write directly to LFB pages here!
   * That would cause the clear color to flash on screen
   * before the frame is fully drawn. The back_buffer
   * gets copied to LFB atomically during vga_flip(). */
}

uint32_t *vga_get_display_buffer(void) {
  /* Single-buffer: the display buffer is always page 0. */
  return lfb_ptr;
}

void vga_flip(void) {
  if (!back_buffer)
    return;

  /* Single-buffer present: copy back_buffer → page 0 (always displayed).
   * No Y_OFFSET flip — eliminates the port I/O that triggers a full QEMU
   * display re-render on every frame, including expensive software scaling
   * in QEMU fullscreen mode. */
  uint32_t *page0 = lfb_ptr;

  if (dirty_full || !dirty_active) {
    memcpy(page0, back_buffer, (size_t)VGA_GFX_SIZE);
  } else {
    int x0 = dirty_x0;
    int y0 = dirty_y0;
    int x1 = dirty_x1;
    int y1 = dirty_y1;
    size_t bytes = (size_t)(x1 - x0) * sizeof(uint32_t);
    for (int row = y0; row < y1; row++) {
      uint32_t *dst = page0 + (uint32_t)row * (uint32_t)VGA_GFX_WIDTH +
                      (uint32_t)x0;
      uint32_t *src = back_buffer + (uint32_t)row * (uint32_t)VGA_GFX_WIDTH +
                      (uint32_t)x0;
      memcpy(dst, src, bytes);
    }
  }

  dirty_active = false;
  dirty_full = false;
  last_flip_ms = timer_get_uptime_ms();
}

void vga_set_vsync_wait(bool enabled) { vga_wait_vsync = enabled; }
