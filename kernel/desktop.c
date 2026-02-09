/**
 * desktop.c - Desktop shell for cupid-os
 *
 * Implements the desktop background, taskbar, icons, and the
 * main event loop that drives the graphical environment.
 */

#include "desktop.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/rtc.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "../drivers/vga.h"
#include "calendar.h"
#include "gfx2d.h"
#include "graphics.h"
#include "gfx2d_icons.h"
#include "gui.h"
#include "kernel.h"
#include "notepad.h"
#include "cupidc.h"
#include "process.h"
#include "string.h"
#include "terminal_app.h"

/* ── Icon storage ─────────────────────────────────────────────────── */
static desktop_icon_t icons[MAX_DESKTOP_ICONS];
static int icon_count = 0;

/* ── Background gradient row cache ───────────────────────────────── */
static uint32_t bg_lut[VGA_GFX_HEIGHT];
static uint32_t bg_lut_top = 0xFFFFFFFFu; /* invalid sentinel */

/* ── Clock & Calendar state ───────────────────────────────────────── */
static char clock_time_str[16];
static char clock_date_str[16];
static uint8_t clock_last_minute = 255; /* Force initial update */
static int16_t clock_hitbox_x = 0;
static uint16_t clock_hitbox_width = 0;
static uint32_t desktop_anim_tick = 0; /* Animation frame counter */
calendar_state_t cal_state;
static bool cal_prev_visible = false; /* Track calendar visibility changes */

/* ── Init ─────────────────────────────────────────────────────────── */

/* Wrappers for icon drawers (custom_draw takes 2 args, draw funcs take 3) */
static void draw_terminal_icon(int x, int y) {
  gfx2d_draw_icon_terminal(x, y, 0x404040);
}
static void draw_notepad_icon(int x, int y) {
  gfx2d_draw_icon_notepad(x, y, 0xFFFF80);
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
      gfx2d_icon_set_launch(h, terminal_launch);
    }

    h = gfx2d_icon_register("Notepad", "__kernel_notepad", 10, 70);
    if (h >= 0) {
      gfx2d_icon_set_desc(h, "CupidOS Notepad");
      gfx2d_icon_set_color(h, 0xFFFF80);
      gfx2d_icon_set_custom_drawer(h, draw_notepad_icon);
      gfx2d_icon_set_launch(h, notepad_launch);
    }
  }

  /* Scan /bin for .cc files with //icon: directives */
  gfx2d_icons_scan_bin();

  KINFO("Desktop initialized");
}

/* ── Icons ────────────────────────────────────────────────────────── */

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

/* ── Drawing ──────────────────────────────────────────────────────── */

/* Helper: blend two RGB colors by factor t (0..max) */
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

void desktop_draw_background(void) {
  /* Slowly cycle background hue between rose, lavender, and peach */
  /* Period: ~512 ticks ≈ several seconds at ~60Hz */
  uint32_t t = desktop_anim_tick & 511;

  /* Interpolate 3 colors in a triangle: rose → lavender → peach → rose */
  uint32_t top;
  if (t < 170) {
    top = desktop_blend_colors(COLOR_DESKTOP_BG, 0xEEDDFF, (int)t, 170);
  } else if (t < 340) {
    top = desktop_blend_colors(0xEEDDFF, 0xFFF0E0, (int)(t - 170), 170);
  } else {
    top = desktop_blend_colors(0xFFF0E0, COLOR_DESKTOP_BG, (int)(t - 340), 172);
  }

  /* Rebuild LUT only when the top colour changes */
  if (top != bg_lut_top) {
    bg_lut_top = top;
    int max_row = (TASKBAR_Y > 1) ? TASKBAR_Y - 1 : 1;
    for (int row = 0; row < TASKBAR_Y; row++) {
      bg_lut[row] = desktop_blend_colors(top, COLOR_TASKBAR, row, max_row);
    }
  }

  /* Fill from LUT (no per-row division) */
  for (int row = 0; row < (int)TASKBAR_Y; row++) {
    gfx_draw_hline(0, (int16_t)row, VGA_GFX_WIDTH, bg_lut[row]);
  }
}

