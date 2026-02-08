/**
 * gfx2d.c - 2D graphics library for cupid-os
 *
 * Software-rendered 2D graphics with alpha blending, gradients,
 * drop shadows, retro effects, sprites, and demo-scene aesthetics.
 * All drawing respects the global clip rectangle.
 */

#include "gfx2d.h"
#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "font_8x8.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"

/* ── Internal state ───────────────────────────────────────────────── */

static uint32_t *g2d_fb;

static int g2d_clip_active;
static int g2d_clip_x, g2d_clip_y, g2d_clip_w, g2d_clip_h;

/* Sprite pool */
#define GFX2D_MAX_SPRITES 32
static uint32_t *g2d_sprite_data[GFX2D_MAX_SPRITES];
static int g2d_sprite_w[GFX2D_MAX_SPRITES];
static int g2d_sprite_h[GFX2D_MAX_SPRITES];
static int g2d_sprite_used[GFX2D_MAX_SPRITES];

/* Blend mode state */
static int g2d_blend_mode_val = GFX2D_BLEND_NORMAL;

/* Offscreen surface pool */
static uint32_t *g2d_surf_data[GFX2D_MAX_SURFACES];
static int g2d_surf_w[GFX2D_MAX_SURFACES];
static int g2d_surf_h[GFX2D_MAX_SURFACES];
static int g2d_surf_used[GFX2D_MAX_SURFACES];

/* The active render target (NULL = main framebuffer) */
static uint32_t *g2d_active_fb = NULL;
static int g2d_active_w = 640;
static int g2d_active_h = 480;

/* Particle systems */
typedef struct {
  int x, y;   /* position (fixed-point, >>8 for screen coord) */
  int vx, vy; /* velocity (fixed-point, >>8 per frame) */
  uint32_t color;
  int life; /* remaining frames */
  int max_life;
} g2d_particle_t;

typedef struct {
  g2d_particle_t particles[GFX2D_MAX_PARTICLES_PER_SYS];
  int used;
} g2d_psys_t;

static g2d_psys_t g2d_psys[GFX2D_MAX_PARTICLE_SYSTEMS];
static int g2d_psys_used[GFX2D_MAX_PARTICLE_SYSTEMS];

/* Screen dimensions */
#define G2D_W 640
#define G2D_H 480

/* ── Integer sine approximation ───────────────────────────────────── */
/* Returns 127*sin(2*PI*a/256), parabolic approximation */
static int32_t g2d_isin(int32_t a) {
  int32_t half, qr, v;
  a &= 255;
  half = (a < 128) ? a : 256 - a;       /* 0..128, half wave */
  qr = (half < 64) ? half : 128 - half; /* 0..64, quarter wave */
  v = (qr * (128 - qr) * 127) / 4096;
  return (a < 128) ? v : -v;
}

/* ── Color component helpers ──────────────────────────────────────── */
/* Fast blend: (a*s + ia*d + 128) >> 8  ≈ (a*s + ia*d) / 255 */
static uint32_t g2d_blend(uint32_t src, uint32_t dst, uint32_t a) {
  uint32_t ia = 255u - a;
  uint32_t r =
      (((src >> 16) & 0xFFu) * a + ((dst >> 16) & 0xFFu) * ia + 128u) >> 8;
  uint32_t g =
      (((src >> 8) & 0xFFu) * a + ((dst >> 8) & 0xFFu) * ia + 128u) >> 8;
  uint32_t b = (((src) & 0xFFu) * a + ((dst) & 0xFFu) * ia + 128u) >> 8;
  return (r << 16) | (g << 8) | b;
}

/* Linear interpolation of two colors, t in [0, max] */
static uint32_t g2d_lerp(uint32_t c1, uint32_t c2, int t, int max) {
  if (max <= 0)
    return c1;
  uint32_t r = ((c1 >> 16 & 0xFFu) * (uint32_t)(max - t) +
                (c2 >> 16 & 0xFFu) * (uint32_t)t) /
               (uint32_t)max;
  uint32_t g = ((c1 >> 8 & 0xFFu) * (uint32_t)(max - t) +
                (c2 >> 8 & 0xFFu) * (uint32_t)t) /
               (uint32_t)max;
  uint32_t b =
      ((c1 & 0xFFu) * (uint32_t)(max - t) + (c2 & 0xFFu) * (uint32_t)t) /
      (uint32_t)max;
  return (r << 16) | (g << 8) | b;
}

/* Apply current blend mode: src over dst */
static uint32_t g2d_apply_blend(uint32_t src, uint32_t dst) {
  uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
  uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
  uint32_t r, g, b;
  switch (g2d_blend_mode_val) {
  case GFX2D_BLEND_ADD:
    r = sr + dr;
    if (r > 255)
      r = 255;
    g = sg + dg;
    if (g > 255)
      g = 255;
    b = sb + db;
    if (b > 255)
      b = 255;
    break;
  case GFX2D_BLEND_MULTIPLY:
    r = (sr * dr) >> 8;
    g = (sg * dg) >> 8;
    b = (sb * db) >> 8;
    break;
  case GFX2D_BLEND_SCREEN:
    r = 255 - (((255 - sr) * (255 - dr)) >> 8);
    g = 255 - (((255 - sg) * (255 - dg)) >> 8);
    b = 255 - (((255 - sb) * (255 - db)) >> 8);
    break;
  case GFX2D_BLEND_OVERLAY:
    r = dr < 128 ? (2 * sr * dr) >> 8
                 : 255 - ((2 * (255 - sr) * (255 - dr)) >> 8);
    g = dg < 128 ? (2 * sg * dg) >> 8
                 : 255 - ((2 * (255 - sg) * (255 - dg)) >> 8);
    b = db < 128 ? (2 * sb * db) >> 8
                 : 255 - ((2 * (255 - sb) * (255 - db)) >> 8);
    break;
  default: /* NORMAL */
    return src;
  }
  return (r << 16) | (g << 8) | b;
}

/* ── Clipped pixel write ──────────────────────────────────────────── */
static void g2d_put(int x, int y, uint32_t c) {
  uint32_t *fb = g2d_active_fb ? g2d_active_fb : g2d_fb;
  int w = g2d_active_w, h = g2d_active_h;
  if (g2d_clip_active) {
    if (x < g2d_clip_x || x >= g2d_clip_x + g2d_clip_w)
      return;
    if (y < g2d_clip_y || y >= g2d_clip_y + g2d_clip_h)
      return;
  }
  if (x < 0 || x >= w || y < 0 || y >= h)
    return;
  uint32_t idx = (uint32_t)y * (uint32_t)w + (uint32_t)x;
  if (g2d_blend_mode_val != GFX2D_BLEND_NORMAL) {
    c = g2d_apply_blend(c, fb[idx]);
  }
  fb[idx] = c;
}

