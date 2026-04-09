/**
 * gui.c - Window manager and GUI API for cupid-os
 *
 * Provides window creation, rendering, dragging, focus management,
 * and input dispatch.  Windows are stored in a flat array ordered
 * by z-index (index 0 = back, highest = front / focused).
 */

#include "gui.h"
#include "simd.h"
#include "desktop.h"
#include "../drivers/mouse.h"
#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "gfx2d.h"
#include "gui_themes.h"
#include "graphics.h"
#include "memory.h"
#include "process.h"
#include "string.h"

static window_t windows[MAX_WINDOWS];
static int win_count = 0;
static uint32_t next_id = 1;

static drag_state_t drag = {false, false, -1, 0, 0, 0, 0, 0, 0};
static int last_draw_first_index = -1;

#define RESIZE_GRIP_SIZE 12

static bool layout_changed_flag = true; /* start true to force first render */

static void invalidate_window_full(window_t *win);

static int theme_channel(uint32_t color, int shift) {
  return (int)((color >> shift) & 0xFFu);
}

static uint32_t titlebar_text_color(uint32_t preferred,
                                    uint32_t start,
                                    uint32_t end) {
  int br = (theme_channel(start, 16) + theme_channel(end, 16)) / 2;
  int bg = (theme_channel(start, 8) + theme_channel(end, 8)) / 2;
  int bb = (theme_channel(start, 0) + theme_channel(end, 0)) / 2;
  int brightness = br * 30 + bg * 59 + bb * 11;

  (void)preferred;
  return brightness < 14000 ? 0x00FFFFFFu : 0x00000000u;
}

static bool window_is_drawable(const window_t *win) {
  return win && (win->flags & WINDOW_FLAG_VISIBLE) &&
         !(win->flags & WINDOW_FLAG_MINIMIZED);
}

static int top_drawable_index(void) {
  for (int i = win_count - 1; i >= 0; i--) {
    if (window_is_drawable(&windows[i]))
      return i;
  }
  return -1;
}

static void sync_focus_to_top_drawable(void) {
  int top_idx;

  for (int i = 0; i < win_count; i++) {
    windows[i].flags &= (uint8_t)~WINDOW_FLAG_FOCUSED;
  }

  top_idx = top_drawable_index();
  if (top_idx >= 0) {
    windows[top_idx].flags |= WINDOW_FLAG_FOCUSED;
    invalidate_window_full(&windows[top_idx]);
  }
}

void gui_init(void) {
  win_count = 0;
  next_id = 1;
  drag.dragging = false;
  drag.resizing = false;
  last_draw_first_index = -1;
  memset(windows, 0, sizeof(windows));
  KINFO("GUI initialized (max %d windows)", MAX_WINDOWS);
}

/* Find array index for a given window ID; returns -1 if not found */
static int find_index(int wid) {
  for (int i = 0; i < win_count; i++) {
    if ((int)windows[i].id == wid)
      return i;
  }
  return -1;
}

/* Copy a window_t */
static void win_copy(window_t *dst, const window_t *src) {
  memcpy(dst, src, sizeof(window_t));
}

static bool window_content_metrics(const window_t *win, int *cx, int *cy,
                                   int *cw, int *ch) {
  int tx;
  int ty;
  int tw;
  int th;

  if (!win)
    return false;

  tx = (int)win->x + 1;
  ty = (int)win->y + TITLEBAR_H + WINDOW_CONTENT_TOP_PAD;
  tw = (int)win->width - 2;
  th = (int)win->height - TITLEBAR_H - WINDOW_CONTENT_BORDER;

  if (tw <= 0 || th <= 0)
    return false;

  if (cx)
    *cx = tx;
  if (cy)
    *cy = ty;
  if (cw)
    *cw = tw;
  if (ch)
    *ch = th;
  return true;
}

static void clear_window_dirty_rect(window_t *win) {
  if (!win)
    return;
  win->dirty_x = 0;
  win->dirty_y = 0;
  win->dirty_w = 0;
  win->dirty_h = 0;
}

