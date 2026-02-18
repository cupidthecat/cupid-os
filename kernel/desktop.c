/**
 * desktop.c - Desktop shell for cupid-os
 *
 * Implements the desktop background, taskbar, icons, and the
 * main event loop that drives the graphical environment.
 */

#include "desktop.h"
#include "simd.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/rtc.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "../drivers/vga.h"
#include "calendar.h"
#include "bmp.h"
#include "gfx2d.h"
#include "graphics.h"
#include "gfx2d_icons.h"
#include "gui.h"
#include "gui_widgets.h"
#include "kernel.h"
#include "memory.h"
#include "cupidc.h"
#include "terminal_app.h"
#include "process.h"
#include "shell.h"
#include "string.h"
#include "ui.h"
#include "vfs.h"

extern char cc_notepad_open_path[256];
extern char cc_notepad_save_path[256];

static desktop_icon_t icons[MAX_DESKTOP_ICONS];
static int icon_count = 0;

static uint32_t bg_lut[VGA_GFX_HEIGHT];
static uint32_t bg_lut_top = 0xFFFFFFFFu; /* invalid sentinel */

enum {
  DESKTOP_BG_ANIM = 0,
  DESKTOP_BG_SOLID = 1,
  DESKTOP_BG_BMP = 2,
  DESKTOP_BG_GRADIENT = 3,
  DESKTOP_BG_TILED = 4
};

static uint8_t desktop_bg_mode = DESKTOP_BG_ANIM;
static uint32_t desktop_bg_solid = COLOR_DESKTOP_BG;
static uint32_t desktop_bg_grad_top = COLOR_DESKTOP_BG;
static uint32_t desktop_bg_grad_bottom = COLOR_TASKBAR;
static uint8_t desktop_anim_theme = DESKTOP_ANIM_THEME_KITTY;
static uint32_t desktop_tile_fg = 0x00C0C0C0u;
static uint32_t desktop_tile_bg = 0x00008080u;
static uint8_t desktop_tile_pattern = DESKTOP_TILE_PATTERN_CHECKER;
static uint8_t desktop_tile_use_bmp = 0;
static char desktop_tile_bmp_path[VFS_MAX_PATH];
static uint32_t *desktop_tile_bmp_data = NULL;
static int desktop_tile_bmp_w = 0;
static int desktop_tile_bmp_h = 0;
static char desktop_bg_bmp_path[VFS_MAX_PATH];
static uint32_t *desktop_bg_bmp_scaled = NULL; /* VGA_GFX_WIDTH * TASKBAR_Y */
static const char *DESKTOP_BG_CONFIG_PATH = "/home/.desktop_bg.conf";

static uint32_t *workspace_base_cache = NULL; /* bg + icon glyphs (no hover FX) */
static bool workspace_base_cache_valid = false;
static uint8_t workspace_cache_bg_mode = 0xFFu;
static uint32_t workspace_cache_bg_solid = 0u;
static uint32_t workspace_cache_bg_grad_top = 0u;
static uint32_t workspace_cache_bg_grad_bottom = 0u;
static uint32_t workspace_cache_tile_fg = 0u;
static uint32_t workspace_cache_tile_bg = 0u;
static uint8_t workspace_cache_tile_pattern = 0u;
static uint8_t workspace_cache_tile_use_bmp = 0u;
static bool workspace_cache_has_bmp = false;
static int desktop_icon_stamp = 1;
static int workspace_cache_icon_stamp = 0;
static bool workspace_base_drawn_with_icons = false;

static uint32_t *taskbar_base_cache = NULL; /* taskbar gradient + separator + brand */
static bool taskbar_base_cache_valid = false;

static char clock_time_str[16];
static char clock_date_str[16];
static uint8_t clock_last_minute = 255; /* Force initial update */
static uint32_t clock_last_poll_ms = 0;
static int16_t clock_hitbox_x = 0;
static uint16_t clock_hitbox_width = 0;
static uint32_t taskbar_last_draw_ms = 0;
static int taskbar_last_window_count = -1;
static int taskbar_last_focus_id = -1;
static uint32_t desktop_anim_tick = 0; /* Animation frame counter */
calendar_state_t cal_state;
static bool cal_prev_visible = false; /* Track calendar visibility changes */

static void desktop_draw_calendar(void);
static void desktop_bg_save_config(void);
static void desktop_bg_load_config(void);
static void desktop_draw_icon_hover_fx(void);
static void desktop_redraw_workspace_region(int16_t x, int16_t y,
                                            uint16_t w, uint16_t h);
static void terminal_launch_cc(void);

static void desktop_invalidate_workspace_cache(void) {
  workspace_base_cache_valid = false;
}

static void desktop_invalidate_taskbar_cache(void) { taskbar_base_cache_valid = false; }

static void desktop_icons_changed(void) {
  desktop_icon_stamp++;
  if (desktop_icon_stamp <= 0)
    desktop_icon_stamp = 1;
  desktop_invalidate_workspace_cache();
}

static void desktop_mark_dirty_rect(int x, int y, int w, int h) {
  vga_mark_dirty(x, y, w, h);
}

static void desktop_fill_row32(uint32_t *dst, int n, uint32_t color) {
  simd_memset32(dst, color, (uint32_t)n);
}

static void desktop_launch_icon_handle(int gfx_icon) {
  if (gfx_icon < 0)
    return;

  gfx2d_icon_select(gfx_icon);
  desktop_icons_changed();
  void (*launch_fn)(void) = gfx2d_icon_get_launch(gfx_icon);
  if (launch_fn) {
    serial_printf("[desktop] icon launch_fn handle=%d\n", gfx_icon);
    launch_fn();
    return;
  }

  {
    const char *prog = gfx2d_icon_get_path(gfx_icon);
    if (prog && prog[0]) {
      serial_printf("[desktop] icon cupidc_jit path=%s handle=%d\n", prog,
                    gfx_icon);
      cupidc_jit(prog);
    }
  }
}

static void desktop_show_icon_info(int gfx_icon) {
  char msg[256];
  const char *label = gfx2d_icon_get_label(gfx_icon);
  const char *path = gfx2d_icon_get_path(gfx_icon);
  const char *desc = gfx2d_icon_get_desc(gfx_icon);
  int p = 0;

  const char *h1 = "Icon: ";
  while (*h1 && p < 255)
    msg[p++] = *h1++;
  while (label && *label && p < 255)
    msg[p++] = *label++;

  if (path && path[0] && p < 254) {
    msg[p++] = '\n';
    const char *h2 = "Path: ";
    while (*h2 && p < 255)
      msg[p++] = *h2++;
    while (*path && p < 255)
      msg[p++] = *path++;
  }

  if (desc && desc[0] && p < 254) {
    msg[p++] = '\n';
    const char *h3 = "Desc: ";
    while (*h3 && p < 255)
      msg[p++] = *h3++;
    while (*desc && p < 255)
      msg[p++] = *desc++;
  }

  msg[p] = '\0';
  gfx2d_message_dialog(msg);
}

static bool desktop_handle_global_shortcuts(const key_event_t *event,
                                            bool *force_full_repaint) {
  if (!event || !event->pressed)
    return false;

  bool ctrl = keyboard_get_ctrl();
  bool alt = keyboard_get_alt();
  char ch = event->character;

  if (ctrl && alt && (event->scancode == 0x14 || ch == 't' || ch == 'T' ||
                      ch == 20)) { /* Ctrl+T = 20 */
    terminal_launch_cc();
    if (force_full_repaint)
      *force_full_repaint = true;
    return true;
  }

  if (ctrl && alt && (event->scancode == 0x31 || ch == 'n' || ch == 'N' ||
                      ch == 14)) { /* Ctrl+N = 14 */
    desktop_notepad_launch();
    if (force_full_repaint)
      *force_full_repaint = true;
    return true;
  }

  if (event->scancode == 0x3F) { /* F5 */
    gfx2d_icons_scan_bin();
    desktop_icons_changed();
    if (force_full_repaint)
      *force_full_repaint = true;
    return true;
  }

  return false;
}

static void desktop_draw_hover_tooltip(void) {
  if (mouse.y >= TASKBAR_Y)
    return;
  if (mouse.buttons & (MOUSE_LEFT | MOUSE_RIGHT))
    return;
  if (gui_hit_test_window(mouse.x, mouse.y) >= 0)
    return;

  int gfx_icon = gfx2d_icon_at_pos(mouse.x, mouse.y);
  if (gfx_icon < 0)
    return;

  {
    const char *tip = gfx2d_icon_get_desc(gfx_icon);
    if (!tip || !tip[0])
      tip = gfx2d_icon_get_label(gfx_icon);
    {
      int ix = gfx2d_icon_get_x(gfx_icon);
      int iy = gfx2d_icon_get_y(gfx_icon);
      gfx2d_tooltip(ix + GFX2D_ICON_SIZE + 8, iy + 4, tip);
    }
  }
}

/* Wrappers for icon drawers (custom_draw takes 2 args, draw funcs take 3) */
static void draw_terminal_icon(int x, int y) {
  gfx2d_draw_icon_terminal(x, y, 0x404040);
}
static void draw_notepad_icon(int x, int y) {
  gfx2d_draw_icon_notepad(x, y, 0xFFFF80);
}

static void terminal_launch_cc(void) {
  terminal_launch();
}

static void notepad_cc_process_entry(void) {
  cupidc_jit("/bin/notepad.cc");
  process_exit();
}

static void fm_cc_process_entry(void) {
  cupidc_jit("/bin/fm.cc");
  process_exit();
}

void desktop_notepad_launch(void) {
  enum { NOTEPAD_JIT_STACK = 262144 };
  shell_set_output_mode(SHELL_OUTPUT_GUI);
  (void)process_create(notepad_cc_process_entry, "notepad", NOTEPAD_JIT_STACK);
}

static void desktop_fm_launch(void) {
  enum { FM_JIT_STACK = 262144 };
  shell_set_output_mode(SHELL_OUTPUT_GUI);
  serial_printf("[desktop] launching fm via process_create\n");
  (void)process_create(fm_cc_process_entry, "fm", FM_JIT_STACK);
}

void desktop_notepad_launch_with_file(const char *path, const char *save_path) {
  int i = 0;
  while (path && path[i] && i < 255) {
    cc_notepad_open_path[i] = path[i];
    i++;
  }
  cc_notepad_open_path[i] = '\0';

  i = 0;
  while (save_path && save_path[i] && i < 255) {
    cc_notepad_save_path[i] = save_path[i];
    i++;
  }
  cc_notepad_save_path[i] = '\0';

  desktop_notepad_launch();
}

void desktop_init(void) {
  icon_count = 0;
  memset(icons, 0, sizeof(icons));

  /* Initialize calendar popup state */
  memset(&cal_state, 0, sizeof(cal_state));
  cal_state.visible = false;
  clock_last_minute = 255; /* Force initial clock update */

  /* Initialize desktop icon system and scan /bin for icon directives */
  gfx2d_icons_init();

  /* Register built-in kernel icons (Terminal, Notepad) */
  {
    int h;

    h = gfx2d_icon_register("Terminal", "__kernel_terminal", 10, 10);
    if (h >= 0) {
      gfx2d_icon_set_desc(h, "CupidOS Terminal");
      gfx2d_icon_set_color(h, 0x404040);
      gfx2d_icon_set_custom_drawer(h, draw_terminal_icon);
      gfx2d_icon_set_launch(h, terminal_launch_cc);
    }

    h = gfx2d_icon_register("Notepad", "__kernel_notepad", 10, 70);
    if (h >= 0) {
      gfx2d_icon_set_desc(h, "CupidOS Notepad");
      gfx2d_icon_set_color(h, 0xFFFF80);
      gfx2d_icon_set_custom_drawer(h, draw_notepad_icon);
      gfx2d_icon_set_launch(h, desktop_notepad_launch);
    }
  }

  /* Scan /bin for .cc files with //icon: directives */
  gfx2d_icons_scan_bin();

  {
    int cc_term = gfx2d_icon_find_by_path("/bin/terminal.cc");
    if (cc_term >= 0) {
      gfx2d_icon_set_color(cc_term, 0x404040);
      gfx2d_icon_set_custom_drawer(cc_term, draw_terminal_icon);
      gfx2d_icon_set_launch(cc_term, terminal_launch_cc);
    }

    /* Keep a single Notepad icon: built-in kernel icon only.
     * /bin/notepad.cc may also declare //icon directives, which would
     * create a duplicate desktop icon after scan. */
    {
      int cc_notepad = gfx2d_icon_find_by_path("/bin/notepad.cc");
      if (cc_notepad >= 0) {
        gfx2d_icon_unregister(cc_notepad);
      }
    }

    {
      int cc_fm = gfx2d_icon_find_by_path("/bin/fm.cc");
      if (cc_fm >= 0) {
        gfx2d_icon_set_launch(cc_fm, desktop_fm_launch);
      }
    }
  }

  desktop_icons_changed();

  desktop_invalidate_workspace_cache();
  desktop_invalidate_taskbar_cache();

  /* Load persisted desktop background preference (if any). */
  desktop_bg_load_config();

  KINFO("Desktop initialized");
}

void desktop_add_icon(int16_t x, int16_t y, const char *label,
                      void (*launch)(void)) {
  if (icon_count >= MAX_DESKTOP_ICONS)
    return;
  desktop_icon_t *ic = &icons[icon_count++];
  ic->x = x;
  ic->y = y;
  ic->launch = launch;
  ic->active = true;

  int i = 0;
  while (label[i] && i < 31) {
    ic->label[i] = label[i];
    i++;
  }
  ic->label[i] = '\0';
}