static void g2d_put_alpha(int x, int y, uint32_t argb) {
  uint32_t a = (argb >> 24) & 0xFFu;
  if (a == 0u)
    return;
  if (a >= 255u) {
    g2d_put(x, y, argb & 0x00FFFFFFu);
    return;
  }
  if (g2d_clip_active) {
    if (x < g2d_clip_x || x >= g2d_clip_x + g2d_clip_w)
      return;
    if (y < g2d_clip_y || y >= g2d_clip_y + g2d_clip_h)
      return;
  }
  if (x < 0 || x >= G2D_W || y < 0 || y >= G2D_H)
    return;
  uint32_t dst = g2d_fb[(uint32_t)y * (uint32_t)G2D_W + (uint32_t)x];
  g2d_fb[(uint32_t)y * (uint32_t)G2D_W + (uint32_t)x] =
      g2d_blend(argb & 0x00FFFFFFu, dst, a);
}

static uint32_t g2d_get(int x, int y) {
  uint32_t *fb = g2d_active_fb ? g2d_active_fb : g2d_fb;
  int w = g2d_active_w, h = g2d_active_h;
  if (x < 0 || x >= w || y < 0 || y >= h)
    return 0u;
  return fb[(uint32_t)y * (uint32_t)w + (uint32_t)x];
}

/* ── Init ─────────────────────────────────────────────────────────── */

void gfx2d_init(void) {
  int i;
  g2d_fb = vga_get_framebuffer();
  g2d_clip_active = 0;
  g2d_clip_x = 0;
  g2d_clip_y = 0;
  g2d_clip_w = G2D_W;
  g2d_clip_h = G2D_H;
  for (i = 0; i < GFX2D_MAX_SPRITES; i++) {
    g2d_sprite_data[i] = NULL;
    g2d_sprite_w[i] = 0;
    g2d_sprite_h[i] = 0;
    g2d_sprite_used[i] = 0;
  }
  /* Initialize blend mode */
  g2d_blend_mode_val = GFX2D_BLEND_NORMAL;
  /* Initialize surface pool */
  for (i = 0; i < GFX2D_MAX_SURFACES; i++) {
    g2d_surf_data[i] = NULL;
    g2d_surf_w[i] = 0;
    g2d_surf_h[i] = 0;
    g2d_surf_used[i] = 0;
  }
  g2d_active_fb = NULL;
  g2d_active_w = G2D_W;
  g2d_active_h = G2D_H;
  /* Initialize particle systems */
  for (i = 0; i < GFX2D_MAX_PARTICLE_SYSTEMS; i++) {
    g2d_psys_used[i] = 0;
  }
  serial_printf("[gfx2d] initialized\n");
}

/* ── Screen ───────────────────────────────────────────────────────── */

static int g2d_debug_frame = 0;
void gfx2d_clear(uint32_t color) {
  if (g2d_debug_frame < 3)
    serial_printf("[gfx2d] clear frame=%d\n", g2d_debug_frame);
  vga_clear_screen(color);
}

void gfx2d_set_framebuffer(uint32_t *new_fb) { g2d_fb = new_fb; }

void gfx2d_flip(void) {
  if (g2d_debug_frame < 3)
    serial_printf("[gfx2d] flip frame=%d\n", g2d_debug_frame);
  g2d_debug_frame++;
  vga_flip();
}

int gfx2d_width(void) { return G2D_W; }
int gfx2d_height(void) { return G2D_H; }

/* ── Pixel ────────────────────────────────────────────────────────── */

void gfx2d_pixel(int x, int y, uint32_t color) { g2d_put(x, y, color); }

void gfx2d_pixel_alpha(int x, int y, uint32_t argb) {
  g2d_put_alpha(x, y, argb);
}

/* ── Lines ────────────────────────────────────────────────────────── */

void gfx2d_hline(int x, int y, int w, uint32_t color) {
  int x1, x2;
  uint32_t *row;
  int i, n;
  if (y < 0 || y >= G2D_H)
    return;
  if (g2d_clip_active && (y < g2d_clip_y || y >= g2d_clip_y + g2d_clip_h))
    return;
  x1 = x;
  x2 = x + w - 1;
  if (g2d_clip_active) {
    if (x1 < g2d_clip_x)
      x1 = g2d_clip_x;
    if (x2 >= g2d_clip_x + g2d_clip_w)
      x2 = g2d_clip_x + g2d_clip_w - 1;
  }
  if (x1 < 0)
    x1 = 0;
  if (x2 >= G2D_W)
    x2 = G2D_W - 1;
  if (x1 > x2)
    return;
  row = g2d_fb + (uint32_t)y * G2D_W + (uint32_t)x1;
  n = x2 - x1 + 1;
  for (i = 0; i < n; i++)
    row[i] = color;
}

void gfx2d_vline(int x, int y, int h, uint32_t color) {
  int y1, y2;
  uint32_t *col;
  int i;
  if (x < 0 || x >= G2D_W)
    return;
  if (g2d_clip_active && (x < g2d_clip_x || x >= g2d_clip_x + g2d_clip_w))
    return;
  y1 = y;
  y2 = y + h - 1;
  if (g2d_clip_active) {
    if (y1 < g2d_clip_y)
      y1 = g2d_clip_y;
    if (y2 >= g2d_clip_y + g2d_clip_h)
      y2 = g2d_clip_y + g2d_clip_h - 1;
  }
  if (y1 < 0)
    y1 = 0;
  if (y2 >= G2D_H)
    y2 = G2D_H - 1;
  if (y1 > y2)
    return;
  col = g2d_fb + (uint32_t)y1 * G2D_W + (uint32_t)x;
  for (i = y1; i <= y2; i++, col += G2D_W)
    *col = color;
}

void gfx2d_line(int x1, int y1, int x2, int y2, uint32_t color) {
  int dx = x2 - x1, dy = y2 - y1;
  int sx = (dx >= 0) ? 1 : -1;
  int sy = (dy >= 0) ? 1 : -1;
  if (dx < 0)
    dx = -dx;
  if (dy < 0)
    dy = -dy;
  int err = dx - dy;
  for (;;) {
    g2d_put(x1, y1, color);
    if (x1 == x2 && y1 == y2)
      break;
    int e2 = err * 2;
    if (e2 > -dy) {
      err -= dy;
      x1 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y1 += sy;
    }
  }
}

/* ── Rectangles ───────────────────────────────────────────────────── */

void gfx2d_rect(int x, int y, int w, int h, uint32_t color) {
  gfx2d_hline(x, y, w, color);
  gfx2d_hline(x, y + h - 1, w, color);
  gfx2d_vline(x, y, h, color);
  gfx2d_vline(x + w - 1, y, h, color);
}