static void invalidate_window_rect_internal(window_t *win, int x, int y,
                                            int w, int h) {
  int ux0;
  int uy0;
  int ux1;
  int uy1;

  if (!win || w <= 0 || h <= 0)
    return;

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (w <= 0 || h <= 0)
    return;

  if (win->dirty_w == 0 || win->dirty_h == 0) {
    win->dirty_x = (int16_t)x;
    win->dirty_y = (int16_t)y;
    win->dirty_w = (uint16_t)w;
    win->dirty_h = (uint16_t)h;
  } else {
    ux0 = x < win->dirty_x ? x : win->dirty_x;
    uy0 = y < win->dirty_y ? y : win->dirty_y;
    ux1 = x + w;
    uy1 = y + h;
    if (ux1 < (int)win->dirty_x + (int)win->dirty_w)
      ux1 = (int)win->dirty_x + (int)win->dirty_w;
    if (uy1 < (int)win->dirty_y + (int)win->dirty_h)
      uy1 = (int)win->dirty_y + (int)win->dirty_h;
    win->dirty_x = (int16_t)ux0;
    win->dirty_y = (int16_t)uy0;
    win->dirty_w = (uint16_t)(ux1 - ux0);
    win->dirty_h = (uint16_t)(uy1 - uy0);
  }

  win->flags |= WINDOW_FLAG_DIRTY;
}

static void invalidate_window_full(window_t *win) {
  if (!win)
    return;
  invalidate_window_rect_internal(win, 0, 0, (int)win->width, (int)win->height);
}

static void free_window_surface(window_t *win) {
  if (!win)
    return;
  if (win->content_surface >= 0) {
    gfx2d_surface_free(win->content_surface);
    win->content_surface = -1;
  }
  win->content_surface_w = 0;
  win->content_surface_h = 0;
}

static int ensure_window_surface(window_t *win) {
  int cw;
  int ch;
  ui_theme_t *theme;
  int surf;

  if (!win)
    return GUI_ERR_INVALID_ARGS;
  if (!window_content_metrics(win, NULL, NULL, &cw, &ch))
    return GUI_ERR_INVALID_ARGS;

  if (win->content_surface >= 0 &&
      win->content_surface_w == (uint16_t)cw &&
      win->content_surface_h == (uint16_t)ch) {
    return GUI_OK;
  }

  free_window_surface(win);

  surf = gfx2d_surface_alloc(cw, ch);
  if (surf < 0)
    return GUI_ERR_NO_MEMORY;

  win->content_surface = surf;
  win->content_surface_w = (uint16_t)cw;
  win->content_surface_h = (uint16_t)ch;

  theme = ui_theme_get();
  gfx2d_surface_fill(surf, theme ? theme->window_bg : 0u);
  invalidate_window_full(win);
  return GUI_OK;
}

static int gui_destroy_window_internal(int wid, bool force_close) {
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;

  if (!force_close && windows[idx].can_close &&
      !windows[idx].can_close(&windows[idx])) {
    return GUI_ERR_CANCELLED;
  }

  if (drag.dragging && drag.window_id == wid) {
    drag.dragging = false;
    drag.window_id = -1;
  }

  /* Notify the application that its window is being destroyed */
  if (windows[idx].on_close) {
    windows[idx].on_close(&windows[idx]);
  }

  free_window_surface(&windows[idx]);
  clear_window_dirty_rect(&windows[idx]);

  /* Shift remaining windows down */
  for (int i = idx; i < win_count - 1; i++) {
    win_copy(&windows[i], &windows[i + 1]);
  }
  win_count--;
  if (win_count >= 0 && win_count < MAX_WINDOWS) {
    memset(&windows[win_count], 0, sizeof(window_t));
  }

  /* Keep focus consistent: the top non-minimized window is focused. */
  sync_focus_to_top_drawable();

  /* Mark everything dirty */
  for (int i = 0; i < win_count; i++) {
    invalidate_window_full(&windows[i]);
  }
  layout_changed_flag = true;

  KINFO("GUI: window %d destroyed", wid);
  return GUI_OK;
}