/* blend two RGB colors by factor t (0..max) */
static uint32_t desktop_blend_colors(uint32_t c1, uint32_t c2, int t, int max) {
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

static uint32_t desktop_anim_top_color(void) {
  uint32_t t = desktop_anim_tick & 511u;
  uint32_t base = desktop_bg_solid & 0x00FFFFFFu;
  if (base == 0)
    base = COLOR_DESKTOP_BG;
  if (t < 170u) {
    return desktop_blend_colors(base, 0xEEDDFFu, (int)t, 170);
  }
  if (t < 340u) {
    return desktop_blend_colors(0xEEDDFFu, 0xFFF0E0u, (int)(t - 170u), 170);
  }
  return desktop_blend_colors(0xFFF0E0u, base, (int)(t - 340u),
                              172);
}

static void desktop_draw_cloud8(int x, int y, uint32_t c_hi, uint32_t c_lo) {
  gfx_fill_rect((int16_t)(x + 4), (int16_t)(y + 8), 44, 12, c_lo);
  gfx_fill_rect((int16_t)(x + 12), (int16_t)(y + 2), 14, 10, c_hi);
  gfx_fill_rect((int16_t)(x + 24), (int16_t)(y + 0), 16, 12, c_hi);
  gfx_fill_rect((int16_t)(x + 38), (int16_t)(y + 4), 12, 8, c_hi);
}

static void desktop_draw_theme_kitty(void) {
  int y = TASKBAR_Y - 28;
  int x = (int)(desktop_anim_tick % (uint32_t)(VGA_GFX_WIDTH + 40)) - 20;
  int leg = (int)((desktop_anim_tick >> 3) & 1u);

  desktop_draw_cloud8(28, 26, 0xFFFFFFu, 0xE5F2FFu);
  desktop_draw_cloud8(180, 38, 0xFFFFFFu, 0xE5F2FFu);
  desktop_draw_cloud8(320, 24, 0xFFFFFFu, 0xE5F2FFu);

  gfx_fill_rect((int16_t)(x + 4), (int16_t)(y + 8), 18, 10, 0xD8C2A8u);
  gfx_fill_rect((int16_t)(x + 18), (int16_t)(y + 5), 10, 10, 0xE8D0B7u);
  gfx_fill_rect((int16_t)(x + 19), (int16_t)(y + 2), 3, 4, 0xB89270u);
  gfx_fill_rect((int16_t)(x + 24), (int16_t)(y + 2), 3, 4, 0xB89270u);
  gfx_fill_rect((int16_t)(x + 22), (int16_t)(y + 9), 2, 2, 0x402818u);
  gfx_fill_rect((int16_t)(x + 27), (int16_t)(y + 9), 2, 2, 0x402818u);
  gfx_fill_rect((int16_t)(x + 20), (int16_t)(y + 17), 3, 3, 0xA67F5Du);
  gfx_fill_rect((int16_t)(x + 25), (int16_t)(y + 17), 3, 3, 0xA67F5Du);
  if (leg) {
    gfx_fill_rect((int16_t)(x + 4), (int16_t)(y + 18), 4, 4, 0x8E6D4Fu);
    gfx_fill_rect((int16_t)(x + 14), (int16_t)(y + 18), 4, 4, 0x8E6D4Fu);
  } else {
    gfx_fill_rect((int16_t)(x + 8), (int16_t)(y + 18), 4, 4, 0x8E6D4Fu);
    gfx_fill_rect((int16_t)(x + 18), (int16_t)(y + 18), 4, 4, 0x8E6D4Fu);
  }
  gfx_fill_rect((int16_t)(x + 0), (int16_t)(y + 10), 5, 3, 0xB89270u);

  gfx_draw_text((int16_t)(x - 12), (int16_t)(y - 10), "<3", 0xFF8FC1u);
}

static void desktop_update_bg_lut(uint32_t top) {
  if (top == bg_lut_top)
    return;
  bg_lut_top = top;
  int max_row = (TASKBAR_Y > 1) ? TASKBAR_Y - 1 : 1;
  for (int row = 0; row < TASKBAR_Y; row++) {
    bg_lut[row] = desktop_blend_colors(top, COLOR_TASKBAR, row, max_row);
  }
}

static void desktop_bg_copy_path(char *dst, const char *src, int cap) {
  int i = 0;
  if (!dst || cap <= 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  while (src[i] && i < cap - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static int desktop_bg_apply_bmp(const char *path) {
  bmp_info_t info;
  uint32_t max_decode_size = (uint32_t)VGA_GFX_WIDTH * (uint32_t)TASKBAR_Y * 4u;

  if (!path || !path[0])
    return BMP_EINVAL;

  if (bmp_get_info(path, &info) != BMP_OK)
    return BMP_EFORMAT;

  if (info.width == 0 || info.height == 0 || info.data_size == 0)
    return BMP_EFORMAT;

  if (!desktop_bg_bmp_scaled) {
    desktop_bg_bmp_scaled =
        (uint32_t *)kmalloc((uint32_t)VGA_GFX_WIDTH * (uint32_t)TASKBAR_Y * 4u);
    if (!desktop_bg_bmp_scaled) {
      return BMP_ENOMEM;
    }
  }

  if (info.data_size <= max_decode_size) {
    uint32_t *decoded = (uint32_t *)kmalloc(info.data_size);
    if (!decoded)
      return BMP_ENOMEM;

    if (bmp_decode(path, decoded, info.data_size) != BMP_OK) {
      kfree(decoded);
      return BMP_EFORMAT;
    }

    for (uint32_t y = 0; y < (uint32_t)TASKBAR_Y; y++) {
      uint32_t src_y = (y * info.height) / (uint32_t)TASKBAR_Y;
      if (src_y >= info.height)
        src_y = info.height - 1u;
      uint32_t *dst_row = desktop_bg_bmp_scaled + y * (uint32_t)VGA_GFX_WIDTH;
      uint32_t *src_row = decoded + src_y * info.width;
      for (uint32_t x = 0; x < (uint32_t)VGA_GFX_WIDTH; x++) {
        uint32_t src_x = (x * info.width) / (uint32_t)VGA_GFX_WIDTH;
        if (src_x >= info.width)
          src_x = info.width - 1u;
        dst_row[x] = src_row[src_x];
      }
    }

    kfree(decoded);
  } else {
    int surf = gfx2d_surface_alloc(VGA_GFX_WIDTH, TASKBAR_Y);
    if (surf < 0) {
      return BMP_ENOMEM;
    }

    if (bmp_decode_to_surface_fit(path, surf, VGA_GFX_WIDTH, TASKBAR_Y) != BMP_OK) {
      gfx2d_surface_free(surf);
      return BMP_EFORMAT;
    }

    {
      int sw = 0;
      int sh = 0;
      uint32_t src_stride = 0;
      size_t row_bytes = (size_t)((uint32_t)VGA_GFX_WIDTH * sizeof(uint32_t));
      uint32_t *src = gfx2d_surface_data(surf, &sw, &sh);
      if (!src || sw < VGA_GFX_WIDTH || sh < TASKBAR_Y) {
        gfx2d_surface_free(surf);
        return BMP_EFORMAT;
      }
      src_stride = (uint32_t)sw;
      for (uint32_t y = 0; y < (uint32_t)TASKBAR_Y; y++) {
        memcpy(desktop_bg_bmp_scaled + y * (uint32_t)VGA_GFX_WIDTH,
               src + y * src_stride,
               row_bytes);
      }
    }
    gfx2d_surface_free(surf);
  }

  desktop_bg_copy_path(desktop_bg_bmp_path, path, VFS_MAX_PATH);
  desktop_bg_mode = DESKTOP_BG_BMP;
  desktop_invalidate_workspace_cache();
  return BMP_OK;
}

static int desktop_bg_apply_tile_bmp(const char *path) {
  bmp_info_t info;
  uint32_t src_w;
  uint32_t src_h;
  uint32_t tile_w;
  uint32_t tile_h;
  uint32_t *pixels;
  uint32_t pixel_count;
  uint32_t bytes;
  const uint32_t max_tile_dim = 256u;

  if (!path || !path[0])
    return BMP_EINVAL;

  if (bmp_get_info(path, &info) != BMP_OK)
    return BMP_EFORMAT;

  if (info.width == 0 || info.height == 0)
    return BMP_EFORMAT;

  src_w = info.width;
  src_h = info.height;
  tile_w = src_w;
  tile_h = src_h;

  if (tile_w > max_tile_dim || tile_h > max_tile_dim) {
    if (tile_w >= tile_h) {
      tile_h = (tile_h * max_tile_dim) / tile_w;
      tile_w = max_tile_dim;
    } else {
      tile_w = (tile_w * max_tile_dim) / tile_h;
      tile_h = max_tile_dim;
    }
    if (tile_w == 0)
      tile_w = 1;
    if (tile_h == 0)
      tile_h = 1;
  }

  if (tile_w > 0xFFFFFFFFu / tile_h)
    return BMP_EFORMAT;
  pixel_count = tile_w * tile_h;
  bytes = pixel_count * 4u;

  pixels = (uint32_t *)kmalloc(bytes);
  if (!pixels)
    return BMP_ENOMEM;

  if (tile_w == src_w && tile_h == src_h && info.data_size == bytes &&
      bmp_decode(path, pixels, bytes) == BMP_OK) {
    /* Decoded directly into final tile buffer. */
  } else {
    int surf = gfx2d_surface_alloc((int)tile_w, (int)tile_h);
    if (surf < 0) {
      kfree(pixels);
      return BMP_ENOMEM;
    }

    if (bmp_decode_to_surface_fit(path, surf, (int)tile_w, (int)tile_h) !=
        BMP_OK) {
      gfx2d_surface_free(surf);
      kfree(pixels);
      return BMP_EFORMAT;
    }

    {
      int sw = 0;
      int sh = 0;
      uint32_t src_stride = 0;
      uint32_t row_words = 0;
      size_t row_bytes = 0;
      uint32_t *src = gfx2d_surface_data(surf, &sw, &sh);
      if (!src || sw < (int)tile_w || sh < (int)tile_h) {
        gfx2d_surface_free(surf);
        kfree(pixels);
        return BMP_EFORMAT;
      }

      src_stride = (uint32_t)sw;
      row_words = tile_w;
      row_bytes = (size_t)row_words * sizeof(uint32_t);

      for (uint32_t y = 0; y < tile_h; y++) {
        memcpy(pixels + y * row_words,
               src + y * src_stride,
               row_bytes);
      }
    }

    gfx2d_surface_free(surf);
  }

  if (desktop_tile_bmp_data)
    kfree(desktop_tile_bmp_data);

  desktop_tile_bmp_data = pixels;
  desktop_tile_bmp_w = (int)tile_w;
  desktop_tile_bmp_h = (int)tile_h;
  desktop_bg_copy_path(desktop_tile_bmp_path, path, VFS_MAX_PATH);
  desktop_tile_use_bmp = 1;
  return BMP_OK;
}

static void desktop_draw_tiled_region(int x0, int y0, int x1, int y1) {
  uint32_t *fb = vga_get_framebuffer();
  int y;

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > VGA_GFX_WIDTH) x1 = VGA_GFX_WIDTH;
  if (y1 > TASKBAR_Y) y1 = TASKBAR_Y;
  if (x1 <= x0 || y1 <= y0)
    return;

  if (desktop_tile_use_bmp && desktop_tile_bmp_data &&
      desktop_tile_bmp_w > 0 && desktop_tile_bmp_h > 0) {
    for (y = y0; y < y1; y++) {
      int x;
      int ty = y % desktop_tile_bmp_h;
      uint32_t *dst = fb + y * VGA_GFX_WIDTH;
      for (x = x0; x < x1; x++) {
        int tx = x % desktop_tile_bmp_w;
        dst[x] = desktop_tile_bmp_data[ty * desktop_tile_bmp_w + tx];
      }
    }
    return;
  }

  for (y = y0; y < y1; y++) {
    int x;
    uint32_t *dst = fb + y * VGA_GFX_WIDTH;
    for (x = x0; x < x1; x++) {
      int local_x = x & 7;
      int local_y = y & 7;
      int on = 0;

      if (desktop_tile_pattern == DESKTOP_TILE_PATTERN_DIAG) {
        on = ((local_x + local_y) & 3) == 0;
      } else if (desktop_tile_pattern == DESKTOP_TILE_PATTERN_DOTS) {
        on = (local_x == 1 || local_x == 5) && (local_y == 1 || local_y == 5);
      } else {
        on = (((local_x >> 1) + (local_y >> 1)) & 1);
      }

      dst[x] = on ? desktop_tile_fg : desktop_tile_bg;
    }
  }
}

static int desktop_parse_uint_dec(const char *s, uint32_t *out) {
  uint32_t v = 0;
  int i = 0;
  if (!s || !out)
    return 0;
  if (s[0] == '\0')
    return 0;
  while (s[i] >= '0' && s[i] <= '9') {
    v = v * 10u + (uint32_t)(s[i] - '0');
    i++;
  }
  if (i == 0)
    return 0;
  *out = v;
  return 1;
}

static int desktop_parse_uint_hex(const char *s, uint32_t *out) {
  uint32_t v = 0;
  int i = 0;
  if (!s || !out)
    return 0;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    s += 2;
  if (s[0] == '\0')
    return 0;
  while (s[i]) {
    uint32_t d;
    char c = s[i];
    if (c >= '0' && c <= '9')
      d = (uint32_t)(c - '0');
    else if (c >= 'a' && c <= 'f')
      d = (uint32_t)(10 + c - 'a');
    else if (c >= 'A' && c <= 'F')
      d = (uint32_t)(10 + c - 'A');
    else
      return 0;
    v = (v << 4) | d;
    i++;
  }
  *out = v;
  return 1;
}

static void desktop_bg_save_config(void) {
  int fd = vfs_open(DESKTOP_BG_CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0)
    return;

  {
    const char *hdr = "# cupid-os desktop background settings\n";
    vfs_write(fd, hdr, (uint32_t)strlen(hdr));
  }

  {
    char line[64];
    int pos = 0;
    uint32_t mode = (uint32_t)desktop_bg_mode;

    line[pos++] = 'm'; line[pos++] = 'o'; line[pos++] = 'd'; line[pos++] = 'e';
    line[pos++] = '=';
    if (mode == 0) {
      line[pos++] = '0';
    } else {
      char tmp[16];
      int ti = 0;
      while (mode > 0 && ti < 15) {
        tmp[ti++] = (char)('0' + (mode % 10u));
        mode /= 10u;
      }
      while (ti > 0)
        line[pos++] = tmp[--ti];
    }
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[64];
    const char *hx = "0123456789ABCDEF";
    uint32_t c = desktop_tile_fg & 0x00FFFFFFu;
    int pos = 0;
    line[pos++] = 't'; line[pos++] = 'i'; line[pos++] = 'l'; line[pos++] = 'e'; line[pos++] = '_';
    line[pos++] = 'f'; line[pos++] = 'g'; line[pos++] = '=';
    line[pos++] = '0'; line[pos++] = 'x';
    line[pos++] = hx[(c >> 20) & 0xFu];
    line[pos++] = hx[(c >> 16) & 0xFu];
    line[pos++] = hx[(c >> 12) & 0xFu];
    line[pos++] = hx[(c >> 8) & 0xFu];
    line[pos++] = hx[(c >> 4) & 0xFu];
    line[pos++] = hx[c & 0xFu];
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[64];
    const char *hx = "0123456789ABCDEF";
    uint32_t c = desktop_tile_bg & 0x00FFFFFFu;
    int pos = 0;
    line[pos++] = 't'; line[pos++] = 'i'; line[pos++] = 'l'; line[pos++] = 'e'; line[pos++] = '_';
    line[pos++] = 'b'; line[pos++] = 'g'; line[pos++] = '=';
    line[pos++] = '0'; line[pos++] = 'x';
    line[pos++] = hx[(c >> 20) & 0xFu];
    line[pos++] = hx[(c >> 16) & 0xFu];
    line[pos++] = hx[(c >> 12) & 0xFu];
    line[pos++] = hx[(c >> 8) & 0xFu];
    line[pos++] = hx[(c >> 4) & 0xFu];
    line[pos++] = hx[c & 0xFu];
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[32];
    int pos = 0;
    uint32_t p = (uint32_t)desktop_tile_pattern;
    line[pos++] = 't'; line[pos++] = 'i'; line[pos++] = 'l'; line[pos++] = 'e'; line[pos++] = '_';
    line[pos++] = 'p'; line[pos++] = 'a'; line[pos++] = 't'; line[pos++] = 't'; line[pos++] = 'e'; line[pos++] = 'r'; line[pos++] = 'n';
    line[pos++] = '=';
    line[pos++] = (char)('0' + (p % 10u));
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[32];
    int pos = 0;
    line[pos++] = 't'; line[pos++] = 'i'; line[pos++] = 'l'; line[pos++] = 'e'; line[pos++] = '_';
    line[pos++] = 'u'; line[pos++] = 's'; line[pos++] = 'e'; line[pos++] = '_';
    line[pos++] = 'b'; line[pos++] = 'm'; line[pos++] = 'p';
    line[pos++] = '=';
    line[pos++] = desktop_tile_use_bmp ? '1' : '0';
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[192];
    int pos = 0;
    int i = 0;
    line[pos++] = 't'; line[pos++] = 'i'; line[pos++] = 'l'; line[pos++] = 'e'; line[pos++] = '_';
    line[pos++] = 'b'; line[pos++] = 'm'; line[pos++] = 'p'; line[pos++] = '=';
    while (desktop_tile_bmp_path[i] && pos < 190) {
      line[pos++] = desktop_tile_bmp_path[i++];
    }
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[64];
    const char *hx = "0123456789ABCDEF";
    uint32_t c = desktop_bg_solid & 0x00FFFFFFu;
    int pos = 0;
    line[pos++] = 's'; line[pos++] = 'o'; line[pos++] = 'l'; line[pos++] = 'i'; line[pos++] = 'd';
    line[pos++] = '=';
    line[pos++] = '0';
    line[pos++] = 'x';
    line[pos++] = hx[(c >> 20) & 0xFu];
    line[pos++] = hx[(c >> 16) & 0xFu];
    line[pos++] = hx[(c >> 12) & 0xFu];
    line[pos++] = hx[(c >> 8) & 0xFu];
    line[pos++] = hx[(c >> 4) & 0xFu];
    line[pos++] = hx[c & 0xFu];
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[64];
    const char *hx = "0123456789ABCDEF";
    uint32_t c = desktop_bg_grad_top & 0x00FFFFFFu;
    int pos = 0;
    line[pos++] = 'g'; line[pos++] = 'r'; line[pos++] = 'a'; line[pos++] = 'd'; line[pos++] = '_';
    line[pos++] = 't'; line[pos++] = 'o'; line[pos++] = 'p';
    line[pos++] = '=';
    line[pos++] = '0';
    line[pos++] = 'x';
    line[pos++] = hx[(c >> 20) & 0xFu];
    line[pos++] = hx[(c >> 16) & 0xFu];
    line[pos++] = hx[(c >> 12) & 0xFu];
    line[pos++] = hx[(c >> 8) & 0xFu];
    line[pos++] = hx[(c >> 4) & 0xFu];
    line[pos++] = hx[c & 0xFu];
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[64];
    const char *hx = "0123456789ABCDEF";
    uint32_t c = desktop_bg_grad_bottom & 0x00FFFFFFu;
    int pos = 0;
    line[pos++] = 'g'; line[pos++] = 'r'; line[pos++] = 'a'; line[pos++] = 'd'; line[pos++] = '_';
    line[pos++] = 'b'; line[pos++] = 'o'; line[pos++] = 't'; line[pos++] = 't'; line[pos++] = 'o'; line[pos++] = 'm';
    line[pos++] = '=';
    line[pos++] = '0';
    line[pos++] = 'x';
    line[pos++] = hx[(c >> 20) & 0xFu];
    line[pos++] = hx[(c >> 16) & 0xFu];
    line[pos++] = hx[(c >> 12) & 0xFu];
    line[pos++] = hx[(c >> 8) & 0xFu];
    line[pos++] = hx[(c >> 4) & 0xFu];
    line[pos++] = hx[c & 0xFu];
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[192];
    int pos = 0;
    int i = 0;
    line[pos++] = 'b'; line[pos++] = 'm'; line[pos++] = 'p'; line[pos++] = '=';
    while (desktop_bg_bmp_path[i] && pos < 190) {
      line[pos++] = desktop_bg_bmp_path[i++];
    }
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  {
    char line[64];
    int pos = 0;
    uint32_t theme = (uint32_t)desktop_anim_theme;

    line[pos++] = 't'; line[pos++] = 'h'; line[pos++] = 'e'; line[pos++] = 'm'; line[pos++] = 'e';
    line[pos++] = '=';
    if (theme == 0) {
      line[pos++] = '0';
    } else {
      char tmp[16];
      int ti = 0;
      while (theme > 0 && ti < 15) {
        tmp[ti++] = (char)('0' + (theme % 10u));
        theme /= 10u;
      }
      while (ti > 0)
        line[pos++] = tmp[--ti];
    }
    line[pos++] = '\n';
    vfs_write(fd, line, (uint32_t)pos);
  }

  vfs_close(fd);
}

static void desktop_bg_load_config(void) {
  int fd = vfs_open(DESKTOP_BG_CONFIG_PATH, O_RDONLY);
  char buf[512];
  int n;
  uint32_t mode = (uint32_t)desktop_bg_mode;
  uint32_t solid = desktop_bg_solid;
  uint32_t grad_top = desktop_bg_grad_top;
  uint32_t grad_bottom = desktop_bg_grad_bottom;
  uint32_t tile_fg = desktop_tile_fg;
  uint32_t tile_bg = desktop_tile_bg;
  uint32_t tile_pattern = (uint32_t)desktop_tile_pattern;
  uint32_t tile_use_bmp = (uint32_t)desktop_tile_use_bmp;
  uint32_t theme = (uint32_t)desktop_anim_theme;
  char bmp_path[VFS_MAX_PATH];
  char tile_bmp_path[VFS_MAX_PATH];

  bmp_path[0] = '\0';
  tile_bmp_path[0] = '\0';
  if (fd < 0)
    return;

  n = vfs_read(fd, buf, 511);
  vfs_close(fd);
  if (n <= 0)
    return;
  buf[n] = '\0';

  {
    char *line = buf;
    while (line && *line) {
      char *next = strchr(line, '\n');
      if (next)
        *next = '\0';

      if (line[0] != '#' && line[0] != '\0' && line[0] != '\r') {
        if (strncmp(line, "mode=", 5) == 0) {
          uint32_t v;
          if (desktop_parse_uint_dec(line + 5, &v) && v <= 4u)
            mode = v;
        } else if (strncmp(line, "theme=", 6) == 0) {
          uint32_t v;
          if (desktop_parse_uint_dec(line + 6, &v) && v <= 3u)
            theme = v;
        } else if (strncmp(line, "solid=", 6) == 0) {
          uint32_t c;
          if (desktop_parse_uint_hex(line + 6, &c))
            solid = c & 0x00FFFFFFu;
        } else if (strncmp(line, "grad_top=", 9) == 0) {
          uint32_t c;
          if (desktop_parse_uint_hex(line + 9, &c))
            grad_top = c & 0x00FFFFFFu;
        } else if (strncmp(line, "grad_bottom=", 12) == 0) {
          uint32_t c;
          if (desktop_parse_uint_hex(line + 12, &c))
            grad_bottom = c & 0x00FFFFFFu;
        } else if (strncmp(line, "tile_fg=", 8) == 0) {
          uint32_t c;
          if (desktop_parse_uint_hex(line + 8, &c))
            tile_fg = c & 0x00FFFFFFu;
        } else if (strncmp(line, "tile_bg=", 8) == 0) {
          uint32_t c;
          if (desktop_parse_uint_hex(line + 8, &c))
            tile_bg = c & 0x00FFFFFFu;
        } else if (strncmp(line, "tile_pattern=", 13) == 0) {
          uint32_t v;
          if (desktop_parse_uint_dec(line + 13, &v) && v <= 2u)
            tile_pattern = v;
        } else if (strncmp(line, "tile_use_bmp=", 13) == 0) {
          uint32_t v;
          if (desktop_parse_uint_dec(line + 13, &v) && (v == 0u || v == 1u))
            tile_use_bmp = v;
        } else if (strncmp(line, "tile_bmp=", 9) == 0) {
          desktop_bg_copy_path(tile_bmp_path, line + 9, VFS_MAX_PATH);
        } else if (strncmp(line, "bmp=", 4) == 0) {
          desktop_bg_copy_path(bmp_path, line + 4, VFS_MAX_PATH);
        }
      }

      if (next)
        line = next + 1;
      else
        break;
    }
  }

  desktop_bg_solid = solid;
  desktop_bg_grad_top = grad_top;
  desktop_bg_grad_bottom = grad_bottom;
  desktop_tile_fg = tile_fg;
  desktop_tile_bg = tile_bg;
  desktop_tile_pattern = (uint8_t)tile_pattern;
  desktop_tile_use_bmp = (uint8_t)tile_use_bmp;
  desktop_bg_copy_path(desktop_tile_bmp_path, tile_bmp_path, VFS_MAX_PATH);
  desktop_anim_theme = (uint8_t)theme;
  desktop_bg_mode = (uint8_t)mode;
  desktop_invalidate_workspace_cache();

  if (desktop_bg_mode == DESKTOP_BG_BMP) {
    if (!bmp_path[0] || desktop_bg_apply_bmp(bmp_path) != BMP_OK) {
      desktop_bg_mode = DESKTOP_BG_SOLID;
      desktop_bg_bmp_path[0] = '\0';
    }
  } else if (bmp_path[0]) {
    desktop_bg_copy_path(desktop_bg_bmp_path, bmp_path, VFS_MAX_PATH);
  }

  if (desktop_tile_bmp_data) {
    kfree(desktop_tile_bmp_data);
    desktop_tile_bmp_data = NULL;
    desktop_tile_bmp_w = 0;
    desktop_tile_bmp_h = 0;
  }

  if (desktop_tile_use_bmp && desktop_tile_bmp_path[0]) {
    if (desktop_bg_apply_tile_bmp(desktop_tile_bmp_path) != BMP_OK) {
      desktop_tile_use_bmp = 0;
      desktop_tile_bmp_path[0] = '\0';
    }
  }
}

static void desktop_open_bg_settings_dialog(void) {
  bool done = false;
  bool apply_changes = false;
  uint8_t prev_buttons = mouse.buttons;

  int r = (int)((desktop_bg_solid >> 16) & 0xFFu);
  int g = (int)((desktop_bg_solid >> 8) & 0xFFu);
  int b = (int)(desktop_bg_solid & 0xFFu);
  bool use_bmp =
      ((desktop_bg_mode == DESKTOP_BG_BMP && desktop_bg_bmp_path[0]) ||
       (desktop_bg_mode == DESKTOP_BG_TILED && desktop_tile_use_bmp &&
        desktop_tile_bmp_path[0]));
  bool use_tile_bmp =
      (desktop_bg_mode == DESKTOP_BG_TILED && desktop_tile_use_bmp);
  bool drag_r = false, drag_g = false, drag_b = false;
  char bmp_path[VFS_MAX_PATH];
  bmp_path[0] = '\0';
  if (desktop_bg_mode == DESKTOP_BG_TILED && desktop_tile_use_bmp &&
      desktop_tile_bmp_path[0]) {
    desktop_bg_copy_path(bmp_path, desktop_tile_bmp_path, VFS_MAX_PATH);
  } else if (desktop_bg_bmp_path[0]) {
    desktop_bg_copy_path(bmp_path, desktop_bg_bmp_path, VFS_MAX_PATH);
  }

  while (!done) {
    key_event_t evt;
    while (keyboard_read_event(&evt)) {
      if (!evt.pressed)
        continue;
      if (evt.scancode == 0x01) {
        done = true;
        apply_changes = false;
      } else if (evt.scancode == 0x1C) {
        done = true;
        apply_changes = true;
      }
    }

    int16_t mx = mouse.x;
    int16_t my = mouse.y;
    uint8_t btn = mouse.buttons;
    bool pressed = (btn & MOUSE_LEFT) && !(prev_buttons & MOUSE_LEFT);
    bool released = !(btn & MOUSE_LEFT) && (prev_buttons & MOUSE_LEFT);

    int16_t dw = 430;
    int16_t dh = 272;
    int16_t dx = (int16_t)((VGA_GFX_WIDTH - dw) / 2);
    int16_t dy = (int16_t)((TASKBAR_Y - dh) / 2);

    ui_rect_t dialog = ui_rect(dx, dy, (uint16_t)dw, (uint16_t)dh);
    ui_rect_t title = ui_rect((int16_t)(dx + 2), (int16_t)(dy + 2),
                              (uint16_t)(dw - 4), 16);

    ui_rect_t row_r = ui_rect((int16_t)(dx + 16), (int16_t)(dy + 36),
                              (uint16_t)(dw - 150), 18);
    ui_rect_t row_g = ui_rect((int16_t)(dx + 16), (int16_t)(dy + 62),
                              (uint16_t)(dw - 150), 18);
    ui_rect_t row_b = ui_rect((int16_t)(dx + 16), (int16_t)(dy + 88),
                              (uint16_t)(dw - 150), 18);
    ui_rect_t swatch = ui_rect((int16_t)(dx + dw - 118), (int16_t)(dy + 36),
                               100, 70);
    ui_rect_t use_bmp_box = ui_rect((int16_t)(dx + 16), (int16_t)(dy + 118),
                                    140, 16);
    ui_rect_t use_tile_bmp_box =
      ui_rect((int16_t)(dx + 16), (int16_t)(dy + 142), 210, 16);
    ui_rect_t browse_btn = ui_rect((int16_t)(dx + 240), (int16_t)(dy + 114),
                     172, 22);
    ui_rect_t use_color_btn =
      ui_rect((int16_t)(dx + 16), (int16_t)(dy + 188), 120, 22);
    ui_rect_t apply_btn =
        ui_rect((int16_t)(dx + dw - 150), (int16_t)(dy + dh - 32), 64, 22);
    ui_rect_t cancel_btn =
        ui_rect((int16_t)(dx + dw - 78), (int16_t)(dy + dh - 32), 64, 22);

    if (pressed) {
      if (ui_contains(row_r, mx, my))
        drag_r = true;
      if (ui_contains(row_g, mx, my))
        drag_g = true;
      if (ui_contains(row_b, mx, my))
        drag_b = true;

      if (ui_contains(browse_btn, mx, my)) {
        char selected[VFS_MAX_PATH];
        if (gfx2d_file_dialog_open("/home", selected, ".bmp") == 1) {
          desktop_bg_copy_path(bmp_path, selected, VFS_MAX_PATH);
          use_bmp = true;
        }
      }

      if (ui_contains(use_color_btn, mx, my)) {
        use_bmp = false;
        use_tile_bmp = false;
      }

      if (ui_draw_checkbox(use_bmp_box, "Use BMP image", use_bmp, true, mx,
                           my, true)) {
        use_bmp = !use_bmp;
        if (!use_bmp)
          use_tile_bmp = false;
      }

      if (ui_draw_checkbox(use_tile_bmp_box, "Use Tile BMP image",
                           use_tile_bmp, true, mx, my, true)) {
        use_tile_bmp = !use_tile_bmp;
        if (use_tile_bmp)
          use_bmp = true;
      }

      if (ui_contains(apply_btn, mx, my)) {
        done = true;
        apply_changes = true;
      } else if (ui_contains(cancel_btn, mx, my)) {
        done = true;
        apply_changes = false;
      }
    }

    if (released) {
      drag_r = false;
      drag_g = false;
      drag_b = false;
    }
    prev_buttons = btn;

    desktop_anim_tick++;
    desktop_draw_background();
    desktop_draw_icons();
    gui_draw_all_windows(true); /* background always repainted in this loop */
    desktop_draw_taskbar();
    desktop_draw_calendar();

    ui_draw_shadow(dialog, COLOR_TEXT, 2);
    ui_draw_panel(dialog, COLOR_WINDOW_BG, true, true);
    ui_draw_titlebar(title, "Change Desktop Background", true);

    r = ui_draw_slider_labeled(row_r, "Red", r, 0, 255, drag_r, mx, my);
    g = ui_draw_slider_labeled(row_g, "Green", g, 0, 255, drag_g, mx, my);
    b = ui_draw_slider_labeled(row_b, "Blue", b, 0, 255, drag_b, mx, my);

    ui_draw_panel(swatch, COLOR_WINDOW_BG, true, false);
    gfx_fill_rect((int16_t)(swatch.x + 4), (int16_t)(swatch.y + 4),
                  (uint16_t)(swatch.w - 8), (uint16_t)(swatch.h - 8),
                  ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);

    (void)ui_draw_checkbox(use_bmp_box, "Use BMP image", use_bmp, true, mx, my,
                           false);
    (void)ui_draw_checkbox(use_tile_bmp_box, "Use Tile BMP image",
                           use_tile_bmp, true, mx, my, false);
    ui_draw_button(browse_btn, "Browse BMP...", false);
    ui_draw_button(use_color_btn, "Use Color", false);
    ui_draw_button(apply_btn, "Apply", true);
    ui_draw_button(cancel_btn, "Cancel", false);

    {
      char path_disp[56];
      int i = 0;
      const char *active_path = bmp_path;

      if (active_path[0]) {
        while (active_path[i] && i < 55) {
          path_disp[i] = active_path[i];
          i++;
        }
      }
      path_disp[i] = '\0';
      ui_draw_label(ui_rect((int16_t)(dx + 16), (int16_t)(dy + 220),
                            (uint16_t)(dw - 32), 16),
                    active_path[0] ? path_disp : "No BMP selected", COLOR_TEXT,
                    UI_ALIGN_LEFT);
    }

    mouse_save_under_cursor();
    mouse_draw_cursor();
    vga_mark_dirty_full();
    vga_flip();
    process_yield();
  }

  if (!apply_changes)
    return;

  desktop_bg_solid = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
  if (use_bmp) {
    if (!bmp_path[0]) {
      gfx2d_message_dialog("Please choose a BMP file first.");
      desktop_bg_mode = DESKTOP_BG_SOLID;
      return;
    }
    if (use_tile_bmp) {
      if (desktop_bg_set_mode_tiled_bmp(bmp_path) != 0) {
        gfx2d_message_dialog("Failed to load Tile BMP background.");
        desktop_bg_mode = DESKTOP_BG_SOLID;
        desktop_bg_save_config();
        return;
      }
    } else {
      if (desktop_bg_apply_bmp(bmp_path) != BMP_OK) {
        gfx2d_message_dialog("Failed to load BMP background.");
        desktop_bg_mode = DESKTOP_BG_SOLID;
        desktop_bg_save_config();
        return;
      }
      desktop_bg_mode = DESKTOP_BG_BMP;
    }
  } else {
    desktop_bg_mode = DESKTOP_BG_SOLID;
    desktop_tile_use_bmp = 0;
  }

  desktop_invalidate_workspace_cache();

  desktop_bg_save_config();
}

void desktop_bg_set_mode_anim(void) {
  desktop_bg_mode = DESKTOP_BG_ANIM;
  desktop_invalidate_workspace_cache();
  desktop_bg_save_config();
}

void desktop_bg_set_mode_solid(uint32_t color) {
  desktop_bg_solid = color & 0x00FFFFFFu;
  desktop_bg_mode = DESKTOP_BG_SOLID;
  desktop_invalidate_workspace_cache();
  desktop_bg_save_config();
}

void desktop_bg_set_mode_gradient(uint32_t top_color, uint32_t bottom_color) {
  desktop_bg_grad_top = top_color & 0x00FFFFFFu;
  desktop_bg_grad_bottom = bottom_color & 0x00FFFFFFu;
  desktop_bg_mode = DESKTOP_BG_GRADIENT;
  desktop_invalidate_workspace_cache();
  desktop_bg_save_config();
}

void desktop_bg_set_mode_tiled_pattern(int pattern, uint32_t fg, uint32_t bg) {
  if (pattern < DESKTOP_TILE_PATTERN_CHECKER)
    pattern = DESKTOP_TILE_PATTERN_CHECKER;
  if (pattern > DESKTOP_TILE_PATTERN_DOTS)
    pattern = DESKTOP_TILE_PATTERN_DOTS;

  desktop_tile_pattern = (uint8_t)pattern;
  desktop_tile_fg = fg & 0x00FFFFFFu;
  desktop_tile_bg = bg & 0x00FFFFFFu;
  desktop_tile_use_bmp = 0;
  desktop_bg_mode = DESKTOP_BG_TILED;
  desktop_invalidate_workspace_cache();
  desktop_bg_save_config();
}

int desktop_bg_set_mode_tiled_bmp(const char *path) {
  int rc = desktop_bg_apply_tile_bmp(path);
  if (rc != BMP_OK)
    return rc;
  desktop_bg_mode = DESKTOP_BG_TILED;
  desktop_invalidate_workspace_cache();
  desktop_bg_save_config();
  return BMP_OK;
}

int desktop_bg_set_mode_bmp(const char *path) {
  int rc = desktop_bg_apply_bmp(path);
  if (rc != BMP_OK) {
    return rc;
  }
  desktop_bg_mode = DESKTOP_BG_BMP;
  desktop_invalidate_workspace_cache();
  desktop_bg_save_config();
  return BMP_OK;
}

int desktop_bg_get_mode(void) { return (int)desktop_bg_mode; }

uint32_t desktop_bg_get_solid_color(void) { return desktop_bg_solid & 0x00FFFFFFu; }

void desktop_bg_set_anim_theme(int theme) {
  (void)theme;
  desktop_anim_theme = DESKTOP_ANIM_THEME_KITTY;
  desktop_bg_mode = DESKTOP_BG_ANIM;
  desktop_invalidate_workspace_cache();
  desktop_bg_save_config();
}

int desktop_bg_get_anim_theme(void) { return DESKTOP_ANIM_THEME_KITTY; }

int desktop_bg_get_tiled_pattern(void) { return (int)desktop_tile_pattern; }

int desktop_bg_get_tiled_use_bmp(void) { return (int)desktop_tile_use_bmp; }

void desktop_draw_background(void) {
  workspace_base_drawn_with_icons = false;

  if (desktop_bg_mode != DESKTOP_BG_ANIM) {
    size_t workspace_bytes =
        (size_t)TASKBAR_Y * (size_t)VGA_GFX_WIDTH * sizeof(uint32_t);
    bool has_bmp = (desktop_bg_mode == DESKTOP_BG_BMP && desktop_bg_bmp_scaled);
    bool has_grad = (desktop_bg_mode == DESKTOP_BG_GRADIENT);
    bool has_tiled = (desktop_bg_mode == DESKTOP_BG_TILED);
    bool can_cache = (desktop_bg_mode == DESKTOP_BG_SOLID) || has_bmp ||
             has_grad || has_tiled;

    if (can_cache && !workspace_base_cache) {
      workspace_base_cache = (uint32_t *)kmalloc((uint32_t)workspace_bytes);
    }

    if (can_cache && workspace_base_cache) {
      bool cache_match = workspace_base_cache_valid &&
                         workspace_cache_bg_mode == desktop_bg_mode &&
                         workspace_cache_bg_solid == desktop_bg_solid &&
                         workspace_cache_bg_grad_top == desktop_bg_grad_top &&
                         workspace_cache_bg_grad_bottom == desktop_bg_grad_bottom &&
                         workspace_cache_tile_fg == desktop_tile_fg &&
                         workspace_cache_tile_bg == desktop_tile_bg &&
                         workspace_cache_tile_pattern == desktop_tile_pattern &&
                         workspace_cache_tile_use_bmp == desktop_tile_use_bmp &&
                         workspace_cache_has_bmp == has_bmp &&
                         workspace_cache_icon_stamp == desktop_icon_stamp;

      if (!cache_match) {
        uint32_t *fb_build = vga_get_framebuffer();
        if (desktop_bg_mode == DESKTOP_BG_BMP && desktop_bg_bmp_scaled) {
          simd_memcpy(fb_build, desktop_bg_bmp_scaled, (uint32_t)workspace_bytes);
        } else if (desktop_bg_mode == DESKTOP_BG_GRADIENT) {
          gfx2d_gradient_v(0, 0, VGA_GFX_WIDTH, TASKBAR_Y,
                           desktop_bg_grad_top, desktop_bg_grad_bottom);
        } else if (desktop_bg_mode == DESKTOP_BG_TILED) {
          desktop_draw_tiled_region(0, 0, VGA_GFX_WIDTH, TASKBAR_Y);
        } else {
          gfx_fill_rect(0, 0, VGA_GFX_WIDTH, TASKBAR_Y, desktop_bg_solid);
        }
        gfx2d_icons_draw_all();
        simd_memcpy(workspace_base_cache, fb_build, (uint32_t)workspace_bytes);

        workspace_base_cache_valid = true;
        workspace_cache_bg_mode = desktop_bg_mode;
        workspace_cache_bg_solid = desktop_bg_solid;
        workspace_cache_bg_grad_top = desktop_bg_grad_top;
        workspace_cache_bg_grad_bottom = desktop_bg_grad_bottom;
        workspace_cache_tile_fg = desktop_tile_fg;
        workspace_cache_tile_bg = desktop_tile_bg;
        workspace_cache_tile_pattern = desktop_tile_pattern;
        workspace_cache_tile_use_bmp = desktop_tile_use_bmp;
        workspace_cache_has_bmp = has_bmp;
        workspace_cache_icon_stamp = desktop_icon_stamp;
      }

      simd_memcpy(vga_get_framebuffer(), workspace_base_cache, (uint32_t)workspace_bytes);
      workspace_base_drawn_with_icons = true;
      return;
    }
  }

  if (desktop_bg_mode == DESKTOP_BG_BMP && desktop_bg_bmp_scaled) {
    uint32_t *fb = vga_get_framebuffer();
    simd_memcpy(fb, desktop_bg_bmp_scaled,
                (uint32_t)TASKBAR_Y * (uint32_t)VGA_GFX_WIDTH * 4u);
    return;
  }

  if (desktop_bg_mode == DESKTOP_BG_SOLID) {
    gfx_fill_rect(0, 0, VGA_GFX_WIDTH, TASKBAR_Y, desktop_bg_solid);
    return;
  }

  if (desktop_bg_mode == DESKTOP_BG_GRADIENT) {
    gfx2d_gradient_v(0, 0, VGA_GFX_WIDTH, TASKBAR_Y,
                     desktop_bg_grad_top, desktop_bg_grad_bottom);
    return;
  }

  if (desktop_bg_mode == DESKTOP_BG_TILED) {
    desktop_draw_tiled_region(0, 0, VGA_GFX_WIDTH, TASKBAR_Y);
    return;
  }

  uint32_t top = desktop_anim_top_color();
  desktop_update_bg_lut(top);

  /* Fill from LUT directly into framebuffer (faster than per-line API calls). */
  {
    uint32_t *fb = vga_get_framebuffer();
    for (int row = 0; row < (int)TASKBAR_Y; row++) {
      desktop_fill_row32(fb + (uint32_t)row * (uint32_t)VGA_GFX_WIDTH,
                         VGA_GFX_WIDTH, bg_lut[row]);
    }
  }

  desktop_draw_theme_kitty();
}

static void desktop_redraw_workspace_region(int16_t x, int16_t y,
                                            uint16_t w, uint16_t h) {
  if (w == 0 || h == 0)
    return;

  int x0 = x;
  int y0 = y;
  int x1 = x + (int16_t)w;
  int y1 = y + (int16_t)h;

  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > VGA_GFX_WIDTH)
    x1 = VGA_GFX_WIDTH;
  if (y1 > TASKBAR_Y)
    y1 = TASKBAR_Y;
  if (x1 <= x0 || y1 <= y0)
    return;

  desktop_mark_dirty_rect(x0, y0, x1 - x0, y1 - y0);

  bool used_cache = false;
  bool can_cache = (desktop_bg_mode == DESKTOP_BG_SOLID) ||
                   (desktop_bg_mode == DESKTOP_BG_BMP && desktop_bg_bmp_scaled) ||
                   (desktop_bg_mode == DESKTOP_BG_GRADIENT) ||
                   (desktop_bg_mode == DESKTOP_BG_TILED);

  if (can_cache && workspace_base_cache_valid && workspace_base_cache) {
    uint32_t cw = (uint32_t)(x1 - x0);
    uint32_t ch = (uint32_t)(y1 - y0);
    uint32_t *fb = vga_get_framebuffer();
    simd_blit_rect(fb + (uint32_t)y0 * (uint32_t)VGA_GFX_WIDTH + (uint32_t)x0,
                   workspace_base_cache + (uint32_t)y0 * (uint32_t)VGA_GFX_WIDTH + (uint32_t)x0,
                   (uint32_t)VGA_GFX_WIDTH, (uint32_t)VGA_GFX_WIDTH, cw, ch);
    used_cache = true;
  }

  if (!used_cache && desktop_bg_mode == DESKTOP_BG_BMP && desktop_bg_bmp_scaled) {
    uint32_t cw = (uint32_t)(x1 - x0);
    uint32_t ch = (uint32_t)(y1 - y0);
    uint32_t *fb = vga_get_framebuffer();
    simd_blit_rect(fb + (uint32_t)y0 * (uint32_t)VGA_GFX_WIDTH + (uint32_t)x0,
                   desktop_bg_bmp_scaled + (uint32_t)y0 * (uint32_t)VGA_GFX_WIDTH + (uint32_t)x0,
                   (uint32_t)VGA_GFX_WIDTH, (uint32_t)VGA_GFX_WIDTH, cw, ch);
  } else if (!used_cache && desktop_bg_mode == DESKTOP_BG_SOLID) {
    gfx_fill_rect((int16_t)x0, (int16_t)y0, (uint16_t)(x1 - x0),
                  (uint16_t)(y1 - y0), desktop_bg_solid);
  } else if (!used_cache && desktop_bg_mode == DESKTOP_BG_GRADIENT) {
    gfx2d_clip_set(x0, y0, x1 - x0, y1 - y0);
    gfx2d_gradient_v(0, 0, VGA_GFX_WIDTH, TASKBAR_Y,
                     desktop_bg_grad_top, desktop_bg_grad_bottom);
    gfx2d_clip_clear();
  } else if (!used_cache && desktop_bg_mode == DESKTOP_BG_TILED) {
    desktop_draw_tiled_region(x0, y0, x1, y1);
  } else if (!used_cache) {
    uint32_t top = desktop_anim_top_color();
    desktop_update_bg_lut(top);
    {
      uint32_t *fb = vga_get_framebuffer();
      int copy_w = x1 - x0;
      for (int row = y0; row < y1; row++) {
        desktop_fill_row32(fb + (uint32_t)row * (uint32_t)VGA_GFX_WIDTH +
                               (uint32_t)x0,
                           copy_w, bg_lut[row]);
      }
    }
  }

  gfx2d_clip_set(x0, y0, x1 - x0, y1 - y0);
  if (used_cache) {
    desktop_draw_icon_hover_fx();
  } else {
    desktop_draw_icons();
  }
  gfx2d_clip_clear();
}

static void desktop_draw_icon_hover_fx(void) {
  (void)0;
}

static void desktop_draw_taskbar_base(void) {
  uint32_t *fb = vga_get_framebuffer();
  uint32_t tb_bytes = (uint32_t)TASKBAR_HEIGHT * (uint32_t)VGA_GFX_WIDTH *
                      (uint32_t)sizeof(uint32_t);

  if (!taskbar_base_cache) {
    taskbar_base_cache = (uint32_t *)kmalloc(tb_bytes);
  }

  if (taskbar_base_cache_valid && taskbar_base_cache) {
    /* Single bulk copy: taskbar rows are contiguous in both buffers */
    simd_memcpy(fb + (uint32_t)TASKBAR_Y * (uint32_t)VGA_GFX_WIDTH,
                taskbar_base_cache, tb_bytes);
    return;
  }

  gfx2d_gradient_v(0, TASKBAR_Y, VGA_GFX_WIDTH, TASKBAR_HEIGHT, 0x00DDD0F0u,
                   COLOR_TASKBAR);
  gfx_draw_hline(0, TASKBAR_Y, VGA_GFX_WIDTH, COLOR_BORDER);
  gfx_draw_text(4, (int16_t)(TASKBAR_Y + 6), "cupid-os", COLOR_TEXT_LIGHT);

  if (taskbar_base_cache) {
    simd_memcpy(taskbar_base_cache,
                fb + (uint32_t)TASKBAR_Y * (uint32_t)VGA_GFX_WIDTH, tb_bytes);
    taskbar_base_cache_valid = true;
  }
}

void desktop_draw_taskbar(void) {
  desktop_draw_taskbar_base();

  /* Window buttons starting at x=80 */
  int wc = gui_window_count();
  for (int i = 0; i < wc && i < MAX_WINDOWS; i++) {
    /* We access windows via ID by iterating; use gui_get_window with
     * a scan approach since we don't have direct index access.
     * For simplicity, iterate through possible window IDs. */
    /* Actually, just draw a button per visible window by checking
     * the window array position. We'll use a helper approach. */
    break; /* Filled in the loop below */
  }

  /* Simple approach: iterate through the window array by index.
   * gui module stores windows internally; we re-check by calling
   * gui_get_window with each potential ID (1..next_id). This is
   * not ideal but works for 16 windows max. */
  /* Better: expose a simple iteration.  For now, we count and draw. */
  (void)wc;

  /* Draw a button for each window ID we can find */
  for (int idx = 0; idx < MAX_WINDOWS; idx++) {
    /* Try to find window at z-order position idx */
    /* We'll just scan by trying IDs 1..next_id */
    /* Simplification: we draw buttons as the desktop loop
     * knows the windows.  We use gui_window_count and
     * access via offset. */
    break;
  }

  /* Simplified taskbar buttons: just enumerate windows */
  {
    int16_t btn_x = TASKBAR_BTN_START;
    /* Reserve space for the clock on the right */
    int16_t btn_limit =
        (int16_t)(clock_hitbox_x > 0 ? clock_hitbox_x - 4 : VGA_GFX_WIDTH - 60);

    int wc_live = gui_window_count();
    for (int wi = 0; wi < wc_live && btn_x < btn_limit; wi++) {
      window_t *w = gui_get_window_by_index(wi);
      if (!w)
        continue;
      if (!(w->flags & WINDOW_FLAG_VISIBLE))
        continue;

      uint16_t btn_w = gfx_text_width(w->title);
      if (btn_w < 40)
        btn_w = 40;
      btn_w = (uint16_t)(btn_w + 8);
      /* Cap to max button width */
      if (btn_w > TASKBAR_BTN_MAX_W)
        btn_w = TASKBAR_BTN_MAX_W;

      /* Don't draw if it would overflow past the clock */
      if (btn_x + (int16_t)btn_w > btn_limit) {
        btn_w = (uint16_t)(btn_limit - btn_x);
        if (btn_w < 20)
          break; /* Too small to be useful */
      }

      uint32_t bg =
          (w->flags & WINDOW_FLAG_FOCUSED) ? COLOR_TASKBAR_ACT : COLOR_TASKBAR;
      gfx_fill_rect(btn_x, (int16_t)(TASKBAR_Y + 2), btn_w,
                    (uint16_t)(TASKBAR_HEIGHT - 4), bg);
      gfx_draw_rect(btn_x, (int16_t)(TASKBAR_Y + 2), btn_w,
                    (uint16_t)(TASKBAR_HEIGHT - 4), COLOR_BORDER);

      /* Draw truncated title: only as many chars as fit in btn_w - 8 */
      {
        int max_chars = (int)(btn_w - 8) / 8; /* 8px per char */
        if (max_chars < 1)
          max_chars = 1;
        char trunc[32];
        int ti = 0;
        while (w->title[ti] && ti < max_chars && ti < 31) {
          trunc[ti] = w->title[ti];
          ti++;
        }
        /* Add ellipsis if truncated */
        if (w->title[ti] && ti >= max_chars && ti >= 2) {
          trunc[ti - 1] = '.';
          trunc[ti - 2] = '.';
        }
        trunc[ti] = '\0';
        gfx_draw_text((int16_t)(btn_x + 4), (int16_t)(TASKBAR_Y + 6), trunc,
                      COLOR_TEXT_LIGHT);
      }

      btn_x = (int16_t)(btn_x + (int16_t)btn_w + 2);
    }
  }

  /* Clock display (right-aligned) */
  {
    uint32_t now_ms = timer_get_uptime_ms();
    if (clock_last_minute == 255 ||
        (uint32_t)(now_ms - clock_last_poll_ms) >= 1000u) {
      rtc_time_t time;
      rtc_date_t date;
      rtc_read_time(&time);
      rtc_read_date(&date);
      clock_last_poll_ms = now_ms;

      /* Rebuild strings only when minute changes. */
      if (time.minute != clock_last_minute) {
        if (rtc_validate_time(&time)) {
          format_time_12hr(&time, clock_time_str,
                           (int)sizeof(clock_time_str));
        } else {
          clock_time_str[0] = '-';
          clock_time_str[1] = '-';
          clock_time_str[2] = ':';
          clock_time_str[3] = '-';
          clock_time_str[4] = '-';
          clock_time_str[5] = '\0';
        }
        if (rtc_validate_date(&date)) {
          format_date_short(&date, clock_date_str,
                            (int)sizeof(clock_date_str));
        } else {
          clock_date_str[0] = '\0';
        }
        clock_last_minute = time.minute;
      }
    }

    /* Calculate right-aligned position */
    uint16_t time_w = gfx_text_width(clock_time_str);
    uint16_t date_w = gfx_text_width(clock_date_str);
    uint16_t spacing = 8;
    uint16_t total_w = (uint16_t)(time_w + spacing + date_w);
    int16_t cx = (int16_t)(VGA_GFX_WIDTH - (int16_t)total_w - 4);

    gfx_draw_text(cx, (int16_t)(TASKBAR_Y + 6), clock_time_str,
                  COLOR_TEXT_LIGHT);
    if (clock_date_str[0]) {
      gfx_draw_text((int16_t)(cx + (int16_t)time_w + (int16_t)spacing),
                    (int16_t)(TASKBAR_Y + 6), clock_date_str, COLOR_TEXT_LIGHT);
    }

    /* Store hitbox for click detection */
    clock_hitbox_x = cx;
    clock_hitbox_width = total_w;
  }
}

void desktop_draw_icons(void) {
  if (!workspace_base_drawn_with_icons) {
    gfx2d_icons_draw_all();
  }
  desktop_draw_icon_hover_fx();
}

int desktop_hit_test_taskbar(int16_t mx, int16_t my) {
  if (my < TASKBAR_Y + 2 || my >= TASKBAR_Y + TASKBAR_HEIGHT)
    return -1;

  int16_t btn_x = TASKBAR_BTN_START;
  int16_t btn_limit =
      (int16_t)(clock_hitbox_x > 0 ? clock_hitbox_x - 4 : VGA_GFX_WIDTH - 60);

  int wc_live = gui_window_count();
  for (int wi = 0; wi < wc_live && btn_x < btn_limit; wi++) {
    window_t *w = gui_get_window_by_index(wi);
    if (!w)
      continue;
    if (!(w->flags & WINDOW_FLAG_VISIBLE))
      continue;

    uint16_t btn_w = gfx_text_width(w->title);
    if (btn_w < 40)
      btn_w = 40;
    btn_w = (uint16_t)(btn_w + 8);
    if (btn_w > TASKBAR_BTN_MAX_W)
      btn_w = TASKBAR_BTN_MAX_W;

    if (btn_x + (int16_t)btn_w > btn_limit) {
      btn_w = (uint16_t)(btn_limit - btn_x);
      if (btn_w < 20)
        break;
    }

    if (mx >= btn_x && mx < btn_x + (int16_t)btn_w) {
      return (int)w->id;
    }
    btn_x = (int16_t)(btn_x + (int16_t)btn_w + 2);
  }
  return -1;
}

void desktop_toggle_calendar(void) {
  if (cal_state.visible) {
    cal_state.visible = false;
  } else {
    /* Initialize view to current date */
    rtc_date_t date;
    rtc_read_date(&date);
    cal_state.view_month = (int)date.month;
    cal_state.view_year = (int)date.year;
    cal_state.today_day = (int)date.day;
    cal_state.today_month = (int)date.month;
    cal_state.today_year = (int)date.year;
    cal_state.visible = true;

    /* Discover notes already persisted to FAT16 */
    calendar_scan_notes(&cal_state);
  }
}

void desktop_close_calendar(void) { cal_state.visible = false; }

bool desktop_calendar_visible(void) { return cal_state.visible; }

/**
 * desktop_draw_calendar - Draw the calendar popup centered on screen
 *
 * Layout:
 *   ┌─────────────────────────────────────┐
 *   │  <  February 2026  >    2:35:47 PM  │
 *   ├─────────────────────────────────────┤
 *   │  Su Mo Tu We Th Fr Sa               │
 *   │                     1               │
 *   │   2  3  4  5 [6] 7  8               │
 *   │   ...                               │
 *   └─────────────────────────────────────┘
 */
static void desktop_draw_calendar(void) {
  if (!cal_state.visible)
    return;

  int16_t cx = (int16_t)((VGA_GFX_WIDTH - CALENDAR_WIDTH) / 2);
  int16_t cy = (int16_t)((TASKBAR_Y - CALENDAR_HEIGHT) / 2);

  /* Background */
  gfx_fill_rect(cx, cy, CALENDAR_WIDTH, CALENDAR_HEIGHT, COLOR_WINDOW_BG);
  gfx_draw_rect(cx, cy, CALENDAR_WIDTH, CALENDAR_HEIGHT, COLOR_BORDER);

  /* Close button "x" - top-right corner */
  {
    int16_t close_x = (int16_t)(cx + CALENDAR_WIDTH - 20);
    int16_t close_y = (int16_t)(cy + 2);
    gfx_fill_rect(close_x, close_y, 18, 18, COLOR_CLOSE_BG);
    gfx_draw_rect(close_x, close_y, 18, 18, COLOR_BORDER);
    gfx_draw_text((int16_t)(close_x + 5), (int16_t)(close_y + 5), "x", COLOR_TEXT_LIGHT);
  }

  /* Header: ◄ Month Year ► (below close button) */
  int16_t hdr_y = (int16_t)(cy + 20);

  /* Left arrow  "<" */
  gfx_draw_text((int16_t)(cx + 6), hdr_y, "<", COLOR_TEXT);

  /* Month/Year centered in header */
  {
    char hdr_buf[32];
    int pos = 0;
    const char *mname = get_month_full((uint8_t)cal_state.view_month);
    while (mname[pos] && pos < 20) {
      hdr_buf[pos] = mname[pos];
      pos++;
    }
    hdr_buf[pos++] = ' ';
    /* Year */
    {
      char ybuf[8];
      int yi = 0;
      int yr = cal_state.view_year;
      char tmp[8];
      int tl = 0;
      if (yr == 0) {
        tmp[tl++] = '0';
      } else {
        while (yr > 0) {
          tmp[tl++] = (char)('0' + (yr % 10));
          yr /= 10;
        }
      }
      while (tl > 0 && yi < 7)
        ybuf[yi++] = tmp[--tl];
      ybuf[yi] = '\0';
      int j = 0;
      while (ybuf[j] && pos < 30)
        hdr_buf[pos++] = ybuf[j++];
    }
    hdr_buf[pos] = '\0';

    uint16_t tw = gfx_text_width(hdr_buf);
    int16_t tx = (int16_t)(cx + (CALENDAR_WIDTH - (int16_t)tw) / 2);
    gfx_draw_text(tx, hdr_y, hdr_buf, COLOR_TEXT);
  }

  /* Right arrow ">" */
  gfx_draw_text((int16_t)(cx + CALENDAR_WIDTH - 14), hdr_y, ">", COLOR_TEXT);

  /* Time with seconds (right side of header) */
  {
    rtc_time_t t;
    rtc_read_time(&t);
    char tbuf[16];
    format_time_12hr_sec(&t, tbuf, (int)sizeof(tbuf));
    uint16_t ttw = gfx_text_width(tbuf);
    gfx_draw_text((int16_t)(cx + CALENDAR_WIDTH - (int16_t)ttw - 4),
                  (int16_t)(hdr_y + 12), tbuf, COLOR_TEXT);
  }

  /* Separator line */
  int16_t sep_y = (int16_t)(cy + 44);
  gfx_draw_hline(cx, sep_y, CALENDAR_WIDTH, COLOR_BORDER);

  /* Full date line */
  {
    rtc_date_t d;
    rtc_read_date(&d);
    char full_date[48];
    format_date_full(&d, full_date, (int)sizeof(full_date));
    uint16_t fdw = gfx_text_width(full_date);
    int16_t fdx = (int16_t)(cx + (CALENDAR_WIDTH - (int16_t)fdw) / 2);
    gfx_draw_text(fdx, (int16_t)(sep_y + 5), full_date, COLOR_TEXT);
  }

  /* Day headers: Su Mo Tu We Th Fr Sa */
  int16_t grid_x = (int16_t)(cx + (CALENDAR_WIDTH - 7 * 52) / 2);
  int16_t grid_y = (int16_t)(sep_y + 22);
  {
    static const char *day_hdrs[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    for (int i = 0; i < 7; i++) {
      int16_t dx = (int16_t)(grid_x + i * 52);
      gfx_draw_text(dx, grid_y, day_hdrs[i], COLOR_TEXT);
    }
  }

  /* Day grid */
  int first_dow = get_first_weekday(cal_state.view_month, cal_state.view_year);
  int days = get_days_in_month(cal_state.view_month, cal_state.view_year);
  bool is_current = (cal_state.view_month == cal_state.today_month &&
                     cal_state.view_year == cal_state.today_year);

  int16_t row_y = (int16_t)(grid_y + 18);
  int col = first_dow;

  for (int d = 1; d <= days; d++) {
    int16_t dx = (int16_t)(grid_x + col * 52);

    /* Highlight current day */
    if (is_current && d == cal_state.today_day) {
      gfx_fill_rect((int16_t)(dx - 1), (int16_t)(row_y - 1), 36, 14,
                    COLOR_TITLEBAR);
      /* Day number text in contrasting color */
      char dbuf[4];
      int dl = 0;
      if (d >= 10)
        dbuf[dl++] = (char)('0' + (d / 10));
      dbuf[dl++] = (char)('0' + (d % 10));
      dbuf[dl] = '\0';
      gfx_draw_text(dx, row_y, dbuf, COLOR_TEXT_LIGHT);
    } else {
      char dbuf[4];
      int dl = 0;
      if (d >= 10)
        dbuf[dl++] = (char)('0' + (d / 10));
      dbuf[dl++] = (char)('0' + (d % 10));
      dbuf[dl] = '\0';
      gfx_draw_text(dx, row_y, dbuf, COLOR_TEXT);
    }

    /* Draw a dot under the date if it has a *saved* note */
    {
      calendar_note_t *dn = calendar_has_note(&cal_state, cal_state.view_year,
                                              cal_state.view_month, d);
      if (dn && dn->saved) {
        int16_t dot_x = (int16_t)(dx + 5);
        int16_t dot_y = (int16_t)(row_y + 9);
        gfx_fill_rect(dot_x, dot_y, 3, 3, COLOR_CLOSE_BG);
      }
    }

    col++;
    if (col >= 7) {
      col = 0;
      row_y = (int16_t)(row_y + 20);
    }
  }
}

/**
 * calendar_hit_test_day - Determine which day (1-31) was clicked
 *
 * @param mx, my: Absolute screen coordinates
 * @return: day number (1-31) or 0 if no day was hit
 */
static int calendar_hit_test_day(int16_t mx, int16_t my) {
  int16_t cx = (int16_t)((VGA_GFX_WIDTH - CALENDAR_WIDTH) / 2);
  int16_t cy = (int16_t)((TASKBAR_Y - CALENDAR_HEIGHT) / 2);
  int16_t sep_y = (int16_t)(cy + 44);
  int16_t grid_x = (int16_t)(cx + (CALENDAR_WIDTH - 7 * 52) / 2);
  int16_t grid_y = (int16_t)(sep_y + 22);
  int16_t row_y = (int16_t)(grid_y + 18);

  int first_dow = get_first_weekday(cal_state.view_month, cal_state.view_year);
  int days_in = get_days_in_month(cal_state.view_month, cal_state.view_year);
  int col = first_dow;

  for (int d = 1; d <= days_in; d++) {
    int16_t dx = (int16_t)(grid_x + col * 52);

    /* Each day cell is ~36px wide, ~20px tall */
    if (mx >= dx - 1 && mx < dx + 36 && my >= row_y - 1 && my < row_y + 19) {
      return d;
    }

    col++;
    if (col >= 7) {
      col = 0;
      row_y = (int16_t)(row_y + 20);
    }
  }
  return 0;
}

/**
 * calendar_handle_click - Handle a left-click inside the calendar popup
 *
 * Left-clicking a date creates/opens a note for that date in Notepad.
 *
 * @param mx, my: Absolute screen coordinates of the click
 * @return: true if click was consumed by the calendar
 */
static bool calendar_handle_click(int16_t mx, int16_t my) {
  if (!cal_state.visible)
    return false;

  int16_t cx = (int16_t)((VGA_GFX_WIDTH - CALENDAR_WIDTH) / 2);
  int16_t cy = (int16_t)((TASKBAR_Y - CALENDAR_HEIGHT) / 2);

  /* Check if click is inside the calendar */
  if (mx < cx || mx >= cx + CALENDAR_WIDTH || my < cy ||
      my >= cy + CALENDAR_HEIGHT) {
    /* Click outside - close calendar */
    cal_state.visible = false;
    return true; /* consumed: prevents click-through */
  }

  /* Check header area for navigation arrows */
  int16_t hdr_y = (int16_t)(cy + 20);

  /* Close button (top-right corner, above nav arrows) */
  {
    int16_t close_x = (int16_t)(cx + CALENDAR_WIDTH - 20);
    int16_t close_y = (int16_t)(cy + 2);
    if (mx >= close_x && mx < close_x + 18 &&
        my >= close_y && my < close_y + 18) {
      cal_state.visible = false;
      return true;
    }
  }

  if (my >= hdr_y && my < hdr_y + 12) {
    /* Left arrow area */
    if (mx >= cx + 2 && mx < cx + 20) {
      calendar_prev_month(&cal_state);
      return true;
    }
    /* Right arrow area */
    if (mx >= cx + CALENDAR_WIDTH - 20 && mx < cx + CALENDAR_WIDTH - 2) {
      calendar_next_month(&cal_state);
      return true;
    }
  }

  /* Check if a day cell was clicked */
  int hit_day = calendar_hit_test_day(mx, my);
  if (hit_day > 0) {
    /* Create or open a note for this date */
    calendar_note_t *note = calendar_has_note(&cal_state, cal_state.view_year,
                                              cal_state.view_month, hit_day);

    if (!note) {
      /* Create a new note file */
      note = calendar_create_note(&cal_state, cal_state.view_year,
                                  cal_state.view_month, hit_day);
    }

    if (note) {
      /* Build full persistent VFS path: /home/<persist> */
      char persist_path[128];
      int pp = 0;
      const char *pfx = "/home/";
      while (*pfx)
        persist_path[pp++] = *pfx++;
      int pk = 0;
      while (note->persist[pk] && pp < 127)
        persist_path[pp++] = note->persist[pk++];
      persist_path[pp] = '\0';

      /* Open from ramfs temp, save to FAT16 persistent */
      desktop_notepad_launch_with_file(note->path, persist_path);

      /* Close calendar after opening note so clicks go to notepad */
      cal_state.visible = false;
    }
    return true;
  }

  /* Clicked inside calendar but not on a day or arrows */
  return true;
}

/**
 * calendar_handle_right_click - Handle a right-click inside the calendar popup
 *
 * Right-clicking a date with a note deletes the note.
 *
 * @param mx, my: Absolute screen coordinates of the click
 * @return: true if click was consumed by the calendar
 */
static bool calendar_handle_right_click(int16_t mx, int16_t my) {
  if (!cal_state.visible)
    return false;

  int16_t cx = (int16_t)((VGA_GFX_WIDTH - CALENDAR_WIDTH) / 2);
  int16_t cy = (int16_t)((TASKBAR_Y - CALENDAR_HEIGHT) / 2);

  /* Outside calendar - ignore */
  if (mx < cx || mx >= cx + CALENDAR_WIDTH || my < cy ||
      my >= cy + CALENDAR_HEIGHT) {
    return false;
  }

  /* Check if a day cell was right-clicked */
  int hit_day = calendar_hit_test_day(mx, my);
  if (hit_day > 0) {
    /* Delete the note for this date if one exists */
    calendar_delete_note(&cal_state, cal_state.view_year, cal_state.view_month,
                         hit_day);
    return true;
  }

  return true; /* consumed even if no day hit (inside calendar) */
}

void desktop_redraw_cycle(void) {
  bool needs_redraw = false;
  bool mouse_only = false; /* true if only mouse position changed */
  static bool cycle_has_first_render = false;

  /* Process mouse */
  if (mouse.updated) {
    mouse.updated = false;
    mouse_only = true; /* assume cursor-only until we see scroll/key */

    if (mouse.scroll_z != 0) {
      if (!shell_jit_program_is_running()) {
        mouse.scroll_z = 0;
      }
      needs_redraw = true;
      mouse_only = false;
    }

    uint8_t btn = mouse.buttons;
    uint8_t prev = mouse.prev_buttons;
    (void)btn;
    (void)prev;
    /* Don't process clicks during blocking command */
  }

  /* Process keyboard events (critical for JIT programs like ed) */
  {
    key_event_t event;
    while (keyboard_read_event(&event)) {
      /* Escape closes calendar popup */
      if (event.scancode == 0x01 && event.pressed && cal_state.visible) {
        cal_state.visible = false;
        needs_redraw = true;
        mouse_only = false;
        continue;
      }
      {
        window_t *focused = gui_get_focused_window();
        if (focused && strcmp(focused->title, "Terminal") == 0) {
          terminal_handle_key(event.scancode, event.character);
          needs_redraw = true;
          mouse_only = false;
          continue;
        }
      }
      if (shell_jit_program_is_running() && !gui_get_focused_window()) {
        shell_gui_handle_key(event.scancode, event.character);
        needs_redraw = true;
        mouse_only = false;
        continue;
      }
      if (shell_jit_program_is_running()) {
        gui_handle_key(event.scancode, event.character);
        needs_redraw = true;
        mouse_only = false;
        continue;
      }
      gui_handle_key(event.scancode, event.character);
      needs_redraw = true;
      mouse_only = false;
    }
  }

  bool any_dirty = gui_any_dirty();

  /* Fast path: cursor moved but nothing else changed - just update cursor.
   * Use mouse_mark_cursor_dirty() to only copy the ~10x10 px cursor region
   * to VRAM instead of the full 1.2MB back buffer. */
  if (mouse_only && !needs_redraw && !any_dirty && cycle_has_first_render) {
    mouse_mark_cursor_dirty();   /* mark old+new cursor rect before moving */
    mouse_restore_under_cursor();
    mouse_save_under_cursor();
    mouse_draw_cursor();
    vga_flip();
    return;
  }

  /* Redraw if needed */
  if (needs_redraw || mouse_only || any_dirty) {
    /* Only increment anim tick for actual content redraws, and throttle it */
    if (needs_redraw || any_dirty) {
      static uint32_t anim_frame = 0;
      if (++anim_frame >= 3u) { /* update animation every 3 redraws */
        anim_frame = 0;
        desktop_anim_tick++;
      }
    }
    desktop_draw_background();
    desktop_draw_icons();
    /* Only mark windows dirty when background was repainted (not cursor-only) */
    if (needs_redraw || any_dirty)
      gui_mark_all_dirty();
    gui_draw_all_windows(true); /* background always repainted in this loop */
    desktop_draw_taskbar();
    desktop_draw_calendar();

    mouse_save_under_cursor();
    mouse_draw_cursor();

    vga_mark_dirty_full();
    vga_flip();
    cycle_has_first_render = true;
  }
}

/* Minimized fullscreen app support
 * When a fullscreen JIT program calls gfx2d_minimize(), we need to
 * run the desktop event loop with a taskbar button for the app.
 * Since JIT programs execute synchronously (blocking the desktop
 * loop), this function takes over as the temporary event loop until
 * the user clicks the taskbar button to restore the app.
 */

void desktop_run_minimized_loop(const char *app_name) {
  serial_printf("[desktop] minimized app: %s\n", app_name);

  bool restore_requested = false;
  bool needs_redraw = true;
  uint8_t prev_btns = mouse.buttons;

  while (!restore_requested) {
    bool repainted_workspace = false;
    /* Check if the minimized program was killed */
    if (shell_jit_program_was_killed()) {
      serial_printf("[desktop] minimized app killed: %s\n", app_name);
      break;
    }

    /* Recalculate all minimized JIT button positions each frame
     * (stack depth can change if a nested JIT program launches/exits) */
    int16_t jit_btn_x = TASKBAR_BTN_START;
    /* Skip past GUI window buttons */
    int wc_live = gui_window_count();
    for (int wi = 0; wi < wc_live; wi++) {
      window_t *w = gui_get_window_by_index(wi);
      if (!w || !(w->flags & WINDOW_FLAG_VISIBLE)) continue;
      uint16_t bw = gfx_text_width(w->title);
      if (bw < 40) bw = 40;
      bw = (uint16_t)(bw + 8);
      if (bw > TASKBAR_BTN_MAX_W) bw = TASKBAR_BTN_MAX_W;
      jit_btn_x = (int16_t)(jit_btn_x + (int16_t)bw + 2);
    }

    /* Build list of minimized JIT buttons: suspended stack + current app */
    int n_suspended = shell_jit_suspended_count();
    /* We only care about stack entries below us (indices 0..n_suspended-1
     * are older minimized apps; the topmost entry is us but we pass
     * ourselves as app_name). */
    #define MAX_JIT_BTNS 8
    struct {
      int16_t  x;
      uint16_t w;
      const char *name;
    } jit_btns[MAX_JIT_BTNS];
    int n_btns = 0;

    /* Add suspended (deeper) JIT apps */
    for (int si = 0; si < n_suspended && n_btns < MAX_JIT_BTNS; si++) {
      const char *sname = shell_jit_suspended_get_name(si);
      if (sname[0] == '\0') continue;
      uint16_t bw = gfx_text_width(sname);
      if (bw < 40) bw = 40;
      bw = (uint16_t)(bw + 8);
      if (bw > TASKBAR_BTN_MAX_W) bw = TASKBAR_BTN_MAX_W;
      jit_btns[n_btns].x = jit_btn_x;
      jit_btns[n_btns].w = bw;
      jit_btns[n_btns].name = sname;
      n_btns++;
      jit_btn_x = (int16_t)(jit_btn_x + (int16_t)bw + 2);
    }

    /* Add the current minimized app (the one we can restore) */
    int current_btn_idx = -1;
    if (n_btns < MAX_JIT_BTNS) {
      uint16_t bw = gfx_text_width(app_name);
      if (bw < 40) bw = 40;
      bw = (uint16_t)(bw + 8);
      if (bw > TASKBAR_BTN_MAX_W) bw = TASKBAR_BTN_MAX_W;
      current_btn_idx = n_btns;
      jit_btns[n_btns].x = jit_btn_x;
      jit_btns[n_btns].w = bw;
      jit_btns[n_btns].name = app_name;
      n_btns++;
    }

    /* Process mouse */
    if (mouse.updated) {
      mouse.updated = false;
      needs_redraw = true;

      /* Handle scroll wheel for focused windows while minimized */
      if (mouse.scroll_z != 0) {
        if (!shell_jit_program_is_running()) {
          mouse.scroll_z = 0;
        }
      }

      uint8_t btn = mouse.buttons;
      bool pressed = (btn & 0x01) && !(prev_btns & 0x01);
      bool right_pressed = (btn & 0x02) && !(prev_btns & 0x02);

      bool drag_passthrough = false;
      if (gui_is_dragging_any()) {
        gui_handle_mouse(mouse.x, mouse.y, btn, prev_btns);
        drag_passthrough = true;
      }

      if (!drag_passthrough && right_pressed && !cal_state.visible &&
          mouse.y < TASKBAR_Y &&
          gui_hit_test_window(mouse.x, mouse.y) < 0) {
        int icon_menu_target = gfx2d_icon_at_pos(mouse.x, mouse.y);
        if (icon_menu_target >= 0) {
          mouse_restore_under_cursor();
          const char *icon_menu[] = {"Open", "Info"};
          int pick = gfx2d_popup_menu(mouse.x, mouse.y, icon_menu, 2);
          if (pick == 0) {
            desktop_launch_icon_handle(icon_menu_target);
            needs_redraw = true;
          } else if (pick == 1) {
            desktop_show_icon_info(icon_menu_target);
            needs_redraw = true;
          }
        }
      }

      /* Right-click on calendar: delete note */
      if (!drag_passthrough && right_pressed && cal_state.visible) {
        calendar_handle_right_click(mouse.x, mouse.y);
      }

      if (!drag_passthrough && pressed) {
        /* Check for click on the current app's taskbar button (restore). */
        if (mouse.y >= TASKBAR_Y && current_btn_idx >= 0 &&
            mouse.x >= jit_btns[current_btn_idx].x &&
            mouse.x < jit_btns[current_btn_idx].x +
                       (int16_t)jit_btns[current_btn_idx].w) {
          restore_requested = true;
          needs_redraw = true;
        } else if (mouse.y >= TASKBAR_Y &&
                   mouse.x >= clock_hitbox_x &&
                   mouse.x < clock_hitbox_x + (int16_t)clock_hitbox_width) {
          /* Check clock hitbox */
          desktop_toggle_calendar();
          needs_redraw = true;
        } else if (pressed && mouse.y >= TASKBAR_Y) {
          /* Handle other taskbar window buttons - no window-hit guard,
           * the taskbar strip belongs to the taskbar regardless of whether
           * a window's bounds extend into it. */
          int tb_id = desktop_hit_test_taskbar(mouse.x, mouse.y);
          if (tb_id >= 0) {
            gui_set_focus(tb_id);
            needs_redraw = true;
          }
          /* Close calendar if clicking elsewhere on taskbar */
          if (cal_state.visible &&
              !(mouse.x >= clock_hitbox_x &&
                mouse.x < clock_hitbox_x + (int16_t)clock_hitbox_width)) {
            cal_state.visible = false;
          }
        } else if (pressed && cal_state.visible) {
          /* Calendar popup click handling */
          calendar_handle_click(mouse.x, mouse.y);
        } else if (pressed && gui_hit_test_window(mouse.x, mouse.y) < 0) {
          /* Icon clicks */
          int gfx_icon = gfx2d_icon_at_pos(mouse.x, mouse.y);
          if (gfx_icon >= 0) {
            desktop_launch_icon_handle(gfx_icon);
            needs_redraw = true;
          }
        }
      }

      /* Forward mouse to window manager for dragging/focus - but NOT for
       * clicks in the taskbar strip; those were already handled above and
       * must not also interact with windows whose bodies extend there. */
      if (!drag_passthrough && (mouse.y < TASKBAR_Y || !(btn & MOUSE_LEFT))) {
        gui_handle_mouse(mouse.x, mouse.y, btn, prev_btns);
      }
      prev_btns = btn;
    }

    /* Process keyboard */
    {
      key_event_t event;
      while (keyboard_read_event(&event)) {
        if (desktop_handle_global_shortcuts(&event, NULL)) {
          needs_redraw = true;
          continue;
        }
        if (event.scancode == 0x01 && event.pressed && cal_state.visible) {
          cal_state.visible = false;
          needs_redraw = true;
          continue;
        }
        {
          window_t *focused = gui_get_focused_window();
          if (focused && strcmp(focused->title, "Terminal") == 0) {
            terminal_handle_key(event.scancode, event.character);
            needs_redraw = true;
            continue;
          }
        }
        if (shell_jit_program_is_running() && !gui_get_focused_window()) {
          shell_gui_handle_key(event.scancode, event.character);
          needs_redraw = true;
          continue;
        }
        if (shell_jit_program_is_running()) {
          gui_handle_key(event.scancode, event.character);
          needs_redraw = true;
          continue;
        }
        gui_handle_key(event.scancode, event.character);
        needs_redraw = true;
      }
    }

    /* Render */
    if (needs_redraw || gui_any_dirty()) {
      desktop_anim_tick++;
      desktop_draw_background();
      desktop_draw_icons();
      repainted_workspace = true;
      if (repainted_workspace)
        gui_mark_all_dirty();
      gui_draw_all_windows(true); /* background always repainted in this loop */

      /* Draw the standard taskbar */
      desktop_draw_taskbar();

      desktop_draw_hover_tooltip();

      /* Draw minimized JIT app buttons on the taskbar */
      for (int bi = 0; bi < n_btns; bi++) {
        uint32_t bg = COLOR_TASKBAR;
        gfx_fill_rect(jit_btns[bi].x, (int16_t)(TASKBAR_Y + 2),
                       jit_btns[bi].w, (uint16_t)(TASKBAR_HEIGHT - 4), bg);
        gfx_draw_rect(jit_btns[bi].x, (int16_t)(TASKBAR_Y + 2),
                       jit_btns[bi].w, (uint16_t)(TASKBAR_HEIGHT - 4),
                       COLOR_BORDER);

        /* Truncate title to fit button */
        int max_chars = (int)(jit_btns[bi].w - 8) / 8;
        if (max_chars < 1) max_chars = 1;
        char trunc[32];
        int ti = 0;
        while (jit_btns[bi].name[ti] && ti < max_chars && ti < 31) {
          trunc[ti] = jit_btns[bi].name[ti];
          ti++;
        }
        if (jit_btns[bi].name[ti] && ti >= max_chars && ti >= 2) {
          trunc[ti - 1] = '.';
          trunc[ti - 2] = '.';
        }
        trunc[ti] = '\0';
        gfx_draw_text((int16_t)(jit_btns[bi].x + 4),
                      (int16_t)(TASKBAR_Y + 6), trunc, COLOR_TEXT_LIGHT);
      }

        desktop_draw_calendar();

      mouse_save_under_cursor();
      mouse_draw_cursor();
      vga_mark_dirty_full();
      vga_flip();

      needs_redraw = false;
    }

    process_yield();
  }

  serial_printf("[desktop] restoring app: %s\n", app_name);
}

void desktop_run(void) {
  bool needs_redraw = true;
  bool force_full_repaint = false;
  static int last_hover_icon = -2;
  bool mouse_buttons_changed = false;
  bool mouse_scroll_activity = false;
  enum {
    CURSOR_W = 8,
    CURSOR_H = 10,
    CURSOR_PAD = 1
  };

  uint32_t fps_frames = 0;
  uint32_t fps_last_ms = 0;

  while (1) {
    /* FPS counter: report actual vga_flip() calls per second every 5s */
    {
      uint32_t now = timer_get_uptime_ms();
      if (fps_last_ms == 0U) {
        fps_last_ms = now; /* start first measurement window */
      } else if (now - fps_last_ms >= 5000U) {
        uint32_t elapsed = now - fps_last_ms;
        uint32_t fps = elapsed ? (fps_frames * 1000U) / elapsed : 0U;
        if (fps > 0U)
          serial_printf("[fps] %u fps\n", fps);
        fps_frames = 0;
        fps_last_ms = now;
      }
    }

    bool mouse_activity = false;
    mouse_buttons_changed = false;
    mouse_scroll_activity = false;

    /* Skip ALL desktop processing if a fullscreen app is running */
    if (gfx2d_fullscreen_active()) {
      __asm__ volatile("hlt");
      continue;
    }

    /* Process mouse */
    if (mouse.updated) {
      mouse.updated = false;
      mouse_activity = true;
      mouse_buttons_changed = (mouse.buttons != mouse.prev_buttons);
      mouse_scroll_activity = (mouse.scroll_z != 0);

      /* Handle scroll wheel – consume accumulated delta */
      if (mouse.scroll_z != 0) {
        /* Each scroll notch is ±1 in scroll_z.
         * Multiply by 5 lines per notch for snappy scrolling.
         * Positive scroll_z = scroll up (show older content). */
        if (!shell_jit_program_is_running()) {
          mouse.scroll_z = 0;
        }
      }

      /* Track prev in the desktop loop, not from mouse.prev_buttons.
       * mouse.prev_buttons is overwritten by every IRQ packet, so if
       * multiple packets (e.g. release + move) arrive between loop
       * iterations, the release transition is silently lost. */
      static uint8_t last_buttons = 0;
      uint8_t btn = mouse.buttons;
      uint8_t prev = last_buttons;
      last_buttons = btn;
      bool pressed = (btn & 0x01) && !(prev & 0x01);
      bool right_pressed = (btn & 0x02) && !(prev & 0x02);
      bool drag_passthrough = false;


      if (btn != prev)
        needs_redraw = true;

      if (gui_is_dragging_any()) {
        gui_handle_mouse(mouse.x, mouse.y, btn, prev);
        drag_passthrough = true;
      }

      if (!drag_passthrough && right_pressed && cal_state.visible) {
        /* Right-click on calendar: delete note */
        calendar_handle_right_click(mouse.x, mouse.y);
      } else if (!drag_passthrough && right_pressed && !cal_state.visible &&
                 mouse.y < TASKBAR_Y &&
                 gui_hit_test_window(mouse.x, mouse.y) < 0) {
        int icon_menu_target = gfx2d_icon_at_pos(mouse.x, mouse.y);
        mouse_restore_under_cursor();
        if (icon_menu_target >= 0) {
          const char *icon_menu[] = {"Open", "Info"};
          int pick = gfx2d_popup_menu(mouse.x, mouse.y, icon_menu, 2);
          if (pick == 0) {
            desktop_launch_icon_handle(icon_menu_target);
          } else if (pick == 1) {
            desktop_show_icon_info(icon_menu_target);
          }
        } else {
          const char *desktop_menu[] = {
              "Change Desktop Background"
          };
          int pick = gfx2d_popup_menu(mouse.x, mouse.y, desktop_menu, 1);
          if (pick == 0) {
            desktop_open_bg_settings_dialog();
          }
        }
        force_full_repaint = true;
        needs_redraw = true;
      }

      /* Check taskbar clicks first.  No window-hit guard: the taskbar is
       * always drawn on top, so any click at y >= TASKBAR_Y belongs to it
       * even if a window's bounding box extends into that strip. */
        if (!drag_passthrough && pressed && mouse.y >= TASKBAR_Y) {
        /* Check clock hitbox */
        if (mouse.x >= clock_hitbox_x &&
            mouse.x < clock_hitbox_x + (int16_t)clock_hitbox_width) {
          desktop_toggle_calendar();
          force_full_repaint = true;
          needs_redraw = true;
        } else {
          int tb_id = desktop_hit_test_taskbar(mouse.x, mouse.y);
          if (tb_id >= 0) {
            gui_set_focus(tb_id);
            force_full_repaint = true;
            needs_redraw = true;
          }
        }
        /* Close calendar if clicking elsewhere on taskbar */
        if (cal_state.visible &&
            !(mouse.x >= clock_hitbox_x &&
              mouse.x < clock_hitbox_x + (int16_t)clock_hitbox_width)) {
          cal_state.visible = false;
        }
      }
      /* Check calendar popup clicks */
      else if (!drag_passthrough && pressed && cal_state.visible) {
        /* Get calendar bounds to check if click was inside or outside */
        int16_t cx = (int16_t)((VGA_GFX_WIDTH - CALENDAR_WIDTH) / 2);
        int16_t cy = (int16_t)((TASKBAR_Y - CALENDAR_HEIGHT) / 2);
        bool click_inside = (mouse.x >= cx && mouse.x < cx + CALENDAR_WIDTH &&
                             mouse.y >= cy && mouse.y < cy + CALENDAR_HEIGHT);

        calendar_handle_click(mouse.x, mouse.y);

        /* If click was outside calendar, pass through to window manager */
        if (!click_inside) {
          gui_handle_mouse(mouse.x, mouse.y, btn, prev);
        }
      }
      /* Check icon clicks */
      else if (!drag_passthrough && pressed &&
           gui_hit_test_window(mouse.x, mouse.y) < 0) {
        /* Check gfx2d desktop icons (unified system) */
        int gfx_icon = gfx2d_icon_at_pos(mouse.x, mouse.y);
        if (gfx_icon >= 0) {
          desktop_launch_icon_handle(gfx_icon);
          /* App launch/return may leave stale framebuffer contents. */
          force_full_repaint = true;
          needs_redraw = true;
        }
      }
      /* Forward to GUI window manager */
      else if (!drag_passthrough) {
        gui_handle_mouse(mouse.x, mouse.y, btn, prev);
      }

      {
        int hover_icon = -1;
        if (!cal_state.visible && mouse.y < TASKBAR_Y &&
            !(mouse.buttons & (MOUSE_LEFT | MOUSE_RIGHT)) &&
            gui_hit_test_window(mouse.x, mouse.y) < 0) {
          hover_icon = gfx2d_icon_at_pos(mouse.x, mouse.y);
        }

        /* Tooltip is anchored to icon position. Repaint only when
         * hover target changes (enter/leave/switch icon). */
        if (hover_icon != last_hover_icon) {
          last_hover_icon = hover_icon;
          force_full_repaint = true;
          needs_redraw = true;
        }
      }

    }

    /* Process keyboard */
    {
      key_event_t event;
      while (keyboard_read_event(&event)) {
        if (desktop_handle_global_shortcuts(&event, &force_full_repaint)) {
          needs_redraw = true;
          continue;
        }
        /* Escape closes calendar popup */
        if (event.scancode == 0x01 && event.pressed && cal_state.visible) {
          cal_state.visible = false;
          needs_redraw = true;
          continue;
        }
        {
          window_t *focused = gui_get_focused_window();
          if (focused && strcmp(focused->title, "Terminal") == 0) {
            terminal_handle_key(event.scancode, event.character);
            needs_redraw = true;
            continue;
          }
        }
        if (shell_jit_program_is_running() && !gui_get_focused_window()) {
          shell_gui_handle_key(event.scancode, event.character);
          needs_redraw = true;
          continue;
        }
        if (shell_jit_program_is_running()) {
          gui_handle_key(event.scancode, event.character);
          needs_redraw = true;
          continue;
        }
        gui_handle_key(event.scancode, event.character);
        needs_redraw = true;
      }
    }

    /* Redraw */
    /* Redraw */
    /* Skip desktop rendering if a fullscreen gfx2d app is running */
    if (gfx2d_fullscreen_active()) {
      /* Also skip input processing so we don't steal clicks/keys */
      __asm__ volatile("hlt");
      continue;
    }

    bool any_dirty = gui_any_dirty();
    bool layout_changed = gui_layout_changed();
    static bool has_first_render = false;
    static bool was_dragging_any = false;
    static int post_drag_settle_frames = 0;
    bool repainted_workspace = false;
    bool cal_visibility_changed = (cal_state.visible != cal_prev_visible);
    cal_prev_visible = cal_state.visible;
    bool dragging_now = gui_is_dragging_any();
    int hover_wid = gui_hit_test_window(mouse.x, mouse.y);
    bool hover_window = (hover_wid >= 0);

    if (was_dragging_any && !dragging_now) {
      post_drag_settle_frames = 4;
      force_full_repaint = true;
      needs_redraw = true;
    }
    was_dragging_any = dragging_now;

    if (post_drag_settle_frames > 0) {
      force_full_repaint = true;
      needs_redraw = true;
    }

    /* Prefer compositor correctness when multiple windows are active:
     * full repaint avoids transient z-order artifacts and flashing from
     * mixed partial redraw paths while interacting with stacked windows. */
    if (gui_window_count() > 1 &&
        (mouse_buttons_changed || mouse_scroll_activity || any_dirty ||
         layout_changed || needs_redraw)) {
      force_full_repaint = true;
    }

    /* In overlapped-window scenarios, cursor-only restore/save can replay
     * stale pixels from a previous composition and smear the top window. */
    if (gui_window_count() > 1 && mouse_activity && hover_window) {
      needs_redraw = true;
    }

    if (cal_state.visible && mouse_activity && !mouse_buttons_changed &&
        !mouse_scroll_activity && !needs_redraw && !force_full_repaint &&
        !layout_changed && !cal_visibility_changed) {
      needs_redraw = true;
    }

    /* Rate-limit animated background to ~60 fps: only force a full redraw
     * when enough time has elapsed since the last vga_flip().  Between frames
     * the cursor-only fast path below still handles mouse movement smoothly. */
    if (desktop_bg_mode == DESKTOP_BG_ANIM && !force_full_repaint &&
        !layout_changed && !cal_visibility_changed && vga_flip_ready()) {
      needs_redraw = true;
    }

    /* If only mouse moved and nothing else changed, do a cursor-only
     * backbuffer refresh to keep display/backbuffer fully in sync.
     * This must work over windows as well as desktop; otherwise cursor
     * movement appears sticky when hovering stacked windows. */
    if (mouse_activity && !needs_redraw && !force_full_repaint && !any_dirty &&
      !layout_changed && !mouse_buttons_changed && !mouse_scroll_activity &&
      !cal_state.visible && !cal_visibility_changed &&
      post_drag_settle_frames == 0 &&
        has_first_render) {
      mouse_mark_cursor_dirty();   /* mark old+new cursor rect before moving */
      mouse_restore_under_cursor();
      mouse_save_under_cursor();
      mouse_draw_cursor();
      vga_flip();
      fps_frames++;
      needs_redraw = false;
      continue;
    }
    if (needs_redraw || any_dirty) {
      if (mouse_activity)
        mouse_mark_cursor_dirty();

      bool draw_taskbar_now = false;
      {
        uint32_t now_ms = timer_get_uptime_ms();
        int wc_now = gui_window_count();
        int focus_now = -1;
        window_t *fw = gui_get_focused_window();
        if (fw)
          focus_now = (int)fw->id;

        if (!has_first_render || force_full_repaint || layout_changed ||
          cal_visibility_changed ||
            wc_now != taskbar_last_window_count ||
            focus_now != taskbar_last_focus_id ||
            (uint32_t)(now_ms - taskbar_last_draw_ms) >= 1000u) {
          draw_taskbar_now = true;
        }
      }

        /* Full scene render.
         * If fast path ran since last full render, back_buffer has a stale
         * cursor at the old position (fast path never touches back_buffer).
         * Force a full background repaint to cover it. */
        if (force_full_repaint || layout_changed || cal_visibility_changed) {
          force_full_repaint = false;
          if (layout_changed && gui_is_dragging_any() &&
              !cal_visibility_changed) {
            int16_t rx;
            int16_t ry;
            uint16_t rw;
            uint16_t rh;
            if (gui_get_drag_invalidate_rect(&rx, &ry, &rw, &rh)) {
              desktop_anim_tick++;
              desktop_redraw_workspace_region(rx, ry, rw, rh);
              /* Mark all windows dirty so any window overlapping the
               * repainted region gets composited back on top.  Without
               * this, windows behind the dragged one are erased because
               * only the background is repainted but they never get
               * a redraw pass. */
              repainted_workspace = true;
            } else {
              desktop_anim_tick++;
              desktop_draw_background();
              desktop_draw_icons();
              repainted_workspace = true;
            }
            gui_clear_layout_changed();
            layout_changed = false;
          } else {
            desktop_anim_tick++;
            desktop_draw_background();
            desktop_draw_icons();
            repainted_workspace = true;
            if (layout_changed) {
              gui_clear_layout_changed();
              layout_changed = false;
            }
          }
        } else {
          if (desktop_bg_mode == DESKTOP_BG_ANIM) {
            /* Advance animation tick every 4 rendered frames */
            static uint32_t anim_main_frame = 0;
            static uint32_t last_anim_tick = 0xFFFFFFFFu;
            if (++anim_main_frame >= 4u) {
              anim_main_frame = 0;
              desktop_anim_tick++;
            }
            /* Only repaint background when the tick actually changed.
             * On the other 3 frames the back-buffer still has the correct
             * background — just restore the cursor area and skip the 640x456
             * pixel fill entirely. */
            if (desktop_anim_tick != last_anim_tick) {
              desktop_draw_background();
              desktop_draw_icons();
              repainted_workspace = true;
              last_anim_tick = desktop_anim_tick;
            } else {
              mouse_restore_under_cursor();
            }
          } else {
            mouse_restore_under_cursor();
          }
        }

        if (repainted_workspace)
          gui_mark_all_dirty();
        gui_draw_all_windows(repainted_workspace);
        /* Always redraw the taskbar after windows so it is never obscured.
         * Windows whose bodies extend below TASKBAR_Y would otherwise paint
         * over it on frames where draw_taskbar_now is false (e.g. when only
         * window content is dirty).  The taskbar strip is 640×24 px and
         * cheap to render. */
        {
          uint32_t now_ms = timer_get_uptime_ms();
          window_t *fw = gui_get_focused_window();
          desktop_draw_taskbar();
          if (draw_taskbar_now) {
            taskbar_last_draw_ms = now_ms;
            taskbar_last_window_count = gui_window_count();
            taskbar_last_focus_id = fw ? (int)fw->id : -1;
          }
        }
        if (cal_state.visible)
          desktop_draw_calendar();
        desktop_draw_hover_tooltip();
        mouse_save_under_cursor();
        mouse_draw_cursor();
        if (repainted_workspace) {
          vga_mark_dirty_full();
        } else {
          /* Only windows + taskbar changed: blit only those rows to VRAM */
          gui_mark_visible_rects();
          vga_mark_dirty(0, (int)TASKBAR_Y,
                         (int)VGA_GFX_WIDTH,
                         (int)VGA_GFX_HEIGHT - (int)TASKBAR_Y);
        }
        vga_flip();
        fps_frames++;
        has_first_render = true;
        if (post_drag_settle_frames > 0)
          post_drag_settle_frames--;

      needs_redraw = false;
    }

    /* Check for deferred reschedule (preemptive time slice) */
    kernel_check_reschedule();

    /* Yield CPU until next interrupt */
    __asm__ volatile("hlt");
  }
}