void gfx2d_rect_fill(int x, int y, int w, int h, uint32_t color) {
  int x1, x2, y1, y2, row, n;
  uint32_t *dst;
  x1 = x;
  x2 = x + w - 1;
  y1 = y;
  y2 = y + h - 1;
  if (g2d_clip_active) {
    if (x1 < g2d_clip_x)
      x1 = g2d_clip_x;
    if (x2 >= g2d_clip_x + g2d_clip_w)
      x2 = g2d_clip_x + g2d_clip_w - 1;
    if (y1 < g2d_clip_y)
      y1 = g2d_clip_y;
    if (y2 >= g2d_clip_y + g2d_clip_h)
      y2 = g2d_clip_y + g2d_clip_h - 1;
  }
  if (x1 < 0)
    x1 = 0;
  if (x2 >= G2D_W)
    x2 = G2D_W - 1;
  if (y1 < 0)
    y1 = 0;
  if (y2 >= G2D_H)
    y2 = G2D_H - 1;
  if (x1 > x2 || y1 > y2)
    return;
  n = x2 - x1 + 1;
  dst = g2d_fb + (uint32_t)y1 * G2D_W + (uint32_t)x1;
  for (row = y1; row <= y2; row++, dst += G2D_W) {
    int i;
    for (i = 0; i < n; i++)
      dst[i] = color;
  }
}

void gfx2d_rect_round(int x, int y, int w, int h, int r, uint32_t color) {
  int i;
  if (r <= 0) {
    gfx2d_rect(x, y, w, h, color);
    return;
  }
  /* Top/bottom horizontal edges */
  gfx2d_hline(x + r, y, w - 2 * r, color);
  gfx2d_hline(x + r, y + h - 1, w - 2 * r, color);
  /* Left/right vertical edges */
  gfx2d_vline(x, y + r, h - 2 * r, color);
  gfx2d_vline(x + w - 1, y + r, h - 2 * r, color);
  /* Corners using midpoint circle quarter */
  for (i = 0; i <= r; i++) {
    int j = (int)(r * r - i * i);
    int k = 0;
    while (k * k < j)
      k++;
    /* 4 corners */
    g2d_put(x + r - i, y + r - k, color);
    g2d_put(x + w - 1 - r + i, y + r - k, color);
    g2d_put(x + r - i, y + h - 1 - r + k, color);
    g2d_put(x + w - 1 - r + i, y + h - 1 - r + k, color);
  }
}

void gfx2d_rect_round_fill(int x, int y, int w, int h, int r, uint32_t color) {
  int row;
  if (r <= 0) {
    gfx2d_rect_fill(x, y, w, h, color);
    return;
  }
  for (row = 0; row < h; row++) {
    int yy = y + row;
    int off = 0;
    if (row < r) {
      int dy = r - row;
      int dx = (int)(r * r - dy * dy);
      int k = 0;
      while (k * k < dx)
        k++;
      off = r - k;
    } else if (row >= h - r) {
      int dy = row - (h - r - 1);
      int dx = (int)(r * r - dy * dy);
      int k = 0;
      while (k * k < dx)
        k++;
      off = r - k;
    }
    gfx2d_hline(x + off, yy, w - 2 * off, color);
  }
}

/* ── Circles & Ellipses ───────────────────────────────────────────── */

void gfx2d_circle(int cx, int cy, int r, uint32_t color) {
  int x = 0, y = r, d = 3 - 2 * r;
  while (x <= y) {
    g2d_put(cx + x, cy + y, color);
    g2d_put(cx - x, cy + y, color);
    g2d_put(cx + x, cy - y, color);
    g2d_put(cx - x, cy - y, color);
    g2d_put(cx + y, cy + x, color);
    g2d_put(cx - y, cy + x, color);
    g2d_put(cx + y, cy - x, color);
    g2d_put(cx - y, cy - x, color);
    if (d < 0)
      d += 4 * x + 6;
    else {
      d += 4 * (x - y) + 10;
      y--;
    }
    x++;
  }
}

void gfx2d_circle_fill(int cx, int cy, int r, uint32_t color) {
  int x = 0, y = r, d = 3 - 2 * r;
  while (x <= y) {
    gfx2d_hline(cx - x, cy + y, 2 * x + 1, color);
    gfx2d_hline(cx - x, cy - y, 2 * x + 1, color);
    gfx2d_hline(cx - y, cy + x, 2 * y + 1, color);
    gfx2d_hline(cx - y, cy - x, 2 * y + 1, color);
    if (d < 0)
      d += 4 * x + 6;
    else {
      d += 4 * (x - y) + 10;
      y--;
    }
    x++;
  }
}

void gfx2d_ellipse(int cx, int cy, int rx, int ry, uint32_t color) {
  int x, y;
  int dx, dy, d1, d2;
  x = 0;
  y = ry;
  d1 = (ry * ry) - (rx * rx * ry) + (rx * rx / 4);
  dx = 2 * ry * ry * x;
  dy = 2 * rx * rx * y;
  while (dx < dy) {
    g2d_put(cx + x, cy + y, color);
    g2d_put(cx - x, cy + y, color);
    g2d_put(cx + x, cy - y, color);
    g2d_put(cx - x, cy - y, color);
    if (d1 < 0) {
      x++;
      dx += 2 * ry * ry;
      d1 += dx + ry * ry;
    } else {
      x++;
      y--;
      dx += 2 * ry * ry;
      dy -= 2 * rx * rx;
      d1 += dx - dy + ry * ry;
    }
  }
  d2 = (ry * ry) * ((x) * (x) + x) + (rx * rx) * ((y - 1) * (y - 1)) -
       (rx * rx * ry * ry);
  while (y >= 0) {
    g2d_put(cx + x, cy + y, color);
    g2d_put(cx - x, cy + y, color);
    g2d_put(cx + x, cy - y, color);
    g2d_put(cx - x, cy - y, color);
    if (d2 > 0) {
      y--;
      dy -= 2 * rx * rx;
      d2 += rx * rx - dy;
    } else {
      y--;
      x++;
      dx += 2 * ry * ry;
      dy -= 2 * rx * rx;
      d2 += dx - dy + rx * rx;
    }
  }
}

void gfx2d_ellipse_fill(int cx, int cy, int rx, int ry, uint32_t color) {
  int y;
  for (y = -ry; y <= ry; y++) {
    /* width at this y: x = rx * sqrt(1 - (y/ry)^2) */
    int dy = y * y * rx * rx;
    int rr = ry * ry * rx * rx;
    int xw = 0;
    while ((xw + 1) * (xw + 1) * ry * ry + dy <= rr)
      xw++;
    gfx2d_hline(cx - xw, cy + y, 2 * xw + 1, color);
  }
}

/* ── Alpha blending ───────────────────────────────────────────────── */

void gfx2d_rect_fill_alpha(int x, int y, int w, int h, uint32_t argb) {
  int row, col;
  for (row = 0; row < h; row++)
    for (col = 0; col < w; col++)
      g2d_put_alpha(x + col, y + row, argb);
}

/* ── Gradients ────────────────────────────────────────────────────── */