int gui_create_window(int16_t x, int16_t y, uint16_t w, uint16_t h,
                      const char *title) {
  if (w < 40 || h < (uint16_t)(TITLEBAR_H + 8))
    return GUI_ERR_INVALID_ARGS;

  if (x < (int16_t)(-(int16_t)w + 20))
    x = (int16_t)(-(int16_t)w + 20);
  if (y < 0)
    y = 0;
  if (x > VGA_GFX_WIDTH - 20)
    x = (int16_t)(VGA_GFX_WIDTH - 20);
  if (y > TASKBAR_Y - TITLEBAR_H)
    y = (int16_t)(TASKBAR_Y - TITLEBAR_H);

  {
    int max_h = TASKBAR_Y - (int)y;
    if (max_h < TITLEBAR_H + 8)
      max_h = TITLEBAR_H + 8;
    if ((int)h > max_h)
      h = (uint16_t)max_h;
  }

  if (win_count >= MAX_WINDOWS) {
    KERROR("GUI: cannot create window, limit reached");
    return GUI_ERR_TOO_MANY;
  }

  window_t *win = &windows[win_count];
  memset(win, 0, sizeof(window_t));

  win->id = next_id++;
  win->owner_pid = process_get_current_pid();
  win->x = x;
  win->y = y;
  win->width = w;
  win->height = h;
  win->flags = WINDOW_FLAG_VISIBLE | WINDOW_FLAG_DIRTY;
  win->redraw = NULL;
  win->app_data = NULL;
  win->key_head = 0;
  win->key_tail = 0;
  win->content_surface = -1;
  clear_window_dirty_rect(win);
  invalidate_window_full(win);

  /* Copy title safely */
  int i = 0;
  if (title) {
    while (title[i] && i < 63) {
      win->title[i] = title[i];
      i++;
    }
  }
  win->title[i] = '\0';

  win_count++;
  layout_changed_flag = true;
  gui_set_focus((int)win->id);

  KINFO("GUI: window %u created \"%s\" (%dx%d at %d,%d)", win->id, win->title,
        (int)w, (int)h, (int)x, (int)y);
  return (int)win->id;
}

int gui_destroy_window(int wid) {
  return gui_destroy_window_internal(wid, false);
}

int gui_destroy_windows_by_owner(uint32_t owner_pid) {
  if (owner_pid == 0)
    return 0;

  int destroyed = 0;
  for (int i = 0; i < win_count;) {
    if (windows[i].owner_pid == owner_pid) {
      int wid = (int)windows[i].id;
      if (gui_destroy_window_internal(wid, true) == GUI_OK) {
        destroyed++;
        continue;
      }
    }
    i++;
  }
  return destroyed;
}

int gui_set_focus(int wid) {
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;
  if (!window_is_drawable(&windows[idx]))
    return GUI_ERR_INVALID_ARGS;

  /* Clear focused flag on all */
  for (int i = 0; i < win_count; i++) {
    windows[i].flags &= (uint8_t)~WINDOW_FLAG_FOCUSED;
    if (window_is_drawable(&windows[i]))
      invalidate_window_full(&windows[i]);
  }

  /* Move window to top (end of array) */
  if (idx < win_count - 1) {
    window_t tmp;
    win_copy(&tmp, &windows[idx]);
    for (int i = idx; i < win_count - 1; i++) {
      win_copy(&windows[i], &windows[i + 1]);
    }
    win_copy(&windows[win_count - 1], &tmp);
  }

  windows[win_count - 1].flags |= WINDOW_FLAG_FOCUSED;
  invalidate_window_full(&windows[win_count - 1]);
  return GUI_OK;
}

window_t *gui_get_focused_window(void) {
  int top_idx = top_drawable_index();
  if (top_idx < 0)
    return NULL;
  window_t *top = &windows[top_idx];
  if (top->flags & WINDOW_FLAG_FOCUSED)
    return top;

  /* Recover from transient focus-flag desync: top window is authoritative
   * for keyboard routing and z-order interaction. */
  for (int i = 0; i < win_count; i++) {
    windows[i].flags &= (uint8_t)~WINDOW_FLAG_FOCUSED;
  }
  top->flags |= WINDOW_FLAG_FOCUSED;
  invalidate_window_full(top);
  return top;
}

window_t *gui_get_window(int wid) {
  int idx = find_index(wid);
  if (idx < 0)
    return NULL;
  return &windows[idx];
}

int gui_window_count(void) { return win_count; }

bool gui_any_dirty(void) {
  for (int i = 0; i < win_count; i++) {
    if (!window_is_drawable(&windows[i]))
      continue;
    if (windows[i].flags & (WINDOW_FLAG_DIRTY | WINDOW_FLAG_DRAGGING))
      return true;
  }
  return false;
}

window_t *gui_get_window_by_index(int i) {
  if (i < 0 || i >= win_count)
    return NULL;
  return &windows[i];
}

bool gui_layout_changed(void) { return layout_changed_flag; }

void gui_clear_layout_changed(void) { layout_changed_flag = false; }

