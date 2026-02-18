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
#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "gfx2d.h"
#include "graphics.h"
#include "memory.h"
#include "process.h"
#include "string.h"

static window_t windows[MAX_WINDOWS];
static int win_count = 0;
static uint32_t next_id = 1;

static drag_state_t drag = {false, false, -1, 0, 0, 0, 0, 0, 0};

#define RESIZE_GRIP_SIZE 12

static bool layout_changed_flag = true; /* start true to force first render */

void gui_init(void) {
  win_count = 0;
  next_id = 1;
  drag.dragging = false;
  drag.resizing = false;
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

static void free_window_cache(window_t *win) {
  if (!win)
    return;
  if (win->content_cache) {
    kfree(win->content_cache);
    win->content_cache = NULL;
  }
  win->content_cache_w = 0;
  win->content_cache_h = 0;
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
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;

  if (drag.dragging && drag.window_id == wid) {
    drag.dragging = false;
    drag.window_id = -1;
  }

  /* Notify the application that its window is being destroyed */
  if (windows[idx].on_close) {
    windows[idx].on_close(&windows[idx]);
  }

  free_window_cache(&windows[idx]);

  /* Shift remaining windows down */
  for (int i = idx; i < win_count - 1; i++) {
    win_copy(&windows[i], &windows[i + 1]);
  }
  win_count--;
  if (win_count >= 0 && win_count < MAX_WINDOWS) {
    memset(&windows[win_count], 0, sizeof(window_t));
  }

  /* Keep focus consistent: top window is always focused when any exist. */
  for (int i = 0; i < win_count; i++) {
    windows[i].flags &= (uint8_t)~WINDOW_FLAG_FOCUSED;
  }
  if (win_count > 0) {
    windows[win_count - 1].flags |= WINDOW_FLAG_FOCUSED;
  }

  /* Mark everything dirty */
  for (int i = 0; i < win_count; i++) {
    windows[i].flags |= WINDOW_FLAG_DIRTY;
  }
  layout_changed_flag = true;

  KINFO("GUI: window %d destroyed", wid);
  return GUI_OK;
}

int gui_destroy_windows_by_owner(uint32_t owner_pid) {
  if (owner_pid == 0)
    return 0;

  int destroyed = 0;
  for (int i = 0; i < win_count;) {
    if (windows[i].owner_pid == owner_pid) {
      int wid = (int)windows[i].id;
      if (gui_destroy_window(wid) == GUI_OK) {
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

  /* Clear focused flag on all */
  for (int i = 0; i < win_count; i++) {
    windows[i].flags &= (uint8_t)~WINDOW_FLAG_FOCUSED;
    windows[i].flags |= WINDOW_FLAG_DIRTY;
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

  windows[win_count - 1].flags |= WINDOW_FLAG_FOCUSED | WINDOW_FLAG_DIRTY;
  return GUI_OK;
}

window_t *gui_get_focused_window(void) {
  if (win_count == 0)
    return NULL;
  window_t *top = &windows[win_count - 1];
  if (top->flags & WINDOW_FLAG_FOCUSED)
    return top;

  /* Recover from transient focus-flag desync: top window is authoritative
   * for keyboard routing and z-order interaction. */
  for (int i = 0; i < win_count; i++) {
    windows[i].flags &= (uint8_t)~WINDOW_FLAG_FOCUSED;
  }
  top->flags |= WINDOW_FLAG_FOCUSED | WINDOW_FLAG_DIRTY;
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
    if (windows[i].flags & WINDOW_FLAG_VISIBLE) {
      windows[i].flags |= WINDOW_FLAG_DIRTY;
    }
  }
}

int gui_cache_window_content(int wid) {
  int idx = find_index(wid);
  if (idx < 0)
    return GUI_ERR_INVALID_ID;

  window_t *win = &windows[idx];
  int cx = (int)win->x + 1;
  int cy = (int)win->y + TITLEBAR_H + WINDOW_CONTENT_TOP_PAD;
  int cw = (int)win->width - 2;
  int ch = (int)win->height - TITLEBAR_H - WINDOW_CONTENT_BORDER;

  if (cw <= 0 || ch <= 0)
    return GUI_ERR_INVALID_ARGS;

  if (cx < 0 || cy < 0 || cx + cw > VGA_GFX_WIDTH || cy + ch > VGA_GFX_HEIGHT)
    return GUI_ERR_INVALID_ARGS;

  if (!win->content_cache || win->content_cache_w != (uint16_t)cw ||
      win->content_cache_h != (uint16_t)ch) {
    uint32_t *buf = (uint32_t *)kmalloc((uint32_t)cw * (uint32_t)ch * 4u);
    if (!buf)
      return GUI_ERR_NO_MEMORY;
    free_window_cache(win);
    win->content_cache = buf;
    win->content_cache_w = (uint16_t)cw;
    win->content_cache_h = (uint16_t)ch;
  }

  {
    uint32_t *fb = vga_get_framebuffer();
    for (int row = 0; row < ch; row++) {
      memcpy(win->content_cache + row * cw,
             fb + (cy + row) * VGA_GFX_WIDTH + cx,
             (uint32_t)cw * 4u);
    }
  }

  return GUI_OK;
}

/* Returns true when the focused window has no redraw callback — it is a
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
  gfx_fill_rect((int16_t)(win->x + 3), (int16_t)(win->y + 3), win->width,
                win->height, 0x00606070u);
}

static void draw_single_window(window_t *win) {
  bool focused = (win->flags & WINDOW_FLAG_FOCUSED) != 0;
  int wx = (int)win->x, wy = (int)win->y;
  int ww = (int)win->width;

  /* Title bar gradient (only titlebar height - small area, fast) */
  if (focused) {
    gfx2d_gradient_h(wx + 1, wy + 1, ww - 2, TITLEBAR_H - 1, 0x000060C8u,
                     0x0040A8F8u);
  } else {
    gfx2d_gradient_h(wx + 1, wy + 1, ww - 2, TITLEBAR_H - 1, 0x006878A8u,
                     0x0090A8C8u);
  }

  /* Title text with shadow */
  gfx2d_text_shadow(wx + 5, wy + 4, win->title, COLOR_TEXT_LIGHT, 0x00000000u,
                    GFX2D_FONT_NORMAL);

  /* Close button */
  int16_t cbx = (int16_t)(win->x + (int16_t)win->width - CLOSE_BTN_SIZE - 2);
  int16_t cby = (int16_t)(win->y + 2);
  gfx_fill_rect(cbx, cby, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, COLOR_CLOSE_BG);
  gfx2d_bevel((int)cbx, (int)cby, (int)CLOSE_BTN_SIZE, (int)CLOSE_BTN_SIZE, 1);
  gfx_draw_line((int16_t)(cbx + 2), (int16_t)(cby + 2),
                (int16_t)(cbx + CLOSE_BTN_SIZE - 3),
                (int16_t)(cby + CLOSE_BTN_SIZE - 3), COLOR_TEXT_LIGHT);
  gfx_draw_line((int16_t)(cbx + CLOSE_BTN_SIZE - 3), (int16_t)(cby + 2),
                (int16_t)(cbx + 2), (int16_t)(cby + CLOSE_BTN_SIZE - 3),
                COLOR_TEXT_LIGHT);

  /* Content area.
   * Callback-based apps overdraw via win->redraw below.
   * Self-rendering apps (no redraw callback) replay their most recent
   * captured content if available so unfocused windows keep their last frame. */
  {
    int cx = (int)win->x + 1;
    int cy = (int)win->y + TITLEBAR_H + WINDOW_CONTENT_TOP_PAD;
    int cw = (int)win->width - 2;
    int ch = (int)win->height - TITLEBAR_H - WINDOW_CONTENT_BORDER;
    bool in_bounds = (cx >= 0 && cy >= 0 && cw > 0 && ch > 0 &&
                      cx + cw <= VGA_GFX_WIDTH &&
                      cy + ch <= VGA_GFX_HEIGHT);

    if (in_bounds && !win->redraw && win->content_cache &&
        win->content_cache_w == (uint16_t)cw &&
        win->content_cache_h == (uint16_t)ch) {
      uint32_t *fb = vga_get_framebuffer();
      simd_blit_rect(fb + (uint32_t)cy * (uint32_t)VGA_GFX_WIDTH + (uint32_t)cx,
                     win->content_cache,
                     (uint32_t)VGA_GFX_WIDTH, (uint32_t)cw,
                     (uint32_t)cw, (uint32_t)ch);
    } else {
      gfx_fill_rect((int16_t)cx, (int16_t)cy, (uint16_t)cw, (uint16_t)ch,
                    COLOR_WINDOW_BG);
    }
  }

  /* Border */
  gfx_draw_rect(win->x, win->y, win->width, win->height, COLOR_BORDER);

  /* Bottom-right resize grip */
  {
    int gx = wx + (int)win->width - 2;
    int gy = wy + (int)win->height - 2;
    for (int i = 0; i < 4; i++) {
      gfx_draw_line((int16_t)(gx - 2 - i * 3), (int16_t)(gy),
                    (int16_t)(gx), (int16_t)(gy - 2 - i * 3),
                    COLOR_BORDER);
    }
  }

  /* App-specific redraw */
  if (win->redraw) {
    win->redraw(win);
  }

  win->flags &= (uint8_t)~WINDOW_FLAG_DIRTY;
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
    if (!(w->flags & WINDOW_FLAG_VISIBLE))
      continue;
    /* Include drop shadow (+3 right/bottom) and a pixel of border clearance */
    vga_mark_dirty((int)w->x - 1, (int)w->y - 1,
                   (int)w->width + 5, (int)w->height + 5);
  }
}

void gui_draw_all_windows(bool draw_shadows) {
  int first = -1;

  for (int i = 0; i < win_count; i++) {
    if (!(windows[i].flags & WINDOW_FLAG_VISIBLE))
      continue;
    if (windows[i].flags &
        (WINDOW_FLAG_DIRTY | WINDOW_FLAG_DRAGGING | WINDOW_FLAG_RESIZING)) {
      first = i;
      break;
    }
  }

  if (first < 0)
    return;

  /* Draw shadows first (back-to-front pass) so they sit behind all windows.
   * When the background wasn't repainted this frame, old shadow pixels are
   * still correct in the back_buffer — skip the fill entirely. */
  if (draw_shadows) {
    for (int i = first; i < win_count; i++) {
      if (windows[i].flags & WINDOW_FLAG_VISIBLE) {
        draw_single_window_shadow(&windows[i]);
      }
    }
  }

  /* Redraw from the first changed window to top to preserve occlusion. */
  for (int i = first; i < win_count; i++) {
    if (windows[i].flags & WINDOW_FLAG_VISIBLE) {
      draw_single_window(&windows[i]);
    }
  }
}

int gui_hit_test_titlebar(int16_t mx, int16_t my) {
  for (int i = win_count - 1; i >= 0; i--) {
    window_t *w = &windows[i];
    if (!(w->flags & WINDOW_FLAG_VISIBLE))
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
    if (!(w->flags & WINDOW_FLAG_VISIBLE))
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
    if (!(w->flags & WINDOW_FLAG_VISIBLE))
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
        w->flags |= WINDOW_FLAG_DIRTY;
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
          w->flags |= WINDOW_FLAG_DIRTY;
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
          w->flags |= WINDOW_FLAG_DIRTY;
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

    /* Close button — only within the topmost window */
    int16_t cbx = (int16_t)(w->x + (int16_t)w->width - CLOSE_BTN_SIZE - 2);
    int16_t cby = (int16_t)(w->y + 2);
    if (mx >= cbx && mx < cbx + CLOSE_BTN_SIZE &&
        my >= cby && my < cby + CLOSE_BTN_SIZE) {
      gui_destroy_window(top_wid);
      return;
    }

    /* Resize grip (bottom-right corner) — only within the topmost window */
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
      w->flags |= WINDOW_FLAG_DIRTY;
      layout_changed_flag = true;
      return;
    }

    /* Title bar drag — only within the topmost window */
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
      w->flags |= WINDOW_FLAG_DIRTY;
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

  int next = (focused->key_tail + 1) & 15;
  if (next != focused->key_head) {
    focused->key_queue[focused->key_tail] =
        ((int)scancode << 8) | (unsigned char)character;
    focused->key_tail = next;
  }
}