void gfx2d_gradient_h(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
  int x1, x2, y1, y2, row, col, n, wm;
  uint32_t *first_row;
  x1 = x;
  x2 = x + w - 1;
  y1 = y;
  y2 = y + h - 1;
  if (g2d_clip_active) {
    if (x1 < g2d_clip_x)
      x1 = g2d_clip_x;
    if (x2 >= g2d_clip_x + g2d_clip_w)
      x2 = g2d_clip_x + g2d_clip_w - 1;
    if (y1 < g2d_clip_y)
      y1 = g2d_clip_y;
    if (y2 >= g2d_clip_y + g2d_clip_h)
      y2 = g2d_clip_y + g2d_clip_h - 1;
  }
  if (x1 < 0)
    x1 = 0;
  if (x2 >= G2D_W)
    x2 = G2D_W - 1;
  if (y1 < 0)
    y1 = 0;
  if (y2 >= G2D_H)
    y2 = G2D_H - 1;
  if (x1 > x2 || y1 > y2)
    return;
  n = x2 - x1 + 1;
  wm = (w > 1) ? w - 1 : 1;
  /* Fill first row with lerped colors, then memcpy to remaining rows */
  first_row = g2d_fb + (uint32_t)y1 * G2D_W + (uint32_t)x1;
  for (col = 0; col < n; col++)
    first_row[col] = g2d_lerp(c1, c2, x1 - x + col, wm);
  for (row = y1 + 1; row <= y2; row++) {
    uint32_t *r = g2d_fb + (uint32_t)row * G2D_W + (uint32_t)x1;
    memcpy(r, first_row, (size_t)n * sizeof(uint32_t));
  }
}

void gfx2d_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
  int row;
  for (row = 0; row < h; row++) {
    uint32_t c = g2d_lerp(c1, c2, row, h - 1);
    gfx2d_hline(x, y + row, w, c);
  }
}

/* ── Drop Shadow ──────────────────────────────────────────────────── */

void gfx2d_shadow(int x, int y, int w, int h, int blur, uint32_t color) {
  int i, row, col;
  uint32_t base_a = (color >> 24) & 0xFFu;
  if (base_a == 0u)
    base_a = 180u; /* default semi-transparent */
  for (i = 0; i < blur; i++) {
    uint32_t a = base_a * (uint32_t)(blur - i) / (uint32_t)blur;
    uint32_t argb = ((a & 0xFFu) << 24) | (color & 0x00FFFFFFu);
    int ox = i + 2, oy = i + 2;
    for (row = oy; row < oy + h; row++)
      for (col = ox; col < ox + w; col++)
        g2d_put_alpha(x + col, y + row, argb);
  }
}

/* ── Dithering ────────────────────────────────────────────────────── */

void gfx2d_dither_rect(int x, int y, int w, int h, uint32_t c1, uint32_t c2,
                       int pattern) {
  int row, col;
  for (row = 0; row < h; row++) {
    for (col = 0; col < w; col++) {
      int use_c2 = 0;
      switch (pattern) {
      case GFX2D_DITHER_CHECKER:
        use_c2 = ((row + col) & 1);
        break;
      case GFX2D_DITHER_HLINES:
        use_c2 = (row & 1);
        break;
      case GFX2D_DITHER_VLINES:
        use_c2 = (col & 1);
        break;
      case GFX2D_DITHER_DIAGONAL:
        use_c2 = (((row + col) & 3) == 0);
        break;
      default:
        use_c2 = ((row + col) & 1);
        break;
      }
      g2d_put(x + col, y + row, use_c2 ? c2 : c1);
    }
  }
}

/* ── Scanlines (CRT effect) ───────────────────────────────────────── */

void gfx2d_scanlines(int x, int y, int w, int h, int alpha) {
  int row, col;
  uint32_t a = (uint32_t)(alpha & 0xFF);
  uint32_t dark = (a << 24);             /* 0xAA000000 — black overlay */
  for (row = y; row < y + h; row += 2) { /* every other row */
    for (col = x; col < x + w; col++) {
      g2d_put_alpha(col, row, dark);
    }
  }
}

/* ── Clipping ─────────────────────────────────────────────────────── */

void gfx2d_clip_set(int x, int y, int w, int h) {
  g2d_clip_active = 1;
  g2d_clip_x = x;
  g2d_clip_y = y;
  g2d_clip_w = w;
  g2d_clip_h = h;
}

void gfx2d_clip_clear(void) { g2d_clip_active = 0; }

/* ── Sprites ──────────────────────────────────────────────────────── */

int gfx2d_sprite_load(const char *path) {
  int i, fd;
  uint32_t w, h, px_count, px_bytes;
  uint32_t *data;
  for (i = 0; i < GFX2D_MAX_SPRITES; i++) {
    if (!g2d_sprite_used[i])
      break;
  }
  if (i >= GFX2D_MAX_SPRITES) {
    serial_printf("[gfx2d] sprite pool full\n");
    return -1;
  }
  fd = vfs_open(path, O_RDONLY);
  if (fd < 0) {
    serial_printf("[gfx2d] sprite_load: cannot open %s\n", path);
    return -1;
  }
  /* Header: 4 bytes width, 4 bytes height */
  if (vfs_read(fd, &w, 4) != 4 || vfs_read(fd, &h, 4) != 4) {
    vfs_close(fd);
    return -1;
  }
  if (w == 0u || h == 0u || w > 512u || h > 512u) {
    vfs_close(fd);
    return -1;
  }
  px_count = w * h;
  px_bytes = px_count * 4u;
  data = (uint32_t *)kmalloc(px_bytes);
  if (!data) {
    vfs_close(fd);
    return -1;
  }
  if ((uint32_t)vfs_read(fd, data, (uint32_t)px_bytes) != px_bytes) {
    kfree(data);
    vfs_close(fd);
    return -1;
  }
  vfs_close(fd);
  g2d_sprite_data[i] = data;
  g2d_sprite_w[i] = (int)w;
  g2d_sprite_h[i] = (int)h;
  g2d_sprite_used[i] = 1;
  serial_printf("[gfx2d] sprite %d loaded: %dx%d\n", i, (int)w, (int)h);
  return i;
}

void gfx2d_sprite_free(int handle) {
  if (handle < 0 || handle >= GFX2D_MAX_SPRITES)
    return;
  if (!g2d_sprite_used[handle])
    return;
  kfree(g2d_sprite_data[handle]);
  g2d_sprite_data[handle] = NULL;
  g2d_sprite_used[handle] = 0;
}

void gfx2d_sprite_draw(int handle, int x, int y) {
  int row, col;
  if (handle < 0 || handle >= GFX2D_MAX_SPRITES)
    return;
  if (!g2d_sprite_used[handle])
    return;
  int w = g2d_sprite_w[handle], h = g2d_sprite_h[handle];
  uint32_t *data = g2d_sprite_data[handle];
  for (row = 0; row < h; row++)
    for (col = 0; col < w; col++)
      g2d_put(x + col, y + row,
              data[(uint32_t)row * (uint32_t)w + (uint32_t)col] & 0x00FFFFFFu);
}