bool gui_is_dragging_any(void) { return drag.dragging; }

bool gui_is_dragging_window(int wid) {
  return drag.dragging && drag.window_id == wid;
}

void gui_mark_all_dirty(void) {
  for (int i = 0; i < win_count; i++) {
    if (window_is_drawable(&windows[i])) {
      invalidate_window_full(&windows[i]);
    }
  }
}

int gui_begin_window_paint(int wid) {
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;
  if (!window_is_drawable(&windows[idx]))
    return GUI_ERR_INVALID_ARGS;
  if (ensure_window_surface(&windows[idx]) != GUI_OK)
    return GUI_ERR_NO_MEMORY;
  gfx2d_surface_set_active(windows[idx].content_surface);
  gfx2d_clip_clear();
  return GUI_OK;
}

int gui_end_window_paint(int wid) {
  int idx = find_index(wid);
  int cw;
  int ch;
  if (idx < 0)
    return GUI_ERR_INVALID_ID;
  gfx2d_surface_unset_active();
  if (!window_content_metrics(&windows[idx], NULL, NULL, &cw, &ch))
    return GUI_ERR_INVALID_ARGS;
  invalidate_window_rect_internal(&windows[idx], 1, TITLEBAR_H,
                                  cw, ch);
  return GUI_OK;
}

int gui_invalidate_window(int wid) {
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;
  invalidate_window_full(&windows[idx]);
  return GUI_OK;
}

int gui_invalidate_window_rect(int wid, int x, int y, int w, int h) {
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;
  if (w <= 0 || h <= 0)
    return GUI_ERR_INVALID_ARGS;
  invalidate_window_rect_internal(&windows[idx], x + 1,
                                  y + TITLEBAR_H + WINDOW_CONTENT_TOP_PAD,
                                  w, h);
  return GUI_OK;
}

int gui_present_windows(void) {
  bool any_dirty = gui_any_dirty();

  if (!any_dirty)
    return GUI_OK;

  mouse_mark_cursor_dirty();
  mouse_restore_under_cursor();
  gui_draw_all_windows(false);
  gui_mark_redraw_regions(false);
  mouse_save_under_cursor();
  mouse_draw_cursor();
  vga_flip();
  return GUI_OK;
}

int gui_cache_window_content(int wid) {
  int idx = find_index(wid);
  int cx;
  int cy;
  int cw;
  int ch;
  int surf_w;
  int surf_h;
  uint32_t *fb;
  uint32_t *dst;
  if (idx < 0)
    return GUI_ERR_INVALID_ID;

  window_t *win = &windows[idx];
  if (!window_content_metrics(win, &cx, &cy, &cw, &ch))
    return GUI_ERR_INVALID_ARGS;
  if (cx < 0 || cy < 0 || cx + cw > VGA_GFX_WIDTH || cy + ch > VGA_GFX_HEIGHT)
    return GUI_ERR_INVALID_ARGS;

  if (ensure_window_surface(win) != GUI_OK)
    return GUI_ERR_NO_MEMORY;

  dst = gfx2d_surface_data(win->content_surface, &surf_w, &surf_h);
  if (!dst || surf_w != cw || surf_h != ch)
    return GUI_ERR_INVALID_ARGS;

  fb = vga_get_framebuffer();
  for (int row = 0; row < ch; row++) {
    memcpy(dst + row * cw,
           fb + (cy + row) * VGA_GFX_WIDTH + cx,
           (uint32_t)cw * 4u);
  }

  return GUI_OK;
}

/* Returns true when the focused window has no redraw callback - it is a
 * self-rendering CupidC app that calls vga_flip() itself.  The desktop
 * uses this to skip its own flip during drag so the displayed frame always
 * contains both the desktop's chrome and the app's content. */
bool gui_focused_is_self_rendering(void) {
  window_t *focused = gui_get_focused_window();
  if (!focused)
    return false;
  return focused->redraw == NULL;
}