void desktop_draw_taskbar(void) {
  /* Taskbar: gradient base */
  gfx2d_gradient_v(0, TASKBAR_Y, VGA_GFX_WIDTH, TASKBAR_HEIGHT, 0x00DDD0F0u,
                   COLOR_TASKBAR);

  /* Top separator line */
  gfx_draw_hline(0, TASKBAR_Y, VGA_GFX_WIDTH, COLOR_BORDER);

  /* "cupid-os" branding on the left */
  gfx_draw_text(4, (int16_t)(TASKBAR_Y + 6), "cupid-os", COLOR_TEXT_LIGHT);

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

    /* We'll try IDs 1..64 (more than enough) */
    for (uint32_t id = 1; id < 64 && btn_x < btn_limit; id++) {
      window_t *w = gui_get_window((int)id);
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

  /* ── Clock display (right-aligned) ────────────────────────── */
  {
    rtc_time_t time;
    rtc_date_t date;
    rtc_read_time(&time);
    rtc_read_date(&date);

    /* Only rebuild strings if minute changed */
    if (time.minute != clock_last_minute) {
      if (rtc_validate_time(&time)) {
        format_time_12hr(&time, clock_time_str, (int)sizeof(clock_time_str));
      } else {
        clock_time_str[0] = '-';
        clock_time_str[1] = '-';
        clock_time_str[2] = ':';
        clock_time_str[3] = '-';
        clock_time_str[4] = '-';
        clock_time_str[5] = '\0';
      }
      if (rtc_validate_date(&date)) {
        format_date_short(&date, clock_date_str, (int)sizeof(clock_date_str));
      } else {
        clock_date_str[0] = '\0';
      }
      clock_last_minute = time.minute;
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
  /* Draw all registered gfx2d icons (kernel + auto-discovered) */
  gfx2d_icons_draw_all();
}

/* ── Taskbar hit-test ─────────────────────────────────────────────── */

int desktop_hit_test_taskbar(int16_t mx, int16_t my) {
  if (my < TASKBAR_Y + 2 || my >= TASKBAR_Y + TASKBAR_HEIGHT)
    return -1;

  int16_t btn_x = TASKBAR_BTN_START;
  int16_t btn_limit =
      (int16_t)(clock_hitbox_x > 0 ? clock_hitbox_x - 4 : VGA_GFX_WIDTH - 60);

  for (uint32_t id = 1; id < 64 && btn_x < btn_limit; id++) {
    window_t *w = gui_get_window((int)id);
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

/* ── Calendar popup ────────────────────────────────────────────────── */

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

  /* Close button "x" — top-right corner */
  {
    int16_t close_x = (int16_t)(cx + CALENDAR_WIDTH - 20);
    int16_t close_y = (int16_t)(cy + 2);
    gfx_fill_rect(close_x, close_y, 18, 18, COLOR_CLOSE_BG);
    gfx_draw_rect(close_x, close_y, 18, 18, COLOR_BORDER);
    gfx_draw_text((int16_t)(close_x + 5), (int16_t)(close_y + 5), "x", COLOR_TEXT_LIGHT);
  }

  /* ── Header: ◄ Month Year ► (below close button) ──────── */
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

  /* ── Day headers: Su Mo Tu We Th Fr Sa ─────────────────── */
  int16_t grid_x = (int16_t)(cx + (CALENDAR_WIDTH - 7 * 52) / 2);
  int16_t grid_y = (int16_t)(sep_y + 22);
  {
    static const char *day_hdrs[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    for (int i = 0; i < 7; i++) {
      int16_t dx = (int16_t)(grid_x + i * 52);
      gfx_draw_text(dx, grid_y, day_hdrs[i], COLOR_TEXT);
    }
  }

  /* ── Day grid ──────────────────────────────────────────── */
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
    /* Click outside — close calendar */
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
      notepad_launch_with_file(note->path, persist_path);

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

  /* Outside calendar — ignore */
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

/* ── Main event loop ──────────────────────────────────────────────── */

void desktop_redraw_cycle(void) {
  bool needs_redraw = false;

  /* Process mouse */
  if (mouse.updated) {
    mouse.updated = false;
    needs_redraw = true;

    if (mouse.scroll_z != 0) {
      int scroll_lines = (int)mouse.scroll_z * 5;
      /* Route scroll to focused window */
      int np_wid = notepad_get_wid();
      window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
      if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
        notepad_handle_scroll(scroll_lines);
      } else {
        terminal_handle_scroll(scroll_lines);
      }
      mouse.scroll_z = 0;
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
        continue;
      }
      /* Route key to focused window */
      int np_wid = notepad_get_wid();
      window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
      if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
        notepad_handle_key(event.scancode, event.character);
      } else {
        terminal_handle_key(event.scancode, event.character);
      }
      needs_redraw = true;
    }
  }

  /* Cursor blink */
  terminal_tick();
  notepad_tick();

  /* Redraw if needed */
  if (needs_redraw || gui_any_dirty()) {
    desktop_anim_tick++; /* Drive animated background */
    desktop_draw_background();
    desktop_draw_icons();
    gui_draw_all_windows();
    desktop_draw_taskbar();
    desktop_draw_calendar();

    mouse_save_under_cursor();
    mouse_draw_cursor();

    vga_flip();
  }
}

void desktop_run(void) {
  bool needs_redraw = true;
  bool force_full_repaint = false;

  while (1) {
    /* Skip ALL desktop processing if a fullscreen app is running */
    if (gfx2d_fullscreen_active()) {
      process_yield();
      continue;
    }

    /* ── Process mouse ──────────────────────────────────────── */
    if (mouse.updated) {
      mouse.updated = false;
      needs_redraw = true;

      /* Handle scroll wheel – consume accumulated delta */
      if (mouse.scroll_z != 0) {
        /* Each scroll notch is ±1 in scroll_z.
         * Multiply by 5 lines per notch for snappy scrolling.
         * Positive scroll_z = scroll up (show older content). */
        int scroll_lines = (int)mouse.scroll_z * 5;
        /* Route scroll to focused window */
        int np_wid = notepad_get_wid();
        window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
        if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
          notepad_handle_scroll(scroll_lines);
        } else {
          terminal_handle_scroll(scroll_lines);
        }
        mouse.scroll_z = 0;
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

      /* Right-click on calendar: delete note */
      if (right_pressed && cal_state.visible) {
        calendar_handle_right_click(mouse.x, mouse.y);
      }

      /* Check taskbar clicks first */
      if (pressed && mouse.y >= TASKBAR_Y) {
        /* Check clock hitbox */
        if (mouse.x >= clock_hitbox_x &&
            mouse.x < clock_hitbox_x + (int16_t)clock_hitbox_width) {
          desktop_toggle_calendar();
        } else {
          int tb_id = desktop_hit_test_taskbar(mouse.x, mouse.y);
          if (tb_id >= 0) {
            gui_set_focus(tb_id);
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
      else if (pressed && cal_state.visible) {
        /* Get calendar bounds to check if click was inside or outside */
        int16_t cx = (int16_t)((VGA_GFX_WIDTH - CALENDAR_WIDTH) / 2);
        int16_t cy = (int16_t)((TASKBAR_Y - CALENDAR_HEIGHT) / 2);
        bool click_inside = (mouse.x >= cx && mouse.x < cx + CALENDAR_WIDTH &&
                             mouse.y >= cy && mouse.y < cy + CALENDAR_HEIGHT);

        calendar_handle_click(mouse.x, mouse.y);

        /* If click was outside calendar, pass through to window manager */
        if (!click_inside) {
          gui_handle_mouse(mouse.x, mouse.y, btn, prev);
          /* Also forward to notepad if its window is focused */
          int np_wid = notepad_get_wid();
          window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
          if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
            notepad_handle_mouse(mouse.x, mouse.y, btn, prev);
          }
        }
      }
      /* Check icon clicks */
      else if (pressed && gui_hit_test_window(mouse.x, mouse.y) < 0) {
        /* Check gfx2d desktop icons (unified system) */
        int gfx_icon = gfx2d_icon_at_pos(mouse.x, mouse.y);
        if (gfx_icon >= 0) {
          gfx2d_icon_select(gfx_icon);
          /* Try direct launch callback first (kernel icons) */
          void (*launch_fn)(void) = gfx2d_icon_get_launch(gfx_icon);
          if (launch_fn) {
            launch_fn();
          } else {
            /* Fall back to cupidc_jit for .cc programs */
            const char *prog = gfx2d_icon_get_path(gfx_icon);
            if (prog && prog[0]) {
              cupidc_jit(prog);
            }
          }
          /* App launch/return may leave stale framebuffer contents. */
          force_full_repaint = true;
          needs_redraw = true;
        }
      }
      /* Forward to GUI window manager */
      else {
        gui_handle_mouse(mouse.x, mouse.y, btn, prev);
        /* Also forward to notepad if its window is focused */
        int np_wid = notepad_get_wid();
        window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
        if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
          notepad_handle_mouse(mouse.x, mouse.y, btn, prev);
        }
      }
    }

    /* ── Process keyboard ───────────────────────────────────── */
    {
      key_event_t event;
      while (keyboard_read_event(&event)) {
        /* Escape closes calendar popup */
        if (event.scancode == 0x01 && event.pressed && cal_state.visible) {
          cal_state.visible = false;
          needs_redraw = true;
          continue;
        }
        /* Route key to focused window */
        int np_wid = notepad_get_wid();
        window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
        if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
          notepad_handle_key(event.scancode, event.character);
        } else {
          terminal_handle_key(event.scancode, event.character);
        }
        needs_redraw = true;
      }
    }

    /* ── Cursor blink tick ──────────────────────────────────── */
    terminal_tick();
    notepad_tick();

    /* ── Redraw ─────────────────────────────────────────────── */
    /* ── Redraw ─────────────────────────────────────────────── */
    /* Skip desktop rendering if a fullscreen gfx2d app is running */
    if (gfx2d_fullscreen_active()) {
      /* Also skip input processing so we don't steal clicks/keys */
      process_yield();
      continue;
    }

    if (needs_redraw || gui_any_dirty()) {
      static bool has_first_render = false;
      static bool fast_path_used = false;

      /* Check if calendar visibility changed */
      bool cal_visibility_changed = (cal_state.visible != cal_prev_visible);
      cal_prev_visible = cal_state.visible;

      if (!force_full_repaint && !gui_any_dirty() && !gui_layout_changed() &&
          !cal_state.visible &&
          !cal_visibility_changed && has_first_render) {
        /* Fast path: only cursor moved — write directly to displayed LFB */
        mouse_update_cursor_direct();
        fast_path_used = true;
      } else {
        /* Full scene render.
         * If fast path ran since last full render, back_buffer has a stale
         * cursor at the old position (fast path never touches back_buffer).
         * Force a full background repaint to cover it. */
        if (force_full_repaint || gui_layout_changed() || fast_path_used ||
            cal_visibility_changed) {
          fast_path_used = false;
          force_full_repaint = false;
          desktop_anim_tick++;
          desktop_draw_background();
          desktop_draw_icons();
          gui_clear_layout_changed();
        } else {
          mouse_restore_under_cursor();
        }
        gui_draw_all_windows();
        desktop_draw_taskbar();
        desktop_draw_calendar();
        mouse_save_under_cursor();
        mouse_draw_cursor();
        vga_flip();
        has_first_render = true;
      }

      /* FPS counter: print to serial once per second */
      {
        static uint32_t fps_frames = 0;
        static uint32_t fps_last_ms = 0;
        fps_frames++;
        uint32_t now = timer_get_uptime_ms();
        if (now - fps_last_ms >= 1000U) {
          serial_printf("[fps] %u fps\n", fps_frames);
          fps_frames = 0;
          fps_last_ms = now;
        }
      }

      needs_redraw = false;
    }

    /* Check for deferred reschedule (preemptive time slice) */
    kernel_check_reschedule();

    /* Yield CPU until next interrupt */
    __asm__ volatile("hlt");
  }
}