void gfx2d_sprite_draw_alpha(int handle, int x, int y) {
  int row, col;
  if (handle < 0 || handle >= GFX2D_MAX_SPRITES)
    return;
  if (!g2d_sprite_used[handle])
    return;
  int w = g2d_sprite_w[handle], h = g2d_sprite_h[handle];
  uint32_t *data = g2d_sprite_data[handle];
  for (row = 0; row < h; row++)
    for (col = 0; col < w; col++)
      g2d_put_alpha(x + col, y + row,
                    data[(uint32_t)row * (uint32_t)w + (uint32_t)col]);
}

void gfx2d_sprite_draw_scaled(int handle, int x, int y, int dw, int dh) {
  int row, col;
  if (handle < 0 || handle >= GFX2D_MAX_SPRITES)
    return;
  if (!g2d_sprite_used[handle])
    return;
  int sw = g2d_sprite_w[handle], sh = g2d_sprite_h[handle];
  uint32_t *data = g2d_sprite_data[handle];
  for (row = 0; row < dh; row++) {
    int sy = (row * sh) / dh;
    for (col = 0; col < dw; col++) {
      int sx = (col * sw) / dw;
      g2d_put(x + col, y + row,
              data[(uint32_t)sy * (uint32_t)sw + (uint32_t)sx] & 0x00FFFFFFu);
    }
  }
}

int gfx2d_sprite_width(int handle) {
  if (handle < 0 || handle >= GFX2D_MAX_SPRITES)
    return 0;
  return g2d_sprite_used[handle] ? g2d_sprite_w[handle] : 0;
}

int gfx2d_sprite_height(int handle) {
  if (handle < 0 || handle >= GFX2D_MAX_SPRITES)
    return 0;
  return g2d_sprite_used[handle] ? g2d_sprite_h[handle] : 0;
}

/* ── Text ─────────────────────────────────────────────────────────── */

static void g2d_draw_char(int x, int y, char c, uint32_t color, int font) {
  uint8_t idx = (uint8_t)c;
  int row, col;
  if (idx >= 128u)
    idx = 0u;
  if (font == GFX2D_FONT_SMALL) {
    /* 6x8: draw only left 6 columns */
    const uint8_t *glyph = font_8x8[idx];
    for (row = 0; row < 8; row++) {
      uint8_t bits = glyph[row];
      for (col = 0; col < 6; col++) {
        if (bits & (uint8_t)(0x80u >> (unsigned)col))
          g2d_put(x + col, y + row, color);
      }
    }
  } else if (font == GFX2D_FONT_LARGE) {
    /* 16x16: 2x scaled */
    const uint8_t *glyph = font_8x8[idx];
    for (row = 0; row < 8; row++) {
      uint8_t bits = glyph[row];
      for (col = 0; col < 8; col++) {
        if (bits & (uint8_t)(0x80u >> (unsigned)col)) {
          g2d_put(x + col * 2, y + row * 2, color);
          g2d_put(x + col * 2 + 1, y + row * 2, color);
          g2d_put(x + col * 2, y + row * 2 + 1, color);
          g2d_put(x + col * 2 + 1, y + row * 2 + 1, color);
        }
      }
    }
  } else {
    /* Normal 8x8 */
    const uint8_t *glyph = font_8x8[idx];
    for (row = 0; row < 8; row++) {
      uint8_t bits = glyph[row];
      for (col = 0; col < 8; col++) {
        if (bits & (uint8_t)(0x80u >> (unsigned)col))
          g2d_put(x + col, y + row, color);
      }
    }
  }
}

void gfx2d_text(int x, int y, const char *str, uint32_t color, int font) {
  int cw = (font == GFX2D_FONT_SMALL) ? 6 : (font == GFX2D_FONT_LARGE) ? 16 : 8;
  int cx = x;
  while (*str) {
    g2d_draw_char(cx, y, *str, color, font);
    cx += cw;
    str++;
  }
}

void gfx2d_text_shadow(int x, int y, const char *str, uint32_t color,
                       uint32_t shadow_color, int font) {
  gfx2d_text(x + 1, y + 1, str, shadow_color, font);
  gfx2d_text(x, y, str, color, font);
}

void gfx2d_text_outline(int x, int y, const char *str, uint32_t color,
                        uint32_t outline_color, int font) {
  gfx2d_text(x - 1, y, str, outline_color, font);
  gfx2d_text(x + 1, y, str, outline_color, font);
  gfx2d_text(x, y - 1, str, outline_color, font);
  gfx2d_text(x, y + 1, str, outline_color, font);
  gfx2d_text(x, y, str, color, font);
}

int gfx2d_text_width(const char *str, int font) {
  int cw = (font == GFX2D_FONT_SMALL) ? 6 : (font == GFX2D_FONT_LARGE) ? 16 : 8;
  int n = 0;
  while (*str) {
    n++;
    str++;
  }
  return n * cw;
}

int gfx2d_text_height(int font) { return (font == GFX2D_FONT_LARGE) ? 16 : 8; }

/* ── Retro Atmosphere Effects ─────────────────────────────────────── */

void gfx2d_vignette(int strength) {
  int x, y;
  for (y = 0; y < G2D_H; y++) {
    for (x = 0; x < G2D_W; x++) {
      int dx = x - G2D_W / 2, dy = y - G2D_H / 2;
      int dist2 = dx * dx + dy * dy;
      int max2 = (G2D_W / 2) * (G2D_W / 2) + (G2D_H / 2) * (G2D_H / 2);
      int dark = (dist2 * strength * 255) / (max2 * 100);
      if (dark > 255)
        dark = 255;
      if (dark > 0) {
        uint32_t d = (uint32_t)dark;
        uint32_t argb = (d << 24); /* black with alpha */
        g2d_put_alpha(x, y, argb);
      }
    }
  }
}

void gfx2d_pixelate(int x, int y, int w, int h, int block_size) {
  int bx, by;
  if (block_size < 2)
    return;
  for (by = y; by < y + h; by += block_size) {
    for (bx = x; bx < x + w; bx += block_size) {
      uint32_t c = g2d_get(bx, by);
      int r, c2;
      for (r = 0; r < block_size && by + r < y + h; r++)
        for (c2 = 0; c2 < block_size && bx + c2 < x + w; c2++)
          g2d_put(bx + c2, by + r, c);
    }
  }
}

void gfx2d_invert(int x, int y, int w, int h) {
  int row, col;
  for (row = 0; row < h; row++)
    for (col = 0; col < w; col++) {
      uint32_t c = g2d_get(x + col, y + row);
      g2d_put(x + col, y + row, (~c) & 0x00FFFFFFu);
    }
}

void gfx2d_tint(int x, int y, int w, int h, uint32_t color, int alpha) {
  uint32_t argb = ((uint32_t)(alpha & 0xFF) << 24) | (color & 0x00FFFFFFu);
  gfx2d_rect_fill_alpha(x, y, w, h, argb);
}

/* ── Win95-Style UI Helpers ───────────────────────────────────────── */