bool gui_get_drag_invalidate_rect(int16_t *x, int16_t *y,
                                  uint16_t *w, uint16_t *h) {
  if (!x || !y || !w || !h)
    return false;
  if (!drag.dragging)
    return false;

  window_t *win = gui_get_window(drag.window_id);
  if (!win)
    return false;

  int old_x = (int)win->prev_x;
  int old_y = (int)win->prev_y;
  int old_w = (int)win->width;
  int old_h = (int)win->height;

  if (drag.resizing) {
    old_w = (int)drag.start_width;
    old_h = (int)drag.start_height;
  }

  int new_x = (int)win->x;
  int new_y = (int)win->y;
  int new_w = (int)win->width;
  int new_h = (int)win->height;

  int rx0 = old_x < new_x ? old_x : new_x;
  int ry0 = old_y < new_y ? old_y : new_y;
  int rx1 = (old_x + old_w) > (new_x + new_w) ? (old_x + old_w) : (new_x + new_w);
  int ry1 = (old_y + old_h) > (new_y + new_h) ? (old_y + old_h) : (new_y + new_h);

  rx0 -= 4;
  ry0 -= 4;
  rx1 += 4;
  ry1 += 4;

  if (rx0 < 0)
    rx0 = 0;
  if (ry0 < 0)
    ry0 = 0;
  if (rx1 > VGA_GFX_WIDTH)
    rx1 = VGA_GFX_WIDTH;
  if (ry1 > VGA_GFX_HEIGHT)
    ry1 = VGA_GFX_HEIGHT;

  if (rx1 <= rx0 || ry1 <= ry0)
    return false;

  *x = (int16_t)rx0;
  *y = (int16_t)ry0;
  *w = (uint16_t)(rx1 - rx0);
  *h = (uint16_t)(ry1 - ry0);
  return true;
}


static void draw_single_window_shadow(window_t *win) {
  ui_style_t *style = ui_style_get();
  if (!style || !style->use_shadows)
    return;

  gfx_fill_rect((int16_t)(win->x + style->window_shadow_offset),
                (int16_t)(win->y + style->window_shadow_offset), win->width,
                win->height, 0x00606070u);
}

static void draw_single_window(window_t *win) {
  ui_theme_t *theme = ui_theme_get();
  bool focused = (win->flags & WINDOW_FLAG_FOCUSED) != 0;
  int wx = (int)win->x, wy = (int)win->y;
  int ww = (int)win->width;
  uint32_t title_start = focused ? theme->titlebar_active_start
                                 : theme->titlebar_inactive_start;
  uint32_t title_end = focused ? theme->titlebar_active_end
                               : theme->titlebar_inactive_end;
  uint32_t title_text = titlebar_text_color(theme->titlebar_text,
                                            title_start, title_end);

  /* Title bar gradient (only titlebar height - small area, fast) */
  gfx2d_gradient_h(wx + 1, wy + 1, ww - 2, TITLEBAR_H - 1,
                   title_start, title_end);

  /* Title text with shadow */
  gfx2d_text_shadow(wx + 5, wy + 4, win->title, title_text,
                    title_text == 0x00000000u ? 0x00FFFFFFu : 0x00000000u,
                    GFX2D_FONT_NORMAL);

  /* Minimize button */
  int16_t mbx = (int16_t)(win->x + (int16_t)win->width -
                          CLOSE_BTN_SIZE - MINIMIZE_BTN_SIZE - 4);
  int16_t mby = (int16_t)(win->y + 2);
  gfx_fill_rect(mbx, mby, MINIMIZE_BTN_SIZE, MINIMIZE_BTN_SIZE,
                theme->button_face);
  gfx2d_bevel((int)mbx, (int)mby, (int)MINIMIZE_BTN_SIZE,
              (int)MINIMIZE_BTN_SIZE, 1);
  gfx_draw_hline((int16_t)(mbx + 2),
                 (int16_t)(mby + MINIMIZE_BTN_SIZE - 3),
                 (uint16_t)(MINIMIZE_BTN_SIZE - 4), theme->button_text);

  /* Close button */
  int16_t cbx = (int16_t)(win->x + (int16_t)win->width - CLOSE_BTN_SIZE - 2);
  int16_t cby = (int16_t)(win->y + 2);
  gfx_fill_rect(cbx, cby, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, theme->error);
  gfx2d_bevel((int)cbx, (int)cby, (int)CLOSE_BTN_SIZE, (int)CLOSE_BTN_SIZE, 1);
  gfx_draw_line((int16_t)(cbx + 2), (int16_t)(cby + 2),
                (int16_t)(cbx + CLOSE_BTN_SIZE - 3),
                (int16_t)(cby + CLOSE_BTN_SIZE - 3), title_text);
  gfx_draw_line((int16_t)(cbx + CLOSE_BTN_SIZE - 3), (int16_t)(cby + 2),
                (int16_t)(cbx + 2), (int16_t)(cby + CLOSE_BTN_SIZE - 3),
                title_text);

  /* Content area.
   * Callback-based apps overdraw via win->redraw below.
   * Self-rendering apps (no redraw callback) replay their most recent
   * captured content if available so unfocused windows keep their last frame. */
  {
    int cx;
    int cy;
    int cw;
    int ch;
    bool have_metrics = window_content_metrics(win, &cx, &cy, &cw, &ch);
    bool in_bounds = have_metrics &&
                     cx >= 0 && cy >= 0 &&
                     cx + cw <= VGA_GFX_WIDTH &&
                     cy + ch <= VGA_GFX_HEIGHT;

    if (in_bounds && !win->redraw && ensure_window_surface(win) == GUI_OK &&
        win->content_surface >= 0) {
      gfx2d_surface_blit(win->content_surface, cx, cy);
    } else if (in_bounds) {
      gfx_fill_rect((int16_t)cx, (int16_t)cy, (uint16_t)cw, (uint16_t)ch,
                    theme->window_bg);
    }
  }

  /* Border */
  gfx_draw_rect(win->x, win->y, win->width, win->height, theme->window_border);

  /* Bottom-right resize grip */
  {
    int gx = wx + (int)win->width - 2;
    int gy = wy + (int)win->height - 2;
    for (int i = 0; i < 4; i++) {
      gfx_draw_line((int16_t)(gx - 2 - i * 3), (int16_t)(gy),
                    (int16_t)(gx), (int16_t)(gy - 2 - i * 3),
                    theme->window_border);
    }
  }

  /* App-specific redraw */
  if (win->redraw) {
    win->redraw(win);
  }

  win->flags &= (uint8_t)~WINDOW_FLAG_DIRTY;
  clear_window_dirty_rect(win);
}

int gui_draw_window(int wid) {
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;
  draw_single_window(&windows[idx]);
  return GUI_OK;
}

void gui_mark_visible_rects(void) {
  for (int i = 0; i < win_count; i++) {
    window_t *w = &windows[i];
    if (!window_is_drawable(w))
      continue;
    /* Include drop shadow (+3 right/bottom) and a pixel of border clearance */
    vga_mark_dirty((int)w->x - 1, (int)w->y - 1,
                   (int)w->width + 5, (int)w->height + 5);
  }
}

void gui_mark_redraw_regions(bool include_shadows) {
  int pad = include_shadows ? 5 : 2;

  if (last_draw_first_index < 0)
    return;

  for (int i = last_draw_first_index; i < win_count; i++) {
    window_t *w = &windows[i];
    if (!window_is_drawable(w))
      continue;
    vga_mark_dirty((int)w->x - 1, (int)w->y - 1,
                   (int)w->width + pad, (int)w->height + pad);
  }
}

void gui_draw_all_windows(bool draw_shadows) {
  int first = -1;

  for (int i = 0; i < win_count; i++) {
    if (!window_is_drawable(&windows[i]))
      continue;
    if (windows[i].flags &
        (WINDOW_FLAG_DIRTY | WINDOW_FLAG_DRAGGING | WINDOW_FLAG_RESIZING)) {
      first = i;
      break;
    }
  }

  last_draw_first_index = first;
  if (first < 0)
    return;

  /* Draw shadows first (back-to-front pass) so they sit behind all windows.
   * When the background wasn't repainted this frame, old shadow pixels are
   * still correct in the back_buffer - skip the fill entirely. */
  if (draw_shadows) {
    for (int i = first; i < win_count; i++) {
      if (!window_is_drawable(&windows[i]))
        continue;
      draw_single_window_shadow(&windows[i]);
    }
  }

  /* Redraw from the first changed window to top to preserve occlusion. */
  for (int i = first; i < win_count; i++) {
    if (window_is_drawable(&windows[i])) {
      draw_single_window(&windows[i]);
    }
  }
}

int gui_hit_test_titlebar(int16_t mx, int16_t my) {
  for (int i = win_count - 1; i >= 0; i--) {
    window_t *w = &windows[i];
    if (!window_is_drawable(w))
      continue;
    if (mx >= w->x && mx < w->x + (int16_t)w->width && my >= w->y &&
        my < w->y + TITLEBAR_H) {
      return (int)w->id;
    }
  }
  return -1;
}