void gfx2d_bevel(int x, int y, int w, int h, int raised) {
  uint32_t light = raised ? 0x00FFFFFFu : 0x00404040u;
  uint32_t dark = raised ? 0x00404040u : 0x00FFFFFFu;
  /* Top and left edges: highlight */
  gfx2d_hline(x, y, w, light);
  gfx2d_vline(x, y, h, light);
  /* Bottom and right edges: shadow */
  gfx2d_hline(x, y + h - 1, w, dark);
  gfx2d_vline(x + w - 1, y, h, dark);
}

void gfx2d_panel(int x, int y, int w, int h) {
  /* Sunken panel: dark outside, light inside */
  gfx2d_hline(x, y, w, 0x00909090u);
  gfx2d_vline(x, y, h, 0x00909090u);
  gfx2d_hline(x, y + h - 1, w, 0x00F0F0F0u);
  gfx2d_vline(x + w - 1, y, h, 0x00F0F0F0u);
  gfx2d_hline(x + 1, y + 1, w - 2, 0x00606060u);
  gfx2d_vline(x + 1, y + 1, h - 2, 0x00606060u);
}

void gfx2d_titlebar(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
  gfx2d_gradient_h(x, y, w, h, c1, c2);
}

/* ── Demo Scene Effects ───────────────────────────────────────────── */

void gfx2d_copper_bars(int y, int count, int spacing, int *colors) {
  int i, row;
  for (i = 0; i < count; i++) {
    uint32_t c = (uint32_t)colors[i];
    int bar_y = y + i * spacing;
    for (row = 0; row < spacing / 2; row++) {
      /* fade in/out */
      int t = (row < spacing / 4) ? row : (spacing / 2 - row);
      int alpha = (t * 255) / (spacing / 4);
      if (alpha > 255)
        alpha = 255;
      uint32_t rc = g2d_blend(c, 0u, (uint32_t)alpha);
      gfx2d_hline(0, bar_y + row, G2D_W, rc);
    }
  }
}