int gui_hit_test_close(int16_t mx, int16_t my) {
  for (int i = win_count - 1; i >= 0; i--) {
    window_t *w = &windows[i];
    if (!window_is_drawable(w))
      continue;
    int16_t cx = (int16_t)(w->x + (int16_t)w->width - CLOSE_BTN_SIZE - 2);
    int16_t cy = (int16_t)(w->y + 2);
    if (mx >= cx && mx < cx + CLOSE_BTN_SIZE && my >= cy &&
        my < cy + CLOSE_BTN_SIZE) {
      return (int)w->id;
    }
  }
  return -1;
}

int gui_hit_test_window(int16_t mx, int16_t my) {
  for (int i = win_count - 1; i >= 0; i--) {
    window_t *w = &windows[i];
    if (!window_is_drawable(w))
      continue;
    if (mx >= w->x && mx < w->x + (int16_t)w->width && my >= w->y &&
        my < w->y + (int16_t)w->height) {
      return (int)w->id;
    }
  }
  return -1;
}

void gui_handle_mouse(int16_t mx, int16_t my, uint8_t buttons,
                      uint8_t prev_buttons) {
  bool lmb_now = (buttons & 0x01) != 0;
  bool lmb_prev = (prev_buttons & 0x01) != 0;
  bool pressed = lmb_now && !lmb_prev;
  bool released = !lmb_now && lmb_prev;

  /* Dragging in progress */
  if (drag.dragging) {
    if (!lmb_now || released) {
      drag.dragging = false;
      window_t *w = gui_get_window(drag.window_id);
      if (w) {
        w->flags &= (uint8_t)~WINDOW_FLAG_DRAGGING;
        w->flags &= (uint8_t)~WINDOW_FLAG_RESIZING;
        invalidate_window_full(w);
        gui_mark_all_dirty();
      }
      drag.resizing = false;
      drag.window_id = -1;
      layout_changed_flag = true;
    } else {
      window_t *w = gui_get_window(drag.window_id);
      if (w) {
        if (drag.resizing) {
          int nw = (int)drag.start_width + ((int)mx - (int)drag.start_mouse_x);
          int nh =
              (int)drag.start_height + ((int)my - (int)drag.start_mouse_y);
          int max_w = VGA_GFX_WIDTH - (int)w->x;
          int max_h = TASKBAR_Y - (int)w->y;

          if (nw < 40)
            nw = 40;
          if (nh < TITLEBAR_H + 8)
            nh = TITLEBAR_H + 8;
          if (max_w < 40)
            max_w = 40;
          if (max_h < TITLEBAR_H + 8)
            max_h = TITLEBAR_H + 8;
          if (nw > max_w)
            nw = max_w;
          if (nh > max_h)
            nh = max_h;

          w->width = (uint16_t)nw;
          w->height = (uint16_t)nh;
          w->flags |= WINDOW_FLAG_RESIZING;
          w->flags &= (uint8_t)~WINDOW_FLAG_DRAGGING;
          invalidate_window_full(w);
        } else {
          w->prev_x = w->x;
          w->prev_y = w->y;
          w->x = (int16_t)(mx - drag.drag_offset_x);
          w->y = (int16_t)(my - drag.drag_offset_y);
          if (w->x < (int16_t)(-(int16_t)w->width + 20))
            w->x = (int16_t)(-(int16_t)w->width + 20);
          if (w->y < 0)
            w->y = 0;
          if (w->x > VGA_GFX_WIDTH - 20)
            w->x = (int16_t)(VGA_GFX_WIDTH - 20);
          /* Keep the title bar above the taskbar so windows can never
           * be dragged on top of it. */
          if (w->y > TASKBAR_Y - TITLEBAR_H)
            w->y = (int16_t)(TASKBAR_Y - TITLEBAR_H);
          w->flags |= WINDOW_FLAG_DRAGGING;
          w->flags &= (uint8_t)~WINDOW_FLAG_RESIZING;
          invalidate_window_full(w);
        }
        layout_changed_flag = true;
      }
    }
    return;
  }

  /* New press */
  if (pressed) {
    /* Find the topmost window under the cursor first.  All subsequent
     * hit-tests are scoped to this one window so that occluded windows
     * (behind a higher-z peer) can never accidentally steal a click. */
    int top_wid = gui_hit_test_window(mx, my);
    if (top_wid < 0)
      return; /* click landed on bare desktop */

    window_t *w = gui_get_window(top_wid);
    if (!w)
      return;

    /* Close button - only within the topmost window */
    int16_t cbx = (int16_t)(w->x + (int16_t)w->width - CLOSE_BTN_SIZE - 2);
    int16_t cby = (int16_t)(w->y + 2);
    int16_t mbx = (int16_t)(cbx - MINIMIZE_BTN_SIZE - 2);
    int16_t mby = cby;
    if (mx >= mbx && mx < mbx + MINIMIZE_BTN_SIZE &&
        my >= mby && my < mby + MINIMIZE_BTN_SIZE) {
      (void)gui_minimize_window(top_wid);
      return;
    }
    if (mx >= cbx && mx < cbx + CLOSE_BTN_SIZE &&
        my >= cby && my < cby + CLOSE_BTN_SIZE) {
      (void)gui_destroy_window(top_wid);
      return;
    }

    /* Resize grip (bottom-right corner) - only within the topmost window */
    int16_t rgx = (int16_t)(w->x + (int16_t)w->width  - RESIZE_GRIP_SIZE);
    int16_t rgy = (int16_t)(w->y + (int16_t)w->height - RESIZE_GRIP_SIZE);
    if (mx >= rgx && mx < w->x + (int16_t)w->width &&
        my >= rgy && my < w->y + (int16_t)w->height) {
      gui_set_focus(top_wid);
      w = gui_get_window(top_wid);
      if (!w)
        return;
      drag.dragging      = true;
      drag.resizing      = true;
      drag.window_id     = top_wid;
      drag.start_mouse_x = mx;
      drag.start_mouse_y = my;
      drag.start_width   = w->width;
      drag.start_height  = w->height;
      w->flags |= WINDOW_FLAG_RESIZING;
      w->flags &= (uint8_t)~WINDOW_FLAG_DRAGGING;
      invalidate_window_full(w);
      layout_changed_flag = true;
      return;
    }

    /* Title bar drag - only within the topmost window */
    if (my >= w->y && my < w->y + TITLEBAR_H) {
      gui_set_focus(top_wid);
      w = gui_get_window(top_wid);
      if (!w)
        return;
      drag.dragging      = true;
      drag.resizing      = false;
      drag.window_id     = top_wid;
      drag.drag_offset_x = (int16_t)(mx - w->x);
      drag.drag_offset_y = (int16_t)(my - w->y);
      w->prev_x = w->x;
      w->prev_y = w->y;
      w->flags |= WINDOW_FLAG_DRAGGING;
      invalidate_window_full(w);
      layout_changed_flag = true;
      return;
    }

    /* Click on window body → focus */
    gui_set_focus(top_wid);
  }
}