void gfx2d_plasma(int x, int y, int w, int h, int tick) {
  /* Ultra-optimized plasma: render at 1/4 resolution, scale up 4x */
  uint32_t *fb = g2d_active_fb ? g2d_active_fb : g2d_fb;
  int fb_w = g2d_active_w;

  /* Pre-compute color lookup table (256 colors) once */
  static uint32_t color_lut[256];
  static int lut_init = 0;
  if (!lut_init) {
    for (int i = 0; i < 256; i++) {
      int32_t r = g2d_isin((int32_t)(((uint32_t)i + 85u) & 255u)) + 127;
      int32_t g = g2d_isin((int32_t)(((uint32_t)i + 170u) & 255u)) + 127;
      int32_t b = g2d_isin((int32_t)((uint32_t)i & 255u)) + 127;
      if (r > 255)
        r = 255;
      if (r < 0)
        r = 0;
      if (g > 255)
        g = 255;
      if (g < 0)
        g = 0;
      if (b > 255)
        b = 255;
      if (b < 0)
        b = 0;
      color_lut[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    lut_init = 1;
  }

  /* Render at 1/4 resolution and scale up 4x */
  int scale = 4;
  int sw = w / scale, sh = h / scale;

  for (int srow = 0; srow < sh; srow++) {
    int32_t v2 = g2d_isin((srow * 8 + tick / 2) & 255);

    for (int scol = 0; scol < sw; scol++) {
      int32_t v1 = g2d_isin((scol * 8 + tick) & 255);
      int32_t v3 = g2d_isin(((scol + srow + tick) * 8) & 255);
      int32_t v = v1 + v2 + v3;
      v = (v + 381) * 255 / 762;
      uint32_t color = color_lut[v];

      /* Write 4x4 block */
      for (int dy = 0; dy < scale; dy++) {
        uint32_t *dst = fb +
                        (uint32_t)(y + srow * scale + dy) * (uint32_t)fb_w +
                        (uint32_t)(x + scol * scale);
        for (int dx = 0; dx < scale; dx++) {
          dst[dx] = color;
        }
      }
    }
  }
}

void gfx2d_checkerboard(int x, int y, int w, int h, int size, uint32_t c1,
                        uint32_t c2) {
  int row, col;
  for (row = 0; row < h; row++) {
    for (col = 0; col < w; col++) {
      int bx = col / size, by = row / size;
      g2d_put(x + col, y + row, ((bx + by) & 1) ? c2 : c1);
    }
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  NEW: Blend Modes
 * ══════════════════════════════════════════════════════════════════════ */

void gfx2d_blend_mode(int mode) { g2d_blend_mode_val = mode; }

/* ══════════════════════════════════════════════════════════════════════
 *  NEW: Offscreen Surfaces
 * ══════════════════════════════════════════════════════════════════════ */

int gfx2d_surface_alloc(int w, int h) {
  int i;
  for (i = 0; i < GFX2D_MAX_SURFACES; i++) {
    if (!g2d_surf_used[i]) {
      g2d_surf_data[i] = (uint32_t *)kmalloc((uint32_t)(w * h) * 4);
      if (!g2d_surf_data[i])
        return -1;
      g2d_surf_w[i] = w;
      g2d_surf_h[i] = h;
      g2d_surf_used[i] = 1;
      /* Zero the surface */
      int n = w * h;
      for (int j = 0; j < n; j++)
        g2d_surf_data[i][j] = 0;
      return i;
    }
  }
  return -1;
}

void gfx2d_surface_free(int handle) {
  if (handle < 0 || handle >= GFX2D_MAX_SURFACES)
    return;
  if (g2d_surf_data[handle])
    kfree(g2d_surf_data[handle]);
  g2d_surf_data[handle] = NULL;
  g2d_surf_used[handle] = 0;
}

void gfx2d_surface_fill(int handle, uint32_t color) {
  if (handle < 0 || handle >= GFX2D_MAX_SURFACES || !g2d_surf_used[handle])
    return;
  int n = g2d_surf_w[handle] * g2d_surf_h[handle];
  for (int i = 0; i < n; i++)
    g2d_surf_data[handle][i] = color;
}

void gfx2d_surface_set_active(int handle) {
  if (handle < 0 || handle >= GFX2D_MAX_SURFACES || !g2d_surf_used[handle])
    return;
  g2d_active_fb = g2d_surf_data[handle];
  g2d_active_w = g2d_surf_w[handle];
  g2d_active_h = g2d_surf_h[handle];
}

void gfx2d_surface_unset_active(void) {
  g2d_active_fb = NULL;
  g2d_active_w = G2D_W;
  g2d_active_h = G2D_H;
}

void gfx2d_surface_blit(int handle, int x, int y) {
  if (handle < 0 || handle >= GFX2D_MAX_SURFACES || !g2d_surf_used[handle])
    return;
  int sw = g2d_surf_w[handle], sh = g2d_surf_h[handle];
  uint32_t *src = g2d_surf_data[handle];
  for (int sy = 0; sy < sh; sy++) {
    for (int sx = 0; sx < sw; sx++) {
      g2d_put(x + sx, y + sy, src[(uint32_t)sy * (uint32_t)sw + (uint32_t)sx]);
    }
  }
}

void gfx2d_surface_blit_alpha(int handle, int x, int y, int alpha) {
  if (handle < 0 || handle >= GFX2D_MAX_SURFACES || !g2d_surf_used[handle])
    return;
  if (alpha <= 0)
    return;
  if (alpha > 255)
    alpha = 255;
  int sw = g2d_surf_w[handle], sh = g2d_surf_h[handle];
  uint32_t *src = g2d_surf_data[handle];
  uint32_t a = (uint32_t)alpha;
  for (int sy = 0; sy < sh; sy++) {
    for (int sx = 0; sx < sw; sx++) {
      int dx = x + sx, dy = y + sy;
      if (dx < 0 || dx >= G2D_W || dy < 0 || dy >= G2D_H)
        continue;
      uint32_t s = src[(uint32_t)sy * (uint32_t)sw + (uint32_t)sx];
      uint32_t d = g2d_fb[(uint32_t)dy * (uint32_t)G2D_W + (uint32_t)dx];
      g2d_fb[(uint32_t)dy * (uint32_t)G2D_W + (uint32_t)dx] =
          g2d_blend(s, d, a);
    }
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  NEW: Tweening / Easing Functions
 * ══════════════════════════════════════════════════════════════════════ */

int gfx2d_tween_linear(int t, int start, int end, int dur) {
  if (dur <= 0)
    return end;
  if (t <= 0)
    return start;
  if (t >= dur)
    return end;
  return start + (end - start) * t / dur;
}

/* Smoothstep: 3t²-2t³ approximation using integer math */
int gfx2d_tween_ease_in_out(int t, int start, int end, int dur) {
  if (dur <= 0)
    return end;
  if (t <= 0)
    return start;
  if (t >= dur)
    return end;
  /* t normalized to 0..1024 */
  int tn = t * 1024 / dur;
  /* smoothstep: 3*tn^2 - 2*tn^3 in 1024-space */
  int s = (3 * tn * tn / 1024) - (2 * tn * tn / 1024 * tn / 1024);
  return start + (end - start) * s / 1024;
}

/* Bounce out */
int gfx2d_tween_bounce(int t, int start, int end, int dur) {
  if (dur <= 0)
    return end;
  if (t <= 0)
    return start;
  if (t >= dur)
    return end;
  int range = end - start;
  /* Normalize t to 0..1024 */
  int tn = t * 1024 / dur;
  int b;
  if (tn < 364) {
    b = (7564 * tn * tn) >> 20;
  } else if (tn < 728) {
    int n = tn - 546;
    b = (7564 * n * n) >> 20;
    b += 768;
  } else if (tn < 910) {
    int n = tn - 819;
    b = (7564 * n * n) >> 20;
    b += 960;
  } else {
    int n = tn - 966;
    b = (7564 * n * n) >> 20;
    b += 992;
  }
  if (b > 1024)
    b = 1024;
  return start + range * b / 1024;
}

/* Elastic out approximation using integer sine */
int gfx2d_tween_elastic(int t, int start, int end, int dur) {
  if (dur <= 0)
    return end;
  if (t <= 0)
    return start;
  if (t >= dur)
    return end;
  int range = end - start;
  int tn = t * 256 / dur; /* 0..256 */
  /* amplitude decay: (256-tn)/256 */
  /* frequency: ~3 oscillations = 3*256 steps */
  int wave = g2d_isin((tn * 3) & 255); /* -127..127 */
  int decay = (256 - tn);
  int elastic = range - (range * wave * decay) / (127 * 256);
  return start + elastic;
}

/* ══════════════════════════════════════════════════════════════════════
 *  NEW: Particle System
 * ══════════════════════════════════════════════════════════════════════ */

int gfx2d_particles_create(void) {
  for (int i = 0; i < GFX2D_MAX_PARTICLE_SYSTEMS; i++) {
    if (!g2d_psys_used[i]) {
      /* Zero all particles */
      for (int j = 0; j < GFX2D_MAX_PARTICLES_PER_SYS; j++) {
        g2d_psys[i].particles[j].life = 0;
      }
      g2d_psys_used[i] = 1;
      return i;
    }
  }
  return -1;
}

void gfx2d_particles_free(int handle) {
  if (handle < 0 || handle >= GFX2D_MAX_PARTICLE_SYSTEMS)
    return;
  g2d_psys_used[handle] = 0;
}

void gfx2d_particle_emit(int handle, int x, int y, int vx, int vy,
                         uint32_t color, int life) {
  if (handle < 0 || handle >= GFX2D_MAX_PARTICLE_SYSTEMS)
    return;
  if (!g2d_psys_used[handle])
    return;
  g2d_psys_t *ps = &g2d_psys[handle];
  /* Find dead slot */
  for (int i = 0; i < GFX2D_MAX_PARTICLES_PER_SYS; i++) {
    if (ps->particles[i].life <= 0) {
      ps->particles[i].x = x << 8;
      ps->particles[i].y = y << 8;
      ps->particles[i].vx = vx;
      ps->particles[i].vy = vy;
      ps->particles[i].color = color;
      ps->particles[i].life = life;
      ps->particles[i].max_life = life;
      return;
    }
  }
}

void gfx2d_particles_update(int handle, int gravity) {
  if (handle < 0 || handle >= GFX2D_MAX_PARTICLE_SYSTEMS)
    return;
  if (!g2d_psys_used[handle])
    return;
  g2d_psys_t *ps = &g2d_psys[handle];
  for (int i = 0; i < GFX2D_MAX_PARTICLES_PER_SYS; i++) {
    g2d_particle_t *p = &ps->particles[i];
    if (p->life <= 0)
      continue;
    p->vy += gravity;
    p->x += p->vx;
    p->y += p->vy;
    p->life--;
  }
}

void gfx2d_particles_draw(int handle) {
  if (handle < 0 || handle >= GFX2D_MAX_PARTICLE_SYSTEMS)
    return;
  if (!g2d_psys_used[handle])
    return;
  g2d_psys_t *ps = &g2d_psys[handle];
  for (int i = 0; i < GFX2D_MAX_PARTICLES_PER_SYS; i++) {
    g2d_particle_t *p = &ps->particles[i];
    if (p->life <= 0)
      continue;
    int sx = p->x >> 8, sy = p->y >> 8;
    /* Fade alpha based on remaining life */
    uint32_t alpha = (uint32_t)p->life * 255u /
                     (uint32_t)(p->max_life > 0 ? p->max_life : 1);
    uint32_t argb = (alpha << 24) | (p->color & 0xFFFFFF);
    g2d_put_alpha(sx, sy, argb);
    g2d_put_alpha(sx + 1, sy, argb); /* 2px wide for visibility */
    g2d_put_alpha(sx, sy + 1, argb);
  }
}

int gfx2d_particles_alive(int handle) {
  if (handle < 0 || handle >= GFX2D_MAX_PARTICLE_SYSTEMS)
    return 0;
  if (!g2d_psys_used[handle])
    return 0;
  g2d_psys_t *ps = &g2d_psys[handle];
  int count = 0;
  for (int i = 0; i < GFX2D_MAX_PARTICLES_PER_SYS; i++) {
    if (ps->particles[i].life > 0)
      count++;
  }
  return count;
}

/* ══════════════════════════════════════════════════════════════════════
 *  NEW: Advanced Drawing Tools
 * ══════════════════════════════════════════════════════════════════════ */

/* Quadratic Bezier: iterative parametric evaluation (no recursion) */
void gfx2d_bezier(int x0, int y0, int x1, int y1, int x2, int y2,
                  uint32_t color) {
  /* Use parametric form: B(t) = (1-t)^2*P0 + 2(1-t)t*P1 + t^2*P2 */
  /* Evaluate at fixed number of steps based on curve length */
  int dx = x2 - x0, dy = y2 - y0;
  int len = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
  int steps = len / 2; /* Approximate steps needed */
  if (steps < 10)
    steps = 10;
  if (steps > 200)
    steps = 200; /* Limit for performance */

  int prev_x = x0, prev_y = y0;
  for (int i = 1; i <= steps; i++) {
    /* t goes from 0 to 256 (fixed point, 256 = 1.0) */
    int t = (i * 256) / steps;
    int t2 = t * t / 256;
    int mt = 256 - t;
    int mt2 = mt * mt / 256;
    int mtt2 = 2 * mt * t / 256;

    int bx = (mt2 * x0 + mtt2 * x1 + t2 * x2) / 256;
    int by = (mt2 * y0 + mtt2 * y1 + t2 * y2) / 256;

    /* Draw line segment from previous point */
    gfx2d_line(prev_x, prev_y, bx, by, color);
    prev_x = bx;
    prev_y = by;
  }
}

/* Filled triangle — scanline rasterization */
void gfx2d_tri_fill(int x0, int y0, int x1, int y1, int x2, int y2,
                    uint32_t color) {
  /* Sort vertices by y */
  int tx, ty;
  if (y1 < y0) {
    tx = x0;
    ty = y0;
    x0 = x1;
    y0 = y1;
    x1 = tx;
    y1 = ty;
  }
  if (y2 < y0) {
    tx = x0;
    ty = y0;
    x0 = x2;
    y0 = y2;
    x2 = tx;
    y2 = ty;
  }
  if (y2 < y1) {
    tx = x1;
    ty = y1;
    x1 = x2;
    y1 = y2;
    x2 = tx;
    y2 = ty;
  }

  int total_h = y2 - y0;
  if (total_h == 0)
    return;

  for (int y = y0; y <= y2; y++) {
    int second_half = (y > y1 || y1 == y0);
    int seg_h = second_half ? (y2 - y1) : (y1 - y0);
    if (seg_h == 0)
      seg_h = 1;
    int alpha = (y - y0) * 256 / total_h;
    int beta =
        second_half ? ((y - y1) * 256 / seg_h) : ((y - y0) * 256 / seg_h);
    int ax = x0 + (x2 - x0) * alpha / 256;
    int bx = second_half ? (x1 + (x2 - x1) * beta / 256)
                         : (x0 + (x1 - x0) * beta / 256);
    if (ax > bx) {
      int t = ax;
      ax = bx;
      bx = t;
    }
    gfx2d_hline(ax, y, bx - ax + 1, color);
  }
}

/* Wu's anti-aliased line */
void gfx2d_line_aa(int x0, int y0, int x1, int y1, uint32_t color) {
  int steep = (y1 - y0 < 0 ? -(y1 - y0) : (y1 - y0)) >
              (x1 - x0 < 0 ? -(x1 - x0) : (x1 - x0));
  if (steep) {
    int t;
    t = x0;
    x0 = y0;
    y0 = t;
    t = x1;
    x1 = y1;
    y1 = t;
  }
  if (x0 > x1) {
    int t;
    t = x0;
    x0 = x1;
    x1 = t;
    t = y0;
    y0 = y1;
    y1 = t;
  }

  int dx = x1 - x0, dy = y1 - y0;
  int gradient = (dx == 0) ? 256 : dy * 256 / dx;

  /* integer y intersection, scaled by 256 */
  int intery = y0 * 256 + gradient;

  for (int x = x0; x <= x1; x++) {
    int iy = intery >> 8;
    int frac = intery & 0xFF; /* fractional part */
    uint32_t a1 = (uint32_t)(255 - frac);
    uint32_t a2 = (uint32_t)frac;
    uint32_t argb1 = (a1 << 24) | (color & 0xFFFFFF);
    uint32_t argb2 = (a2 << 24) | (color & 0xFFFFFF);
    if (steep) {
      g2d_put_alpha(iy, x, argb1);
      g2d_put_alpha(iy + 1, x, argb2);
    } else {
      g2d_put_alpha(x, iy, argb1);
      g2d_put_alpha(x, iy + 1, argb2);
    }
    intery += gradient;
  }
}

/* Flood fill — iterative BFS using a small stack */
#define FLOOD_STACK_SIZE 2048
void gfx2d_flood_fill(int x, int y, uint32_t color) {
  uint32_t target = g2d_get(x, y);
  if (target == color)
    return;

  /* Stack of (x, y) pairs encoded as x*1024+y for speed */
  static int flood_stack[FLOOD_STACK_SIZE];
  int sp = 0;
  flood_stack[sp++] = x * 1024 + y;

  while (sp > 0) {
    int v = flood_stack[--sp];
    int cx = v / 1024, cy = v % 1024;
    if (cx < 0 || cx >= G2D_W || cy < 0 || cy >= G2D_H)
      continue;
    if (g2d_get(cx, cy) != target)
      continue;
    g2d_put(cx, cy, color);
    if (sp < FLOOD_STACK_SIZE - 4) {
      flood_stack[sp++] = (cx + 1) * 1024 + cy;
      flood_stack[sp++] = (cx - 1) * 1024 + cy;
      flood_stack[sp++] = cx * 1024 + (cy + 1);
      flood_stack[sp++] = cx * 1024 + (cy - 1);
    }
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Fullscreen Mode (pauses desktop rendering)
 * ══════════════════════════════════════════════════════════════════════ */

static int g2d_fullscreen_mode = 0;

void gfx2d_fullscreen_enter(void) {
  g2d_fullscreen_mode = 1;
  /* Refresh the framebuffer pointer in case it changed */
  g2d_fb = vga_get_framebuffer();
  serial_printf("[gfx2d] fullscreen mode entered (fb=%x)\n", (uint32_t)g2d_fb);
}

void gfx2d_fullscreen_exit(void) {
  g2d_fullscreen_mode = 0;
  serial_printf("[gfx2d] fullscreen mode exited\n");
}

int gfx2d_fullscreen_active(void) { return g2d_fullscreen_mode; }