void gui_handle_key(uint8_t scancode, char character) {
  window_t *focused = gui_get_focused_window();
  if (!focused)
    return;

  int next = (focused->key_tail + 1) % GUI_KEY_QUEUE_SIZE;
  if (next != focused->key_head) {
    focused->key_queue[focused->key_tail] =
        ((int)scancode << 8) | (unsigned char)character;
    focused->key_tail = next;
  }
}

int gui_minimize_window(int wid) {
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;

  if (windows[idx].flags & WINDOW_FLAG_MINIMIZED)
    return GUI_OK;

  if (drag.dragging && drag.window_id == wid) {
    drag.dragging = false;
    drag.resizing = false;
    drag.window_id = -1;
  }

  windows[idx].flags |= WINDOW_FLAG_MINIMIZED;
  windows[idx].flags &= (uint8_t)~(WINDOW_FLAG_FOCUSED | WINDOW_FLAG_DIRTY |
                                   WINDOW_FLAG_DRAGGING |
                                   WINDOW_FLAG_RESIZING);
  gui_mark_all_dirty();
  sync_focus_to_top_drawable();
  layout_changed_flag = true;
  return GUI_OK;
}

int gui_restore_window(int wid) {
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;

  windows[idx].flags &= (uint8_t)~WINDOW_FLAG_MINIMIZED;
  windows[idx].flags |= WINDOW_FLAG_VISIBLE;
  invalidate_window_full(&windows[idx]);
  layout_changed_flag = true;
  return gui_set_focus(wid);
}

bool gui_is_minimized(int wid) {
  int idx = find_index(wid);
  if (idx < 0)
    return false;
  return (windows[idx].flags & WINDOW_FLAG_MINIMIZED) != 0;
}
