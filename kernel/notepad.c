/**
 * notepad.c - Windows XP-style Notepad for cupid-os
 *
 * Full GUI text editor with menu bar, scrollbars, file operations,
 * clipboard support, and undo/redo.  Renders in VGA Mode 13h
 * (320x200, 256 colors) with double buffering.
 *
 * Phases implemented:
 *   1. Core editor (buffer, cursor, viewport scrolling)
 *   2. Selection and clipboard (mouse/keyboard selection, copy/cut/paste)
 *   3. Scrollbars (vertical & horizontal, draggable thumbs)
 *   4. Menu system (File, Edit dropdowns with keyboard shortcuts)
 *   5. File operations (open/save via FAT16 file browser dialog)
 *   6. Icon and polish (desktop icon, status bar)
 */

#include "notepad.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "../drivers/vga.h"
#include "calendar.h"
#include "clipboard.h"
#include "desktop.h"
#include "fat16.h"
#include "font_8x8.h"
#include "graphics.h"
#include "gui.h"
#include "kernel.h"
#include "memory.h"
#include "process.h"
#include "string.h"
#include "ui.h"
#include "vfs.h"

/* Constants */
#define NOTEPAD_WIN_W 540
#define NOTEPAD_WIN_H 350

#define NOTEPAD_MAX_LINES 4096
#define NOTEPAD_MAX_LINE_LEN 256

#define MENUBAR_H 12
#define STATUSBAR_H 10
#define VSCROLL_W 12
#define HSCROLL_H 12
#define SCROLL_ARROW_SIZE 12
#define SCROLL_THUMB_MIN 20

#define CURSOR_BLINK_MS 500

/* Scancode definitions */
#define SC_BACKSPACE 0x0E
#define SC_TAB 0x0F
#define SC_ENTER 0x1C
#define SC_LCTRL 0x1D
#define SC_DELETE 0x53
#define SC_HOME 0x47
#define SC_END 0x4F
#define SC_PAGE_UP 0x49
#define SC_PAGE_DOWN 0x51
#define SC_ARROW_UP 0x48
#define SC_ARROW_DOWN 0x50
#define SC_ARROW_LEFT 0x4B
#define SC_ARROW_RIGHT 0x4D
#define SC_ESCAPE 0x01
#define SC_F2 0x3C

/* Key scancodes for shortcuts (lowercase letters) */
#define SC_KEY_A 0x1E
#define SC_KEY_C 0x2E
#define SC_KEY_N 0x31
#define SC_KEY_O 0x18
#define SC_KEY_Q 0x10
#define SC_KEY_R 0x13
#define SC_KEY_S 0x1F
#define SC_KEY_V 0x2F
#define SC_KEY_X 0x2D
#define SC_KEY_Y 0x15
#define SC_KEY_Z 0x2C
#define SC_KEY_EQUALS 0x0D /* =/+ key */
#define SC_KEY_MINUS 0x0C  /* -/_ key */

/* Menu item indices */
#define MENU_NONE -1
#define MENU_FILE 0
#define MENU_EDIT 1

/* File menu items */
#define FMENU_NEW 0
#define FMENU_OPEN 1
#define FMENU_SAVE 2
#define FMENU_SAVE_AS 3
#define FMENU_SEP 4
#define FMENU_EXIT 5
#define FMENU_COUNT 6

/* Edit menu items */
#define EMENU_UNDO 0
#define EMENU_REDO 1
#define EMENU_SEP1 2
#define EMENU_CUT 3
#define EMENU_COPY 4
#define EMENU_PASTE 5
#define EMENU_SEP2 6
#define EMENU_SELECT_ALL 7
#define EMENU_COUNT 8

/* Data structures */

typedef struct {
  char *lines[NOTEPAD_MAX_LINES];
  int line_count;
  int cursor_line;
  int cursor_col;
  int scroll_x;
  int scroll_y;
  bool modified;
  char filename[64];
} notepad_buffer_t;

typedef struct {
  bool active;
  int start_line, start_col;
  int end_line, end_col;
  bool dragging;
} notepad_selection_t;

typedef struct {
  bool dragging_vthumb;
  bool dragging_hthumb;
  int drag_start_x, drag_start_y;
  int drag_start_scroll;
} scrollbar_state_t;

/* File browser dialog */
typedef struct {
  char filename[32];
  uint32_t size;
  bool is_directory;
} file_entry_t;

#define DLG_W 400
#define DLG_H 300
#define DLG_LIST_H 190
#define DLG_ITEM_H 14
#define DLG_SCROLLBAR_W 12
#define DLG_BTN_W 60
#define DLG_BTN_H 24
#define DLG_FILES_INIT_CAP 64
#define DLG_FILES_MAX_CAP 2048

/* Computed layout for the file dialog - shared by draw and mouse */
typedef struct {
  ui_rect_t dialog;      /* Outer dialog rect                    */
  ui_rect_t titlebar;    /* Title bar                            */
  ui_rect_t path_row;    /* Current directory display            */
  ui_rect_t list_area;   /* File list + scrollbar (sunken)       */
  ui_rect_t list;        /* File list only (no scrollbar)        */
  ui_rect_t scrollbar;   /* Vertical scrollbar                   */
  ui_rect_t input_label; /* "File:" label                        */
  ui_rect_t input_field; /* Text input field                     */
  ui_rect_t ok_btn;      /* OK / Open / Save button              */
  ui_rect_t cancel_btn;  /* Cancel button                        */
  ui_rect_t status;      /* File count text area                 */
  int items_y;           /* Y of first file entry (absolute)     */
  int items_h;           /* Height available for file entries    */
  int items_visible;     /* Number of visible file entries       */
} dlg_layout_t;

typedef struct {
  file_entry_t *files;
  int file_cap;
  int file_count;
  int selected_index;
  int scroll_offset;
  bool open;
  bool save_mode;
  char input[64];
  int input_len;
} file_dialog_t;

typedef struct {
  notepad_buffer_t buffer;
  notepad_selection_t selection;
  scrollbar_state_t scrollbars;

  /* CupidDoc (.ctxt) render mode */
  bool is_ctxt_file;
  bool render_mode;
  bool ctxt_theme_light;
  int ctxt_scroll_px;
  int ctxt_scroll_x_px;
  int ctxt_content_height;
  int ctxt_content_width;
  uint32_t ctxt_style_bg;
  uint32_t ctxt_style_h1;
  uint32_t ctxt_style_h2;
  uint32_t ctxt_style_h3;
  uint32_t ctxt_style_body;
  uint32_t ctxt_style_rule;
  uint32_t ctxt_style_box_bg;
  uint32_t ctxt_style_box_text;
  uint32_t ctxt_style_link;
  uint32_t ctxt_style_link_hover;
  uint16_t ctxt_style_mask;
  struct {
    uint8_t type;
    char text[NOTEPAD_MAX_LINE_LEN];
    uint32_t color;
    uint32_t bg_color;
  } ctxt_lines[NOTEPAD_MAX_LINES];
  int ctxt_line_count;

  struct {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
    char target[VFS_MAX_PATH];
  } ctxt_links[256];
  int ctxt_link_count;
  int ctxt_hover_link;

  /* Menu state */
  int active_menu;
  int hover_item;

  /* Undo state (single-level) */
  char *undo_lines[NOTEPAD_MAX_LINES];
  int undo_line_count;
  int undo_cursor_line, undo_cursor_col;
  bool undo_available;
  bool redo_available;
  char *redo_lines[NOTEPAD_MAX_LINES];
  int redo_line_count;
  int redo_cursor_line, redo_cursor_col;

  /* Window integration */
  int window_id;
  bool dialog_open;
  file_dialog_t dialog;

  /* Cursor blink */
  bool cursor_visible;
  uint32_t last_blink_ms;

  /* Font zoom: 1 = normal, 2 = 2x, 3 = 3x */
  int font_scale;

  /* Process */
  uint32_t pid;
} notepad_app_t;

/* Static state */
static notepad_app_t app;
static int notepad_wid = -1;

/* Forward declarations */
static void notepad_init_buffer(void);
static void notepad_free_buffer(void);
static char *notepad_strdup(const char *s);
static void notepad_ensure_line(int idx);
static int notepad_max_line_width(void);
static int notepad_filename_is_ctxt(const char *path);
static int notepad_buffer_looks_ctxt(void);
static int notepad_toggle_ctxt_mode(window_t *win);

/* CupidDoc (.ctxt) */
static void notepad_ctxt_parse(void);
static void notepad_draw_ctxt_area(window_t *win);
static int notepad_ctxt_text_width(const char *text, int scale);
static void notepad_ctxt_draw_text(int16_t x, int16_t y, const char *text,
                                   uint32_t base_color, int scale,
                                   int16_t max_x, int line_h,
                                   uint32_t link_color,
                                   uint32_t link_hover_color,
                                   uint32_t code_fg,
                                   uint32_t code_bg);
static int notepad_ctxt_line_height(uint8_t type);
static int notepad_ctxt_line_scale(uint8_t type);
static int notepad_ctxt_hit_link(int16_t mx, int16_t my);
static void notepad_ctxt_resolve_link(const char *target, char *out);
static void notepad_ctxt_open_link(const char *target);
static int notepad_ctxt_max_scroll(window_t *win);
static int notepad_ctxt_max_scroll_x(window_t *win);

/* Rendering */
static void notepad_draw_menubar(window_t *win);
static void notepad_draw_dropdown(window_t *win);
static void notepad_draw_text_area(window_t *win);
static void notepad_draw_scrollbars(window_t *win);
static void notepad_draw_statusbar(window_t *win);
static void notepad_draw_file_dialog(window_t *win);

/* Editing */
static void notepad_insert_char(char c);
static void notepad_delete_char(void);
static void notepad_backspace(void);
static void notepad_insert_newline(void);
static void notepad_move_cursor(int dl, int dc);

/* Selection */
static void notepad_clear_selection(void);
static void notepad_delete_selection(void);
static void notepad_copy_selection(void);
static void notepad_select_all(void);
static bool notepad_has_selection(void);
static void notepad_normalize_selection(int *sl, int *sc, int *el, int *ec);

/* Undo */
static void notepad_save_undo(void);
static void notepad_do_undo(void);
static void notepad_do_redo(void);
static void notepad_free_undo(void);
static void notepad_free_redo(void);

/* Scroll */
static void notepad_ensure_cursor_visible(void);
static void notepad_get_viewport(int *vis_cols, int *vis_lines, int *edit_x,
                                 int *edit_y, int *edit_w, int *edit_h,
                                 window_t *win);

/* File ops */
static void notepad_do_new(void);
static void notepad_do_open(void);
static void notepad_do_save(void);
static void notepad_do_save_as(void);
static void notepad_open_file(const char *name);
static void notepad_save_file(const char *name);

/* Menu */
static void notepad_menu_action(int menu, int item);

/* File dialog */
static void notepad_open_dialog(bool save_mode);
static void notepad_close_dialog(void);
static void notepad_populate_dialog(void);
static void notepad_dialog_navigate_dir(const char *dname);
static void notepad_dialog_handle_key(uint8_t scancode, char character);
static void notepad_dialog_handle_mouse(int16_t mx, int16_t my, uint8_t buttons,
                                        uint8_t prev_buttons, window_t *win);
static dlg_layout_t notepad_dialog_get_layout(window_t *win);

static void notepad_dialog_release_files(void) {
  if (app.dialog.files) {
    kfree(app.dialog.files);
    app.dialog.files = NULL;
  }
  app.dialog.file_cap = 0;
  app.dialog.file_count = 0;
}

static bool notepad_dialog_reserve_files(int needed) {
  if (needed <= 0)
    return false;
  if (app.dialog.file_cap >= needed)
    return true;

  int new_cap = app.dialog.file_cap > 0 ? app.dialog.file_cap : DLG_FILES_INIT_CAP;
  while (new_cap < needed && new_cap < DLG_FILES_MAX_CAP) {
    int doubled = new_cap * 2;
    if (doubled <= new_cap)
      break;
    new_cap = doubled;
  }
  if (new_cap < needed)
    new_cap = needed;
  if (new_cap > DLG_FILES_MAX_CAP)
    return false;

  size_t bytes = (size_t)new_cap * sizeof(file_entry_t);
  file_entry_t *buf = (file_entry_t *)kmalloc((uint32_t)bytes);
  if (!buf)
    return false;

  if (app.dialog.files && app.dialog.file_count > 0) {
    memcpy(buf, app.dialog.files,
           (size_t)app.dialog.file_count * sizeof(file_entry_t));
  }
  if (app.dialog.files)
    kfree(app.dialog.files);

  app.dialog.files = buf;
  app.dialog.file_cap = new_cap;
  return true;
}

/* Utility */

static size_t np_strlen(const char *s) {
  size_t n = 0;
  while (s[n])
    n++;
  return n;
}

static char *notepad_strdup(const char *s) {
  size_t len = np_strlen(s);
  char *p = kmalloc(len + 1);
  if (p)
    memcpy(p, s, len + 1);
  return p;
}

static void notepad_strcpy(char *dst, const char *src) {
  while ((*dst++ = *src++)) {
  }
}

static int np_strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}

static int np_strncmp(const char *a, const char *b, size_t n) {
  size_t i = 0;
  while (i < n) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];
    if (ca != cb)
      return (int)ca - (int)cb;
    if (ca == '\0')
      return 0;
    i++;
  }
  return 0;
}

static int np_strcasecmp(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a;
    char cb = *b;
    if (ca >= 'A' && ca <= 'Z')
      ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = (char)(cb - 'A' + 'a');
    if (ca != cb)
      return (int)(unsigned char)ca - (int)(unsigned char)cb;
    a++;
    b++;
  }
  return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int np_parse_hex6(const char *s, uint32_t *out) {
  uint32_t value = 0;
  if (!s || !out)
    return 0;
  if (s[0] != '#')
    return 0;
  for (int i = 1; i <= 6; i++) {
    char c = s[i];
    uint32_t v;
    if (c >= '0' && c <= '9')
      v = (uint32_t)(c - '0');
    else if (c >= 'a' && c <= 'f')
      v = (uint32_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      v = (uint32_t)(c - 'A' + 10);
    else
      return 0;
    value = (value << 4) | v;
  }
  if (s[7] != '\0' && s[7] != ' ' && s[7] != '\t')
    return 0;
  *out = value;
  return 1;
}

static const char *np_skip_spaces(const char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}

static void np_copy_limited(char *dst, const char *src, int max_len) {
  int i = 0;
  if (max_len <= 0)
    return;
  while (src && src[i] && i < max_len - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static void np_append_limited(char *dst, const char *suffix, int max_len) {
  int d = 0;
  int s = 0;
  if (!dst || !suffix || max_len <= 0)
    return;
  while (dst[d] && d < max_len - 1)
    d++;
  while (suffix[s] && d < max_len - 1) {
    dst[d++] = suffix[s++];
  }
  dst[d] = '\0';
}

static int notepad_filename_is_ctxt(const char *path) {
  if (!path)
    return 0;
  int len = (int)np_strlen(path);
  if (len < 5)
    return 0;
  return np_strcasecmp(path + len - 5, ".ctxt") == 0;
}

static int notepad_buffer_looks_ctxt(void) {
  int max_scan = app.buffer.line_count;
  if (max_scan > 64)
    max_scan = 64;

  for (int i = 0; i < max_scan; i++) {
    const char *line = app.buffer.lines[i] ? app.buffer.lines[i] : "";
    line = np_skip_spaces(line);
    if (line[0] != '>')
      continue;

    if (np_strncmp(line, ">h1", 3) == 0 || np_strncmp(line, ">h2", 3) == 0 ||
        np_strncmp(line, ">h3", 3) == 0 ||
        np_strncmp(line, ">rule", 5) == 0 ||
        np_strncmp(line, ">center", 7) == 0 ||
        np_strncmp(line, ">comment", 8) == 0 ||
        np_strncmp(line, ">color", 6) == 0 ||
        np_strncmp(line, ">text", 5) == 0 ||
        np_strncmp(line, ">style", 6) == 0 ||
        np_strncmp(line, ">box", 4) == 0 ||
        np_strncmp(line, ">endbox", 7) == 0 ||
        np_strncmp(line, ">theme", 6) == 0) {
      return 1;
    }
  }

  return 0;
}

static int notepad_toggle_ctxt_mode(window_t *win) {
  if (!app.is_ctxt_file) {
    if (!notepad_filename_is_ctxt(app.buffer.filename) &&
        !notepad_buffer_looks_ctxt()) {
      return 0;
    }
    app.is_ctxt_file = true;
  }

  if (app.render_mode) {
    app.render_mode = false;
    app.buffer.scroll_x = 0;
    app.buffer.scroll_y = 0;
    app.ctxt_hover_link = -1;
  } else {
    app.render_mode = true;
    app.ctxt_theme_light = false;
    app.ctxt_style_mask = 0;
    app.ctxt_scroll_px = 0;
    app.ctxt_scroll_x_px = 0;
    app.ctxt_hover_link = -1;
    notepad_ctxt_parse();
  }

  if (win)
    win->flags |= WINDOW_FLAG_DIRTY;
  return 1;
}

enum {
  CTXT_LINE_BODY = 0,
  CTXT_LINE_H1,
  CTXT_LINE_H2,
  CTXT_LINE_H3,
  CTXT_LINE_RULE,
  CTXT_LINE_CENTER,
  CTXT_LINE_BOX_START,
  CTXT_LINE_BOX_END,
  CTXT_LINE_COMMENT
};

enum {
  CTXT_STYLE_BG        = 1u << 0,
  CTXT_STYLE_H1        = 1u << 1,
  CTXT_STYLE_H2        = 1u << 2,
  CTXT_STYLE_H3        = 1u << 3,
  CTXT_STYLE_BODY      = 1u << 4,
  CTXT_STYLE_RULE      = 1u << 5,
  CTXT_STYLE_BOX_BG    = 1u << 6,
  CTXT_STYLE_BOX_TEXT  = 1u << 7,
  CTXT_STYLE_LINK      = 1u << 8,
  CTXT_STYLE_LINK_HOV  = 1u << 9
};

typedef struct {
  uint32_t bg;
  uint32_t h1;
  uint32_t h2;
  uint32_t h3;
  uint32_t body;
  uint32_t rule;
  uint32_t box_bg;
  uint32_t box_text;
} ctxt_theme_t;

static ctxt_theme_t notepad_ctxt_theme(bool light) {
  ctxt_theme_t t;
  if (light) {
    t.bg = 0xF5F5F0;
    t.h1 = 0xCC2266;
    t.h2 = 0x2255AA;
    t.h3 = 0xAA6600;
    t.body = 0x111111;
    t.rule = 0xAAAAAA;
    t.box_bg = 0xE8EEF8;
    t.box_text = 0x223355;
  } else {
    t.bg = 0x0D0D1A;
    t.h1 = 0xFF88CC;
    t.h2 = 0x88AAFF;
    t.h3 = 0xFFDD88;
    t.body = 0xDDDDDD;
    t.rule = 0x445566;
    t.box_bg = 0x1A1A3A;
    t.box_text = 0xAADDFF;
  }
  return t;
}

static void notepad_ctxt_apply_style_overrides(ctxt_theme_t *t,
                                               uint32_t *link_color,
                                               uint32_t *link_hover_color) {
  if (app.ctxt_style_mask & CTXT_STYLE_BG)
    t->bg = app.ctxt_style_bg;
  if (app.ctxt_style_mask & CTXT_STYLE_H1)
    t->h1 = app.ctxt_style_h1;
  if (app.ctxt_style_mask & CTXT_STYLE_H2)
    t->h2 = app.ctxt_style_h2;
  if (app.ctxt_style_mask & CTXT_STYLE_H3)
    t->h3 = app.ctxt_style_h3;
  if (app.ctxt_style_mask & CTXT_STYLE_BODY)
    t->body = app.ctxt_style_body;
  if (app.ctxt_style_mask & CTXT_STYLE_RULE)
    t->rule = app.ctxt_style_rule;
  if (app.ctxt_style_mask & CTXT_STYLE_BOX_BG)
    t->box_bg = app.ctxt_style_box_bg;
  if (app.ctxt_style_mask & CTXT_STYLE_BOX_TEXT)
    t->box_text = app.ctxt_style_box_text;
  if (app.ctxt_style_mask & CTXT_STYLE_LINK)
    *link_color = app.ctxt_style_link;
  if (app.ctxt_style_mask & CTXT_STYLE_LINK_HOV)
    *link_hover_color = app.ctxt_style_link_hover;
}

static int notepad_ctxt_line_height(uint8_t type) {
  if (type == CTXT_LINE_H1 || type == CTXT_LINE_H2)
    return FONT_H * 2 + 2;
  if (type == CTXT_LINE_H3)
    return FONT_H + 1;
  if (type == CTXT_LINE_RULE)
    return 8;
  if (type == CTXT_LINE_BOX_START || type == CTXT_LINE_BOX_END ||
      type == CTXT_LINE_COMMENT)
    return 2;
  return FONT_H + 1;
}

static int notepad_ctxt_line_scale(uint8_t type) {
  if (type == CTXT_LINE_H1 || type == CTXT_LINE_H2)
    return 2;
  return 1;
}

static int notepad_ctxt_max_scroll(window_t *win) {
  int vis_h = 0;
  int max_scroll = 0;
  notepad_get_viewport(NULL, NULL, NULL, NULL, NULL, &vis_h, win);
  max_scroll = app.ctxt_content_height - vis_h;
  if (max_scroll < 0)
    max_scroll = 0;
  return max_scroll;
}

static int notepad_ctxt_max_scroll_x(window_t *win) {
  int edit_w = 0;
  int view_w = 0;
  int max_scroll = 0;
  notepad_get_viewport(NULL, NULL, NULL, NULL, &edit_w, NULL, win);
  view_w = edit_w - 16;
  if (view_w < 8)
    view_w = 8;
  max_scroll = app.ctxt_content_width - view_w;
  if (max_scroll < 0)
    max_scroll = 0;
  return max_scroll;
}

static void notepad_ctxt_parse(void) {
  app.ctxt_line_count = 0;
  app.ctxt_content_height = 8;
  app.ctxt_content_width = 8;

  uint32_t current_fg = 0;
  bool box_active = false;
  uint32_t box_bg = 0;
  app.ctxt_style_mask = 0;

  for (int i = 0; i < app.buffer.line_count && i < NOTEPAD_MAX_LINES; i++) {
    const char *src = app.buffer.lines[i] ? app.buffer.lines[i] : "";
    uint8_t type = CTXT_LINE_BODY;
    uint32_t color = current_fg;
    uint32_t bg_color = box_active ? box_bg : 0;
    const char *text = src;

    if (src[0] == '>') {
      if (np_strncmp(src, ">h1", 3) == 0) {
        type = CTXT_LINE_H1;
        text = np_skip_spaces(src + 3);
      } else if (np_strncmp(src, ">h2", 3) == 0) {
        type = CTXT_LINE_H2;
        text = np_skip_spaces(src + 3);
      } else if (np_strncmp(src, ">h3", 3) == 0) {
        type = CTXT_LINE_H3;
        text = np_skip_spaces(src + 3);
      } else if (np_strncmp(src, ">rule", 5) == 0) {
        type = CTXT_LINE_RULE;
        text = "";
      } else if (np_strncmp(src, ">center", 7) == 0) {
        type = CTXT_LINE_CENTER;
        text = np_skip_spaces(src + 7);
      } else if (np_strncmp(src, ">comment", 8) == 0) {
        type = CTXT_LINE_COMMENT;
        text = "";
      } else if (np_strncmp(src, ">color", 6) == 0) {
        const char *arg = np_skip_spaces(src + 6);
        uint32_t parsed;
        if (*arg == '\0') {
          current_fg = 0;
        } else if (np_parse_hex6(arg, &parsed)) {
          current_fg = parsed;
        }
        type = CTXT_LINE_COMMENT;
        text = "";
      } else if (np_strncmp(src, ">text", 5) == 0) {
        const char *arg = np_skip_spaces(src + 5);
        uint32_t parsed;
        if (*arg == '\0') {
          current_fg = 0;
        } else if (np_parse_hex6(arg, &parsed)) {
          current_fg = parsed;
        }
        type = CTXT_LINE_COMMENT;
        text = "";
      } else if (np_strncmp(src, ">style", 6) == 0) {
        const char *arg = np_skip_spaces(src + 6);
        if (np_strncmp(arg, "reset", 5) == 0) {
          app.ctxt_style_mask = 0;
        } else {
          char key[16];
          int ki = 0;
          while (*arg && *arg != ' ' && *arg != '\t' && ki < 15) {
            key[ki++] = *arg++;
          }
          key[ki] = '\0';
          arg = np_skip_spaces(arg);

          uint32_t parsed;
          if (np_parse_hex6(arg, &parsed)) {
            if (np_strcasecmp(key, "bg") == 0) {
              app.ctxt_style_bg = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_BG;
            } else if (np_strcasecmp(key, "h1") == 0) {
              app.ctxt_style_h1 = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_H1;
            } else if (np_strcasecmp(key, "h2") == 0) {
              app.ctxt_style_h2 = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_H2;
            } else if (np_strcasecmp(key, "h3") == 0) {
              app.ctxt_style_h3 = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_H3;
            } else if (np_strcasecmp(key, "body") == 0) {
              app.ctxt_style_body = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_BODY;
            } else if (np_strcasecmp(key, "rule") == 0) {
              app.ctxt_style_rule = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_RULE;
            } else if (np_strcasecmp(key, "box") == 0 ||
                       np_strcasecmp(key, "boxbg") == 0) {
              app.ctxt_style_box_bg = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_BOX_BG;
            } else if (np_strcasecmp(key, "boxtext") == 0) {
              app.ctxt_style_box_text = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_BOX_TEXT;
            } else if (np_strcasecmp(key, "link") == 0) {
              app.ctxt_style_link = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_LINK;
            } else if (np_strcasecmp(key, "linkhover") == 0 ||
                       np_strcasecmp(key, "hover") == 0) {
              app.ctxt_style_link_hover = parsed;
              app.ctxt_style_mask |= CTXT_STYLE_LINK_HOV;
            }
          }
        }
        type = CTXT_LINE_COMMENT;
        text = "";
      } else if (np_strncmp(src, ">box", 4) == 0) {
        const char *arg = np_skip_spaces(src + 4);
        uint32_t parsed;
        if (np_parse_hex6(arg, &parsed)) {
          box_bg = parsed;
        } else {
          ctxt_theme_t theme = notepad_ctxt_theme(app.ctxt_theme_light);
          box_bg = theme.box_bg;
        }
        box_active = true;
        type = CTXT_LINE_BOX_START;
        text = "";
      } else if (np_strncmp(src, ">endbox", 7) == 0) {
        type = CTXT_LINE_BOX_END;
        text = "";
        box_active = false;
      } else if (np_strncmp(src, ">theme", 6) == 0) {
        const char *arg = np_skip_spaces(src + 6);
        if (np_strncmp(arg, "light", 5) == 0)
          app.ctxt_theme_light = true;
        else if (np_strncmp(arg, "dark", 4) == 0)
          app.ctxt_theme_light = false;
        type = CTXT_LINE_COMMENT;
        text = "";
      }
    }

    app.ctxt_lines[app.ctxt_line_count].type = type;
    app.ctxt_lines[app.ctxt_line_count].color = color;
    app.ctxt_lines[app.ctxt_line_count].bg_color = bg_color;
    np_copy_limited(app.ctxt_lines[app.ctxt_line_count].text, text,
                    NOTEPAD_MAX_LINE_LEN);
    app.ctxt_line_count++;
  }

  for (int i = 0; i < app.ctxt_line_count; i++) {
    uint8_t t = app.ctxt_lines[i].type;
    int scale = notepad_ctxt_line_scale(t);
    int line_w = notepad_ctxt_text_width(app.ctxt_lines[i].text, scale);
    if (app.ctxt_lines[i].bg_color != 0)
      line_w += 4;
    if (line_w > app.ctxt_content_width)
      app.ctxt_content_width = line_w;
    app.ctxt_content_height += notepad_ctxt_line_height(t);
  }
}

static uint32_t notepad_ctxt_inline_color(const char *spec, uint32_t fallback) {
  uint32_t parsed;
  if (np_parse_hex6(spec, &parsed))
    return parsed;
  return fallback;
}

static int np_ends_with_case(const char *s, const char *suffix) {
  int sl = (int)np_strlen(s);
  int xl = (int)np_strlen(suffix);
  if (xl > sl)
    return 0;
  return np_strcasecmp(s + sl - xl, suffix) == 0;
}

static int np_find_last_slash(const char *s) {
  int idx = -1;
  for (int i = 0; s && s[i]; i++) {
    if (s[i] == '/')
      idx = i;
  }
  return idx;
}

static int np_is_space_char(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int np_is_link_break(char c) {
  return np_is_space_char(c) || c == ',' || c == ';' || c == ')' || c == ']' ||
         c == '}' || c == '"' || c == '\'';
}

static int np_looks_like_link_target(const char *tok) {
  if (!tok || !tok[0])
    return 0;
  if (tok[0] == '/' || (tok[0] == '.' && tok[1] == '/'))
    return 1;
  for (int i = 0; tok[i]; i++) {
    if (tok[i] == '/')
      return 1;
  }
  if (np_ends_with_case(tok, ".ctxt") || np_ends_with_case(tok, ".txt"))
    return 1;
  return 0;
}

static void notepad_ctxt_resolve_link(const char *target, char *out) {
  if (!target || !target[0]) {
    out[0] = '\0';
    return;
  }

  if (target[0] == '/') {
    np_copy_limited(out, target, VFS_MAX_PATH);
    return;
  }

  char base[VFS_MAX_PATH];
  np_copy_limited(base, app.buffer.filename, VFS_MAX_PATH);
  int slash = np_find_last_slash(base);
  if (slash < 0) {
    base[0] = '/';
    base[1] = '\0';
  } else if (slash == 0) {
    base[1] = '\0';
  } else {
    base[slash] = '\0';
  }

  char joined[VFS_MAX_PATH];
  int j = 0;
  for (int i = 0; base[i] && j < VFS_MAX_PATH - 1; i++)
    joined[j++] = base[i];
  if (j > 0 && joined[j - 1] != '/' && j < VFS_MAX_PATH - 1)
    joined[j++] = '/';
  for (int i = 0; target[i] && j < VFS_MAX_PATH - 1; i++)
    joined[j++] = target[i];
  joined[j] = '\0';

  char tmp[VFS_MAX_PATH];
  int ti = 0;
  int stack[64];
  int top = 0;
  int i = 0;
  if (joined[0] != '/')
    tmp[ti++] = '/';

  while (joined[i]) {
    while (joined[i] == '/')
      i++;
    if (!joined[i])
      break;
    int start = i;
    while (joined[i] && joined[i] != '/')
      i++;
    int len = i - start;

    if (len == 1 && joined[start] == '.') {
      continue;
    }
    if (len == 2 && joined[start] == '.' && joined[start + 1] == '.') {
      if (top > 0) {
        ti = stack[top - 1];
        top--;
      }
      continue;
    }

    if (ti == 0 || tmp[ti - 1] != '/') {
      if (ti < VFS_MAX_PATH - 1)
        tmp[ti++] = '/';
    }
    if (top < 64)
      stack[top++] = ti - 1;
    for (int k = 0; k < len && ti < VFS_MAX_PATH - 1; k++) {
      tmp[ti++] = joined[start + k];
    }
  }

  if (ti == 0) {
    tmp[ti++] = '/';
  }
  tmp[ti] = '\0';
  np_copy_limited(out, tmp, VFS_MAX_PATH);
}

static int notepad_ctxt_hit_link(int16_t mx, int16_t my) {
  for (int i = 0; i < app.ctxt_link_count; i++) {
    int16_t lx = app.ctxt_links[i].x;
    int16_t ly = app.ctxt_links[i].y;
    uint16_t lw = app.ctxt_links[i].w;
    uint16_t lh = app.ctxt_links[i].h;
    if (mx >= lx && mx < lx + (int16_t)lw && my >= ly && my < ly + (int16_t)lh)
      return i;
  }
  return -1;
}

static int notepad_try_open_path(const char *path) {
  int fd;
  if (!path || !path[0])
    return 0;
  fd = vfs_open(path, O_RDONLY);
  if (fd < 0)
    return 0;
  vfs_close(fd);
  notepad_open_file(path);
  return 1;
}

static void notepad_ctxt_open_link(const char *target) {
  char resolved[VFS_MAX_PATH];
  static const char *ext_variants[] = {".ctxt", ".CTXT", ".txt", ".TXT"};
  notepad_ctxt_resolve_link(target, resolved);

  if (notepad_try_open_path(resolved))
    return;

  char alt[VFS_MAX_PATH];
  int len = (int)np_strlen(resolved);
  int dot = -1;
  for (int i = 0; i < len; i++) {
    if (resolved[i] == '.')
      dot = i;
  }

  if (dot >= 0 && dot < VFS_MAX_PATH - 6 &&
      (np_ends_with_case(resolved, ".txt") ||
       np_ends_with_case(resolved, ".ctxt"))) {
    for (int e = 0; e < 4; e++) {
      np_copy_limited(alt, resolved, VFS_MAX_PATH);
      alt[dot] = '\0';
      if ((int)np_strlen(alt) + (int)np_strlen(ext_variants[e]) >=
          VFS_MAX_PATH)
        continue;
      np_append_limited(alt, ext_variants[e], VFS_MAX_PATH);
      if (notepad_try_open_path(alt))
        return;
    }
  }

  if (dot < 0 && len < VFS_MAX_PATH - 6) {
    for (int e = 0; e < 4; e++) {
      np_copy_limited(alt, resolved, VFS_MAX_PATH);
      if ((int)np_strlen(alt) + (int)np_strlen(ext_variants[e]) >=
          VFS_MAX_PATH)
        continue;
      np_append_limited(alt, ext_variants[e], VFS_MAX_PATH);
      if (notepad_try_open_path(alt))
      return;
    }
  }
}

static int notepad_ctxt_text_width(const char *text, int scale) {
  int width = 0;
  int char_w = FONT_W * scale;
  const char *p = text;
  while (*p) {
    if (*p == '[') {
      const char *close = p + 1;
      while (*close && *close != ']')
        close++;
      if (*close == ']' && close[1] == '(') {
        const char *r = close + 2;
        while (*r && *r != ')')
          r++;
        if (*r == ')') {
          width += (int)(close - (p + 1)) * char_w;
          p = r + 1;
          continue;
        }
      }
    }

    if (*p == '{') {
      const char *end = p + 1;
      while (*end && *end != '}')
        end++;
      if (*end == '}') {
        p = end + 1;
        continue;
      }
    }
    width += char_w;
    p++;
  }
  return width;
}

static void notepad_ctxt_draw_text(int16_t x, int16_t y, const char *text,
                                   uint32_t base_color, int scale,
                                   int16_t max_x, int line_h,
                                   uint32_t link_color,
                                   uint32_t link_hover_color,
                                   uint32_t code_fg,
                                   uint32_t code_bg) {
  int16_t cx = x;
  uint32_t cur_color = base_color;
  uint32_t cur_bg = 0;
  int bold = 0;
  int underline = 0;
  int code_mode = 0;
  const char *p = text;
  int char_w = FONT_W * scale;

  while (*p) {
    if (!np_is_link_break(*p)) {
      const char *end = p;
      while (*end && !np_is_link_break(*end) && *end != '{')
        end++;
      int tok_len = (int)(end - p);
      if (tok_len > 0 && tok_len < VFS_MAX_PATH) {
        char tok[VFS_MAX_PATH];
        for (int i = 0; i < tok_len; i++)
          tok[i] = p[i];
        tok[tok_len] = '\0';
        if (np_looks_like_link_target(tok)) {
          int cur_link_index = app.ctxt_link_count;
          bool is_hover = (app.ctxt_hover_link == cur_link_index);
          uint32_t draw_col = is_hover ? link_hover_color : link_color;
          int start_x = cx;
          int i = 0;
          for (; i < tok_len && cx + char_w <= max_x; i++) {
            if (scale == 1)
              gfx_draw_char(cx, y, tok[i], draw_col);
            else
              gfx_draw_char_scaled(cx, y, tok[i], draw_col, scale);
            cx = (int16_t)(cx + char_w);
          }

          int link_w = cx - start_x;
          if (link_w > 0) {
            gfx_draw_hline((int16_t)start_x,
                           (int16_t)(y + (line_h > 2 ? line_h - 2 : 0)),
                           (uint16_t)link_w, draw_col);
            if (app.ctxt_link_count < 256) {
              app.ctxt_links[app.ctxt_link_count].x = (int16_t)start_x;
              app.ctxt_links[app.ctxt_link_count].y = y;
              app.ctxt_links[app.ctxt_link_count].w = (uint16_t)link_w;
              app.ctxt_links[app.ctxt_link_count].h = (uint16_t)line_h;
              np_copy_limited(app.ctxt_links[app.ctxt_link_count].target, tok,
                              VFS_MAX_PATH);
              app.ctxt_link_count++;
            }
          }

          p = end;
          continue;
        }
      }
    }

    if (*p == '[') {
      const char *close = p + 1;
      while (*close && *close != ']')
        close++;
      if (*close == ']' && close[1] == '(') {
        const char *r = close + 2;
        while (*r && *r != ')')
          r++;
        if (*r == ')') {
          int label_len = (int)(close - (p + 1));
          int target_len = (int)(r - (close + 2));
          if (label_len > 0 && target_len > 0) {
            char target[VFS_MAX_PATH];
            int tl = 0;
            const char *ts = close + 2;
            while (tl < target_len && tl < VFS_MAX_PATH - 1) {
              target[tl] = ts[tl];
              tl++;
            }
            target[tl] = '\0';

            int start_x = cx;
            int li = 0;
            int cur_link_index = app.ctxt_link_count;
            bool is_hover = (app.ctxt_hover_link == cur_link_index);
            uint32_t draw_col = is_hover ? link_hover_color : link_color;
            for (; li < label_len && cx + char_w <= max_x; li++) {
              if (scale == 1)
                gfx_draw_char(cx, y, p[1 + li], draw_col);
              else
                gfx_draw_char_scaled(cx, y, p[1 + li], draw_col, scale);
              cx = (int16_t)(cx + char_w);
            }

            int link_w = cx - start_x;
            if (link_w > 0) {
              uint32_t ucol = is_hover ? link_hover_color : link_color;
              gfx_draw_hline((int16_t)start_x,
                             (int16_t)(y + (line_h > 2 ? line_h - 2 : 0)),
                             (uint16_t)link_w, ucol);

              if (app.ctxt_link_count < 256) {
                app.ctxt_links[app.ctxt_link_count].x = (int16_t)start_x;
                app.ctxt_links[app.ctxt_link_count].y = y;
                app.ctxt_links[app.ctxt_link_count].w = (uint16_t)link_w;
                app.ctxt_links[app.ctxt_link_count].h = (uint16_t)line_h;
                np_copy_limited(app.ctxt_links[app.ctxt_link_count].target,
                                target, VFS_MAX_PATH);
                app.ctxt_link_count++;
              }
            }

            p = r + 1;
            continue;
          }
        }
      }
    }

    if (*p == '{') {
      const char *end = p + 1;
      while (*end && *end != '}')
        end++;
      if (*end == '}') {
        int len = (int)(end - (p + 1));
        if (len == 0) {
          cur_color = base_color;
          cur_bg = 0;
          bold = 0;
          underline = 0;
          code_mode = 0;
          p = end + 1;
          continue;
        }
        if (len >= 9 && p[1] == 'c' && p[2] == ':' && p[3] == '#') {
          char hex[8];
          int hi = 0;
          for (int k = 3; k < 10 && hi < 7; k++) {
            hex[hi++] = p[k];
          }
          hex[hi] = '\0';
          cur_color = notepad_ctxt_inline_color(hex, cur_color);
          p = end + 1;
          continue;
        }
        if (len >= 10 && p[1] == 'b' && p[2] == 'g' && p[3] == ':' &&
            p[4] == '#') {
          char hex[8];
          int hi = 0;
          for (int k = 4; k < 11 && hi < 7; k++) {
            hex[hi++] = p[k];
          }
          hex[hi] = '\0';
          cur_bg = notepad_ctxt_inline_color(hex, cur_bg);
          p = end + 1;
          continue;
        }
        if (len == 1 && p[1] == 'b') {
          bold = 1;
          p = end + 1;
          continue;
        }
        if (len == 2 && p[1] == '/' && p[2] == 'b') {
          bold = 0;
          p = end + 1;
          continue;
        }
        if (len == 1 && p[1] == 'u') {
          underline = 1;
          p = end + 1;
          continue;
        }
        if (len == 2 && p[1] == '/' && p[2] == 'u') {
          underline = 0;
          p = end + 1;
          continue;
        }
        if (len == 4 && p[1] == 'c' && p[2] == 'o' && p[3] == 'd' &&
            p[4] == 'e') {
          code_mode = 1;
          cur_bg = code_bg;
          cur_color = code_fg;
          p = end + 1;
          continue;
        }
        if (len == 5 && p[1] == '/' && p[2] == 'c' && p[3] == 'o' &&
            p[4] == 'd' && p[5] == 'e') {
          code_mode = 0;
          cur_bg = 0;
          cur_color = base_color;
          p = end + 1;
          continue;
        }
      }
    }

    if (cx + char_w > max_x)
      break;

    if (cur_bg != 0) {
      gfx_fill_rect(cx, y, (uint16_t)char_w, (uint16_t)line_h, cur_bg);
    }

    if (scale == 1)
      gfx_draw_char(cx, y, *p, cur_color);
    else
      gfx_draw_char_scaled(cx, y, *p, cur_color, scale);

    if (bold) {
      if (scale == 1)
        gfx_draw_char((int16_t)(cx + 1), y, *p, cur_color);
      else
        gfx_draw_char_scaled((int16_t)(cx + 1), y, *p, cur_color, scale);
    }

    if (underline || code_mode) {
      int ul_y = y + (line_h > 2 ? line_h - 2 : 0);
      gfx_draw_hline(cx, (int16_t)ul_y, (uint16_t)char_w, cur_color);
    }

    cx = (int16_t)(cx + char_w);
    p++;
  }
}

static void notepad_draw_ctxt_area(window_t *win) {
  int edit_x, edit_y, edit_w, edit_h;
  notepad_get_viewport(NULL, NULL, &edit_x, &edit_y, &edit_w, &edit_h, win);

  ctxt_theme_t theme = notepad_ctxt_theme(app.ctxt_theme_light);
  uint32_t link_color = app.ctxt_theme_light ? 0x1D4ED8 : 0x66B3FF;
  uint32_t link_hover_color = app.ctxt_theme_light ? 0x0A3AA9 : 0x9ED0FF;
  notepad_ctxt_apply_style_overrides(&theme, &link_color, &link_hover_color);
  uint32_t code_fg = theme.box_text;
  uint32_t code_bg = theme.box_bg;

  app.ctxt_link_count = 0;

  gfx_fill_rect((int16_t)edit_x, (int16_t)edit_y, (uint16_t)edit_w,
                (uint16_t)edit_h, theme.bg);

  int margin = 8;
  int content_x = edit_x + margin;
  int content_w = edit_w - margin * 2;
  if (content_w < 8)
    content_w = 8;

  int y = edit_y + 4 - app.ctxt_scroll_px;

  for (int i = 0; i < app.ctxt_line_count; i++) {
    uint8_t t = app.ctxt_lines[i].type;
    const char *text = app.ctxt_lines[i].text;
    int line_h = notepad_ctxt_line_height(t);
    int scale = notepad_ctxt_line_scale(t);
    uint32_t fg = theme.body;

    if (t == CTXT_LINE_COMMENT) {
      y += line_h;
      continue;
    }
    if (t == CTXT_LINE_BOX_START || t == CTXT_LINE_BOX_END) {
      y += line_h;
      continue;
    }

    if (t == CTXT_LINE_H1) {
      fg = theme.h1;
    } else if (t == CTXT_LINE_H2) {
      fg = theme.h2;
    } else if (t == CTXT_LINE_H3) {
      fg = theme.h3;
    } else if (t == CTXT_LINE_RULE) {
      fg = theme.rule;
    } else {
      if (app.ctxt_lines[i].bg_color != 0)
        fg = theme.box_text;
      if (app.ctxt_lines[i].color != 0)
        fg = app.ctxt_lines[i].color;
    }

    if (y + line_h < edit_y) {
      y += line_h;
      continue;
    }
    if (y >= edit_y + edit_h)
      break;
    if (y < edit_y || y + line_h > edit_y + edit_h) {
      y += line_h;
      continue;
    }

    if (app.ctxt_lines[i].bg_color != 0 && t != CTXT_LINE_RULE) {
      gfx_fill_rect((int16_t)content_x, (int16_t)y, (uint16_t)content_w,
                    (uint16_t)line_h, app.ctxt_lines[i].bg_color);
      gfx_draw_vline((int16_t)content_x, (int16_t)y, (uint16_t)line_h,
                     theme.rule);
    }

    if (t == CTXT_LINE_RULE) {
      gfx_draw_hline((int16_t)content_x, (int16_t)(y + 3), (uint16_t)content_w,
                     fg);
    } else {
      int16_t tx = (int16_t)content_x;
      if (app.ctxt_lines[i].bg_color != 0)
        tx = (int16_t)(tx + 4);
      if (t == CTXT_LINE_CENTER) {
        int tw = notepad_ctxt_text_width(text, scale);
        if (tw < content_w)
          tx = (int16_t)(content_x + (content_w - tw) / 2);
      }
      tx = (int16_t)(tx - app.ctxt_scroll_x_px);
      notepad_ctxt_draw_text(tx, (int16_t)y, text, fg, scale,
                 (int16_t)(content_x + content_w), line_h,
                 link_color, link_hover_color, code_fg, code_bg);
    }

    y += line_h;
  }
}

/* Buffer management */

static void notepad_init_buffer(void) {
  memset(&app.buffer, 0, sizeof(app.buffer));
  app.buffer.lines[0] = notepad_strdup("");
  app.buffer.line_count = 1;
  app.buffer.cursor_line = 0;
  app.buffer.cursor_col = 0;
  app.buffer.scroll_x = 0;
  app.buffer.scroll_y = 0;
  app.buffer.modified = false;
  app.buffer.filename[0] = '\0';
}

static void notepad_free_buffer(void) {
  for (int i = 0; i < app.buffer.line_count; i++) {
    if (app.buffer.lines[i]) {
      kfree(app.buffer.lines[i]);
      app.buffer.lines[i] = NULL;
    }
  }
  app.buffer.line_count = 0;
}

static void notepad_ensure_line(int idx) {
  if (idx < 0 || idx >= NOTEPAD_MAX_LINES)
    return;
  if (!app.buffer.lines[idx]) {
    app.buffer.lines[idx] = notepad_strdup("");
  }
}

static int notepad_max_line_width(void) {
  int max_w = 0;
  for (int i = 0; i < app.buffer.line_count; i++) {
    if (app.buffer.lines[i]) {
      int len = (int)np_strlen(app.buffer.lines[i]);
      if (len > max_w)
        max_w = len;
    }
  }
  return max_w;
}

/* Viewport calculation */

static void notepad_get_viewport(int *vis_cols, int *vis_lines, int *edit_x,
                                 int *edit_y, int *edit_w, int *edit_h,
                                 window_t *win) {
  /* Content area: inside window border, below titlebar+menubar,
   * above status bar, minus scrollbar areas */
  int cx = (int)win->x + 2;
  int cy = (int)win->y + TITLEBAR_H + 1 + MENUBAR_H;
  int cw = (int)win->width - 4 - VSCROLL_W;
  int ch =
      (int)win->height - TITLEBAR_H - 2 - MENUBAR_H - STATUSBAR_H - HSCROLL_H;

  if (cw < 8)
    cw = 8;
  if (ch < 8)
    ch = 8;

  int scale = app.font_scale;
  if (scale < 1)
    scale = 1;

  if (vis_cols)
    *vis_cols = cw / (FONT_W * scale);
  if (vis_lines)
    *vis_lines = ch / (FONT_H * scale);
  if (edit_x)
    *edit_x = cx;
  if (edit_y)
    *edit_y = cy;
  if (edit_w)
    *edit_w = cw;
  if (edit_h)
    *edit_h = ch;
}

static void notepad_ensure_cursor_visible(void) {
  int vis_cols, vis_lines;
  notepad_get_viewport(&vis_cols, &vis_lines, NULL, NULL, NULL, NULL,
                       gui_get_window(notepad_wid));

  /* Vertical */
  if (app.buffer.cursor_line < app.buffer.scroll_y) {
    app.buffer.scroll_y = app.buffer.cursor_line;
  }
  if (app.buffer.cursor_line >= app.buffer.scroll_y + vis_lines) {
    app.buffer.scroll_y = app.buffer.cursor_line - vis_lines + 1;
  }

  /* Horizontal */
  if (app.buffer.cursor_col < app.buffer.scroll_x) {
    app.buffer.scroll_x = app.buffer.cursor_col;
  }
  if (app.buffer.cursor_col >= app.buffer.scroll_x + vis_cols) {
    app.buffer.scroll_x = app.buffer.cursor_col - vis_cols + 1;
  }
}

/* Selection */

static void notepad_clear_selection(void) {
  app.selection.active = false;
  app.selection.dragging = false;
}

static bool notepad_has_selection(void) {
  if (!app.selection.active)
    return false;
  return (app.selection.start_line != app.selection.end_line ||
          app.selection.start_col != app.selection.end_col);
}

static void notepad_normalize_selection(int *sl, int *sc, int *el, int *ec) {
  int s_l = app.selection.start_line;
  int s_c = app.selection.start_col;
  int e_l = app.selection.end_line;
  int e_c = app.selection.end_col;

  if (s_l > e_l || (s_l == e_l && s_c > e_c)) {
    int tmp;
    tmp = s_l;
    s_l = e_l;
    e_l = tmp;
    tmp = s_c;
    s_c = e_c;
    e_c = tmp;
  }
  *sl = s_l;
  *sc = s_c;
  *el = e_l;
  *ec = e_c;
}

static void notepad_copy_selection(void) {
  if (!notepad_has_selection())
    return;

  int sl, sc, el, ec;
  notepad_normalize_selection(&sl, &sc, &el, &ec);

  char buf[CLIPBOARD_MAX_SIZE];
  int pos = 0;

  for (int line = sl; line <= el && pos < CLIPBOARD_MAX_SIZE - 2; line++) {
    const char *text = app.buffer.lines[line];
    if (!text)
      text = "";
    int len = (int)np_strlen(text);

    int col_start = (line == sl) ? sc : 0;
    int col_end = (line == el) ? ec : len;
    if (col_start > len)
      col_start = len;
    if (col_end > len)
      col_end = len;

    for (int c = col_start; c < col_end && pos < CLIPBOARD_MAX_SIZE - 2; c++) {
      buf[pos++] = text[c];
    }

    if (line < el && pos < CLIPBOARD_MAX_SIZE - 2) {
      buf[pos++] = '\n';
    }
  }
  buf[pos] = '\0';
  clipboard_copy(buf, pos);
}

static void notepad_delete_selection(void) {
  if (!notepad_has_selection())
    return;

  notepad_save_undo();

  int sl, sc, el, ec;
  notepad_normalize_selection(&sl, &sc, &el, &ec);

  if (sl == el) {
    /* Single line: remove characters [sc..ec) */
    char *line = app.buffer.lines[sl];
    int len = (int)np_strlen(line);
    if (ec > len)
      ec = len;
    /* Shift remaining chars left */
    for (int i = sc; i + (ec - sc) <= len; i++) {
      line[i] = line[i + (ec - sc)];
    }
  } else {
    /* Multi-line: merge first and last, remove middle lines */
    char *first = app.buffer.lines[sl];
    char *last = app.buffer.lines[el];
    int first_len = (int)np_strlen(first);
    int last_len = (int)np_strlen(last);

    if (sc > first_len)
      sc = first_len;
    if (ec > last_len)
      ec = last_len;

    /* Build merged line: first[0..sc) + last[ec..] */
    int new_len = sc + (last_len - ec);
    if (new_len >= NOTEPAD_MAX_LINE_LEN)
      new_len = NOTEPAD_MAX_LINE_LEN - 1;

    char *merged = kmalloc((size_t)(new_len + 1));
    if (merged) {
      memcpy(merged, first, (size_t)sc);
      int tail = last_len - ec;
      if (sc + tail > NOTEPAD_MAX_LINE_LEN - 1)
        tail = NOTEPAD_MAX_LINE_LEN - 1 - sc;
      memcpy(merged + sc, last + ec, (size_t)tail);
      merged[sc + tail] = '\0';
    }

    /* Free lines sl..el */
    for (int i = sl; i <= el; i++) {
      if (app.buffer.lines[i])
        kfree(app.buffer.lines[i]);
    }

    /* Put merged line at sl */
    app.buffer.lines[sl] = merged;

    /* Shift remaining lines up */
    int removed = el - sl;
    for (int i = sl + 1; i + removed < app.buffer.line_count; i++) {
      app.buffer.lines[i] = app.buffer.lines[i + removed];
    }
    for (int i = app.buffer.line_count - removed; i < app.buffer.line_count;
         i++) {
      app.buffer.lines[i] = NULL;
    }
    app.buffer.line_count -= removed;
  }

  app.buffer.cursor_line = sl;
  app.buffer.cursor_col = sc;
  app.buffer.modified = true;
  notepad_clear_selection();
}

static void notepad_select_all(void) {
  app.selection.active = true;
  app.selection.start_line = 0;
  app.selection.start_col = 0;
  app.selection.end_line = app.buffer.line_count - 1;
  app.selection.end_col =
      (int)np_strlen(app.buffer.lines[app.buffer.line_count - 1]
                         ? app.buffer.lines[app.buffer.line_count - 1]
                         : "");
  app.buffer.cursor_line = app.selection.end_line;
  app.buffer.cursor_col = app.selection.end_col;
}

/* Undo / Redo (single-level deep copy) */

static void notepad_free_undo(void) {
  for (int i = 0; i < app.undo_line_count; i++) {
    if (app.undo_lines[i]) {
      kfree(app.undo_lines[i]);
      app.undo_lines[i] = NULL;
    }
  }
  app.undo_line_count = 0;
  app.undo_available = false;
}

static void notepad_free_redo(void) {
  for (int i = 0; i < app.redo_line_count; i++) {
    if (app.redo_lines[i]) {
      kfree(app.redo_lines[i]);
      app.redo_lines[i] = NULL;
    }
  }
  app.redo_line_count = 0;
  app.redo_available = false;
}

static void notepad_save_undo(void) {
  notepad_free_undo();
  notepad_free_redo();

  for (int i = 0; i < app.buffer.line_count && i < NOTEPAD_MAX_LINES; i++) {
    app.undo_lines[i] =
        notepad_strdup(app.buffer.lines[i] ? app.buffer.lines[i] : "");
  }
  app.undo_line_count = app.buffer.line_count;
  app.undo_cursor_line = app.buffer.cursor_line;
  app.undo_cursor_col = app.buffer.cursor_col;
  app.undo_available = true;
}

static void notepad_do_undo(void) {
  if (!app.undo_available)
    return;

  /* Save current state to redo */
  notepad_free_redo();
  for (int i = 0; i < app.buffer.line_count && i < NOTEPAD_MAX_LINES; i++) {
    app.redo_lines[i] =
        notepad_strdup(app.buffer.lines[i] ? app.buffer.lines[i] : "");
  }
  app.redo_line_count = app.buffer.line_count;
  app.redo_cursor_line = app.buffer.cursor_line;
  app.redo_cursor_col = app.buffer.cursor_col;
  app.redo_available = true;

  /* Restore undo state */
  notepad_free_buffer();
  for (int i = 0; i < app.undo_line_count; i++) {
    app.buffer.lines[i] = app.undo_lines[i];
    app.undo_lines[i] = NULL;
  }
  app.buffer.line_count = app.undo_line_count;
  app.buffer.cursor_line = app.undo_cursor_line;
  app.buffer.cursor_col = app.undo_cursor_col;
  app.undo_line_count = 0;
  app.undo_available = false;
  app.buffer.modified = true;
}

static void notepad_do_redo(void) {
  if (!app.redo_available)
    return;

  /* Save current to undo */
  notepad_free_undo();
  for (int i = 0; i < app.buffer.line_count && i < NOTEPAD_MAX_LINES; i++) {
    app.undo_lines[i] =
        notepad_strdup(app.buffer.lines[i] ? app.buffer.lines[i] : "");
  }
  app.undo_line_count = app.buffer.line_count;
  app.undo_cursor_line = app.buffer.cursor_line;
  app.undo_cursor_col = app.buffer.cursor_col;
  app.undo_available = true;

  /* Restore redo state */
  notepad_free_buffer();
  for (int i = 0; i < app.redo_line_count; i++) {
    app.buffer.lines[i] = app.redo_lines[i];
    app.redo_lines[i] = NULL;
  }
  app.buffer.line_count = app.redo_line_count;
  app.buffer.cursor_line = app.redo_cursor_line;
  app.buffer.cursor_col = app.redo_cursor_col;
  app.redo_line_count = 0;
  app.redo_available = false;
  app.buffer.modified = true;
}

/* Editing operations */

static void notepad_insert_char(char c) {
  if (notepad_has_selection()) {
    notepad_delete_selection();
  } else {
    notepad_save_undo();
  }

  int line = app.buffer.cursor_line;
  int col = app.buffer.cursor_col;
  notepad_ensure_line(line);

  char *text = app.buffer.lines[line];
  int len = (int)np_strlen(text);

  if (col > len)
    col = len;
  if (len >= NOTEPAD_MAX_LINE_LEN - 1)
    return; /* Line full */

  /* Allocate new line with room */
  char *new_line = kmalloc((size_t)(len + 2));
  if (!new_line)
    return;

  memcpy(new_line, text, (size_t)col);
  new_line[col] = c;
  memcpy(new_line + col + 1, text + col, (size_t)(len - col + 1));

  kfree(app.buffer.lines[line]);
  app.buffer.lines[line] = new_line;

  app.buffer.cursor_col = col + 1;
  app.buffer.modified = true;
  notepad_ensure_cursor_visible();
}

static void notepad_delete_char(void) {
  if (notepad_has_selection()) {
    notepad_delete_selection();
    return;
  }

  notepad_save_undo();

  int line = app.buffer.cursor_line;
  int col = app.buffer.cursor_col;
  notepad_ensure_line(line);

  char *text = app.buffer.lines[line];
  int len = (int)np_strlen(text);

  if (col < len) {
    /* Delete character at cursor */
    for (int i = col; i < len; i++) {
      text[i] = text[i + 1];
    }
    app.buffer.modified = true;
  } else if (line < app.buffer.line_count - 1) {
    /* Merge with next line */
    notepad_ensure_line(line + 1);
    char *next = app.buffer.lines[line + 1];
    int next_len = (int)np_strlen(next);
    int new_len = len + next_len;
    if (new_len >= NOTEPAD_MAX_LINE_LEN)
      new_len = NOTEPAD_MAX_LINE_LEN - 1;

    char *merged = kmalloc((size_t)(new_len + 1));
    if (!merged)
      return;
    memcpy(merged, text, (size_t)len);
    int copy_len = next_len;
    if (len + copy_len > NOTEPAD_MAX_LINE_LEN - 1)
      copy_len = NOTEPAD_MAX_LINE_LEN - 1 - len;
    memcpy(merged + len, next, (size_t)copy_len);
    merged[len + copy_len] = '\0';

    kfree(app.buffer.lines[line]);
    kfree(app.buffer.lines[line + 1]);
    app.buffer.lines[line] = merged;

    /* Shift lines up */
    for (int i = line + 1; i < app.buffer.line_count - 1; i++) {
      app.buffer.lines[i] = app.buffer.lines[i + 1];
    }
    app.buffer.lines[app.buffer.line_count - 1] = NULL;
    app.buffer.line_count--;
    app.buffer.modified = true;
  }
}

static void notepad_backspace(void) {
  if (notepad_has_selection()) {
    notepad_delete_selection();
    return;
  }

  int line = app.buffer.cursor_line;
  int col = app.buffer.cursor_col;

  if (col > 0) {
    app.buffer.cursor_col--;
    notepad_delete_char();
  } else if (line > 0) {
    /* Move to end of previous line and merge */
    notepad_ensure_line(line - 1);
    int prev_len = (int)np_strlen(app.buffer.lines[line - 1]);
    app.buffer.cursor_line = line - 1;
    app.buffer.cursor_col = prev_len;
    notepad_delete_char();
  }
  notepad_ensure_cursor_visible();
}

static void notepad_insert_newline(void) {
  if (notepad_has_selection()) {
    notepad_delete_selection();
  } else {
    notepad_save_undo();
  }

  int line = app.buffer.cursor_line;
  int col = app.buffer.cursor_col;

  if (app.buffer.line_count >= NOTEPAD_MAX_LINES)
    return;

  notepad_ensure_line(line);
  char *text = app.buffer.lines[line];
  int len = (int)np_strlen(text);
  if (col > len)
    col = len;

  /* Create new line from text after cursor */
  char *new_line = notepad_strdup(text + col);

  /* Truncate current line at cursor */
  char *truncated = kmalloc((size_t)(col + 1));
  if (!truncated) {
    if (new_line)
      kfree(new_line);
    return;
  }
  memcpy(truncated, text, (size_t)col);
  truncated[col] = '\0';

  kfree(app.buffer.lines[line]);
  app.buffer.lines[line] = truncated;

  /* Shift lines down to make room */
  for (int i = app.buffer.line_count; i > line + 1; i--) {
    app.buffer.lines[i] = app.buffer.lines[i - 1];
  }
  app.buffer.lines[line + 1] = new_line;
  app.buffer.line_count++;

  app.buffer.cursor_line = line + 1;
  app.buffer.cursor_col = 0;
  app.buffer.modified = true;
  notepad_ensure_cursor_visible();
}

static void notepad_move_cursor(int dl, int dc) {
  int line = app.buffer.cursor_line + dl;
  int col = app.buffer.cursor_col + dc;

  /* Clamp line */
  if (line < 0)
    line = 0;
  if (line >= app.buffer.line_count)
    line = app.buffer.line_count - 1;

  /* Clamp column */
  notepad_ensure_line(line);
  int len = (int)np_strlen(app.buffer.lines[line]);

  /* Wrap at line boundaries for left/right */
  if (dc != 0 && dl == 0) {
    if (col < 0 && line > 0) {
      line--;
      notepad_ensure_line(line);
      col = (int)np_strlen(app.buffer.lines[line]);
    } else if (col > len && line < app.buffer.line_count - 1) {
      line++;
      col = 0;
    }
  }

  if (col < 0)
    col = 0;
  notepad_ensure_line(line);
  len = (int)np_strlen(app.buffer.lines[line]);
  if (col > len)
    col = len;

  app.buffer.cursor_line = line;
  app.buffer.cursor_col = col;
  notepad_ensure_cursor_visible();
}

/* File operations */

static void notepad_do_new(void) {
  /* TODO: prompt if modified (requires modal dialog) */
  notepad_free_buffer();
  notepad_free_undo();
  notepad_free_redo();
  notepad_init_buffer();
  notepad_clear_selection();
  app.is_ctxt_file = false;
  app.render_mode = false;
  app.ctxt_theme_light = false;
  app.ctxt_scroll_px = 0;
  app.ctxt_scroll_x_px = 0;
  app.ctxt_line_count = 0;
  app.ctxt_content_height = 0;
  app.ctxt_content_width = 0;
  app.ctxt_link_count = 0;
  app.ctxt_hover_link = -1;

  /* Update title */
  window_t *win = gui_get_window(notepad_wid);
  if (win) {
    notepad_strcpy(win->title, "Notepad");
  }
}

/* Current path for the file dialog */
static char notepad_dialog_path[VFS_MAX_PATH] = "/home";

static void notepad_open_file(const char *name) {
  /* Build full VFS path */
  char vpath[VFS_MAX_PATH];
  if (name[0] == '/') {
    /* Already absolute */
    int i = 0;
    while (name[i] && i < VFS_MAX_PATH - 1) {
      vpath[i] = name[i];
      i++;
    }
    vpath[i] = '\0';
  } else {
    /* Relative to dialog path */
    int i = 0, j = 0;
    while (notepad_dialog_path[i] && j < VFS_MAX_PATH - 2) {
      vpath[j++] = notepad_dialog_path[i++];
    }
    if (j > 0 && vpath[j - 1] != '/')
      vpath[j++] = '/';
    i = 0;
    while (name[i] && j < VFS_MAX_PATH - 1) {
      vpath[j++] = name[i++];
    }
    vpath[j] = '\0';
  }

  int fd = vfs_open(vpath, O_RDONLY);
  if (fd < 0)
    return;

  notepad_free_buffer();
  notepad_free_undo();
  notepad_free_redo();
  notepad_clear_selection();

  /* Read file contents - grow buffer dynamically until EOF */
  uint32_t buf_cap = 32768;      /* start at 32 KB          */
  uint32_t buf_max = 512 * 1024; /* hard cap: 512 KB        */
  char *read_buf = kmalloc(buf_cap);
  if (!read_buf) {
    vfs_close(fd);
    notepad_init_buffer();
    return;
  }

  uint32_t total_read = 0;
  for (;;) {
    uint32_t space = buf_cap - total_read - 1; /* -1 for '\0' */
    if (space == 0) {
      /* Buffer full - try to grow */
      if (buf_cap >= buf_max)
        break; /* hard limit reached */
      uint32_t new_cap = buf_cap * 2;
      if (new_cap > buf_max)
        new_cap = buf_max;
      char *new_buf = kmalloc(new_cap);
      if (!new_buf)
        break; /* out of memory, use what we have */
      memcpy(new_buf, read_buf, total_read);
      kfree(read_buf);
      read_buf = new_buf;
      buf_cap = new_cap;
      space = buf_cap - total_read - 1;
    }
    int chunk = vfs_read(fd, read_buf + total_read, space);
    if (chunk <= 0)
      break; /* EOF or error */
    total_read += (uint32_t)chunk;
  }
  vfs_close(fd);
  int bytes = (int)total_read;

  if (bytes <= 0) {
    kfree(read_buf);
    notepad_init_buffer();
    return;
  }
  read_buf[bytes] = '\0';

  /* Parse into lines */
  app.buffer.line_count = 0;
  int line_start = 0;
  for (int i = 0; i <= bytes; i++) {
    if (read_buf[i] == '\n' || read_buf[i] == '\0') {
      int line_len = i - line_start;
      if (line_len >= NOTEPAD_MAX_LINE_LEN)
        line_len = NOTEPAD_MAX_LINE_LEN - 1;

      char *line = kmalloc((size_t)(line_len + 1));
      if (line) {
        memcpy(line, read_buf + line_start, (size_t)line_len);
        line[line_len] = '\0';
      }

      if (app.buffer.line_count < NOTEPAD_MAX_LINES) {
        app.buffer.lines[app.buffer.line_count++] = line;
      } else {
        if (line)
          kfree(line);
        break;
      }

      line_start = i + 1;
      if (read_buf[i] == '\0')
        break;
    }
  }

  kfree(read_buf);

  if (app.buffer.line_count == 0) {
    app.buffer.lines[0] = notepad_strdup("");
    app.buffer.line_count = 1;
  }

  app.buffer.cursor_line = 0;
  app.buffer.cursor_col = 0;
  app.buffer.scroll_x = 0;
  app.buffer.scroll_y = 0;
  app.buffer.modified = false;

  /* Copy full absolute path so future saves use the correct location */
  int i = 0;
  while (vpath[i] && i < 63) {
    app.buffer.filename[i] = vpath[i];
    i++;
  }
  app.buffer.filename[i] = '\0';

  app.is_ctxt_file =
      (notepad_filename_is_ctxt(vpath) != 0) || (notepad_buffer_looks_ctxt() != 0);
  app.render_mode = app.is_ctxt_file;
  app.ctxt_theme_light = false;
  app.ctxt_scroll_px = 0;
  app.ctxt_scroll_x_px = 0;
  app.ctxt_line_count = 0;
  app.ctxt_content_height = 0;
  app.ctxt_content_width = 0;
  app.ctxt_link_count = 0;
  app.ctxt_hover_link = -1;
  if (app.is_ctxt_file) {
    notepad_ctxt_parse();
  }

  /* Update window title */
  window_t *win = gui_get_window(notepad_wid);
  if (win) {
    notepad_strcpy(win->title, "Notepad - ");
    /* Append filename */
    int tlen = (int)np_strlen(win->title);
    int j = 0;
    while (vpath[j] && tlen < 63) {
      win->title[tlen++] = vpath[j++];
    }
    win->title[tlen] = '\0';
  }
}

static void notepad_save_file(const char *name) {
  /* Calculate total size needed */
  uint32_t total_len = 0;
  for (int i = 0; i < app.buffer.line_count; i++) {
    total_len +=
        (uint32_t)np_strlen(app.buffer.lines[i] ? app.buffer.lines[i] : "");
    if (i < app.buffer.line_count - 1)
      total_len++; /* newline */
  }

  char *write_buf = kmalloc(total_len + 1);
  if (!write_buf)
    return;

  int pos = 0;
  for (int i = 0; i < app.buffer.line_count; i++) {
    const char *text = app.buffer.lines[i] ? app.buffer.lines[i] : "";
    int len = (int)np_strlen(text);
    for (int j = 0; j < len; j++) {
      write_buf[pos++] = text[j];
    }
    if (i < app.buffer.line_count - 1) {
      write_buf[pos++] = '\n';
    }
  }
  write_buf[pos] = '\0';

  /* Build full VFS path */
  char vpath[VFS_MAX_PATH];
  if (name[0] == '/') {
    int k = 0;
    while (name[k] && k < VFS_MAX_PATH - 1) {
      vpath[k] = name[k];
      k++;
    }
    vpath[k] = '\0';
  } else {
    int k = 0, j = 0;
    while (notepad_dialog_path[k] && j < VFS_MAX_PATH - 2) {
      vpath[j++] = notepad_dialog_path[k++];
    }
    if (j > 0 && vpath[j - 1] != '/')
      vpath[j++] = '/';
    k = 0;
    while (name[k] && j < VFS_MAX_PATH - 1) {
      vpath[j++] = name[k++];
    }
    vpath[j] = '\0';
  }

  int fd = vfs_open(vpath, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd >= 0) {
    /* Write in chunks */
    uint32_t written = 0;
    while (written < (uint32_t)pos) {
      int w = vfs_write(fd, write_buf + written, (uint32_t)pos - written);
      if (w <= 0)
        break;
      written += (uint32_t)w;
    }
    vfs_close(fd);
  } else {
    /* Fallback: try writing directly via FAT16 */
    fat16_write_file(vpath, write_buf, (uint32_t)pos);
  }
  kfree(write_buf);
  app.buffer.modified = false;

  /* Copy full absolute path as filename so future Ctrl+S uses the right path */
  int i = 0;
  while (vpath[i] && i < 63) {
    app.buffer.filename[i] = vpath[i];
    i++;
  }
  app.buffer.filename[i] = '\0';

  /* Update window title */
  window_t *win = gui_get_window(notepad_wid);
  if (win) {
    notepad_strcpy(win->title, "Notepad - ");
    int tlen = (int)np_strlen(win->title);
    int j = 0;
    while (vpath[j] && tlen < 63) {
      win->title[tlen++] = vpath[j++];
    }
    win->title[tlen] = '\0';
  }
}

static void notepad_do_open(void) { notepad_open_dialog(false); }

static void notepad_do_save(void) {
  if (app.buffer.filename[0]) {
    notepad_save_file(app.buffer.filename);

    /* If this looks like a calendar note (N_MMDD.TXT), mark saved */
    const char *fn = app.buffer.filename;
    if (!app.buffer.modified) {
      /* Walk to the basename */
      const char *base = fn;
      for (const char *p = fn; *p; p++) {
        if (*p == '/')
          base = p + 1;
      }
      if ((base[0] == 'n' || base[0] == 'N') && base[1] == '_') {
        int m = (base[2] - '0') * 10 + (base[3] - '0');
        int d = (base[4] - '0') * 10 + (base[5] - '0');
        if (m >= 1 && m <= 12 && d >= 1 && d <= 31) {
          calendar_mark_saved(&cal_state, cal_state.view_year, m, d);
        }
      }
    }
  } else {
    notepad_do_save_as();
  }
}

static void notepad_do_save_as(void) { notepad_open_dialog(true); }

/* File dialog */

/* Populate file list from VFS directory */
static void notepad_populate_dialog(void) {
  app.dialog.file_count = 0;
  app.dialog.selected_index = -1;
  app.dialog.scroll_offset = 0;

  if (!notepad_dialog_reserve_files(DLG_FILES_INIT_CAP))
    return;

  int fd = vfs_open(notepad_dialog_path, O_RDONLY);
  if (fd < 0) {
    /* Fallback: try root */
    fd = vfs_open("/", O_RDONLY);
    if (fd < 0)
      return;
    notepad_dialog_path[0] = '/';
    notepad_dialog_path[1] = '\0';
  }

  /* Add ".." entry if not at root */
  if (notepad_dialog_path[0] != '/' || notepad_dialog_path[1] != '\0') {
    if (!notepad_dialog_reserve_files(app.dialog.file_count + 1)) {
      vfs_close(fd);
      return;
    }
    file_entry_t *fe = &app.dialog.files[app.dialog.file_count];
    notepad_strcpy(fe->filename, "..");
    fe->size = 0;
    fe->is_directory = true;
    app.dialog.file_count++;
  }

  vfs_dirent_t ent;
  while (vfs_readdir(fd, &ent) > 0) {
    if (!notepad_dialog_reserve_files(app.dialog.file_count + 1))
      break;
    file_entry_t *fe = &app.dialog.files[app.dialog.file_count];
    int i = 0;
    while (ent.name[i] && i < 31) {
      fe->filename[i] = ent.name[i];
      i++;
    }
    fe->filename[i] = '\0';
    fe->size = ent.size;
    fe->is_directory = (ent.type == VFS_TYPE_DIR);
    app.dialog.file_count++;
  }

  vfs_close(fd);
}

static void notepad_open_dialog(bool save_mode) {
  app.dialog_open = true;
  app.dialog.open = true;
  app.dialog.save_mode = save_mode;
  app.dialog.input_len = 0;
  app.dialog.input[0] = '\0';

  /* Pre-fill with current filename for save */
  if (save_mode && app.buffer.filename[0]) {
    notepad_strcpy(app.dialog.input, app.buffer.filename);
    app.dialog.input_len = (int)np_strlen(app.dialog.input);
  }

  notepad_populate_dialog();
}

static void notepad_close_dialog(void) {
  app.dialog_open = false;
  app.dialog.open = false;
}

/* Navigate the file dialog into a directory by name.
   Used by Enter key, OK button, and double-click handlers. */
static void notepad_dialog_navigate_dir(const char *dname) {
  if (dname[0] == '.' && dname[1] == '.' && dname[2] == '\0') {
    /* Go up: strip last path component */
    int plen = (int)np_strlen(notepad_dialog_path);
    if (plen > 1) {
      plen--;
      while (plen > 0 && notepad_dialog_path[plen] != '/')
        plen--;
      if (plen == 0)
        plen = 1; /* keep root / */
      notepad_dialog_path[plen] = '\0';
    }
  } else {
    int plen = (int)np_strlen(notepad_dialog_path);
    if (plen > 1 && plen < VFS_MAX_PATH - 2)
      notepad_dialog_path[plen++] = '/';
    int k = 0;
    while (dname[k] && plen < VFS_MAX_PATH - 1)
      notepad_dialog_path[plen++] = dname[k++];
    notepad_dialog_path[plen] = '\0';
  }
  app.dialog.input_len = 0;
  app.dialog.input[0] = '\0';
  notepad_populate_dialog();
}

static void notepad_dialog_handle_key(uint8_t scancode, char character) {
  if (scancode == SC_ESCAPE) {
    notepad_close_dialog();
    return;
  }

  if (scancode == SC_ENTER) {
    /* If a file is selected in the list but input is empty, use selected */
    if (app.dialog.input_len == 0 && app.dialog.selected_index >= 0 &&
        app.dialog.selected_index < app.dialog.file_count) {
      notepad_strcpy(app.dialog.input,
                     app.dialog.files[app.dialog.selected_index].filename);
      app.dialog.input_len = (int)np_strlen(app.dialog.input);
    }
    /* Confirm action */
    if (app.dialog.input_len > 0) {
      /* Navigate into a directory only if the input text exactly matches
         a directory entry - not just because a dir happens to be selected */
      int sel = app.dialog.selected_index;
      if (sel >= 0 && sel < app.dialog.file_count &&
          app.dialog.files[sel].is_directory &&
          np_strcmp(app.dialog.input, app.dialog.files[sel].filename) == 0) {
        notepad_dialog_navigate_dir(app.dialog.files[sel].filename);
        return;
      }
      if (app.dialog.save_mode) {
        notepad_save_file(app.dialog.input);
      } else {
        notepad_open_file(app.dialog.input);
      }
    }
    notepad_close_dialog();
    return;
  }

  /* Arrow keys to navigate file list */
  if (scancode == SC_ARROW_UP) {
    if (app.dialog.selected_index > 0) {
      app.dialog.selected_index--;
      notepad_strcpy(app.dialog.input,
                     app.dialog.files[app.dialog.selected_index].filename);
      app.dialog.input_len = (int)np_strlen(app.dialog.input);
      /* Adjust scroll if needed */
      if (app.dialog.selected_index < app.dialog.scroll_offset) {
        app.dialog.scroll_offset = app.dialog.selected_index;
      }
    }
    return;
  }

  if (scancode == SC_ARROW_DOWN) {
    if (app.dialog.selected_index < app.dialog.file_count - 1) {
      app.dialog.selected_index++;
      notepad_strcpy(app.dialog.input,
                     app.dialog.files[app.dialog.selected_index].filename);
      app.dialog.input_len = (int)np_strlen(app.dialog.input);
      /* Adjust scroll if needed */
      int items_h = DLG_LIST_H - DLG_ITEM_H - 2;
      int items_visible = items_h / DLG_ITEM_H;
      if (items_visible < 1)
        items_visible = 1;
      if (app.dialog.selected_index >=
          app.dialog.scroll_offset + items_visible) {
        app.dialog.scroll_offset =
            app.dialog.selected_index - items_visible + 1;
      }
    }
    return;
  }

  if (scancode == SC_BACKSPACE) {
    if (app.dialog.input_len > 0) {
      app.dialog.input_len--;
      app.dialog.input[app.dialog.input_len] = '\0';
    }
    return;
  }

  /* Regular character */
  if (character >= 32 && character < 127 && app.dialog.input_len < 63) {
    app.dialog.input[app.dialog.input_len++] = character;
    app.dialog.input[app.dialog.input_len] = '\0';
  }
}

static void notepad_dialog_handle_mouse(int16_t mx, int16_t my, uint8_t buttons,
                                        uint8_t prev_buttons, window_t *win) {
  bool pressed = (buttons & 0x01) && !(prev_buttons & 0x01);
  if (!pressed)
    return;

  /* Use the same layout as the draw function */
  dlg_layout_t L = notepad_dialog_get_layout(win);

  {
    bool page = false;
    int dir = ui_vscrollbar_hit(L.scrollbar, mx, my, &page);
    if (dir != 0) {
      int max_scroll = app.dialog.file_count - L.items_visible;
      if (max_scroll < 0)
        max_scroll = 0;
      if (page) {
        app.dialog.scroll_offset += dir * L.items_visible;
      } else {
        app.dialog.scroll_offset += dir;
      }
      if (app.dialog.scroll_offset < 0)
        app.dialog.scroll_offset = 0;
      if (app.dialog.scroll_offset > max_scroll)
        app.dialog.scroll_offset = max_scroll;
      return;
    }
  }

  if (ui_contains(L.ok_btn, mx, my)) {
    if (app.dialog.input_len == 0 && app.dialog.selected_index >= 0 &&
        app.dialog.selected_index < app.dialog.file_count) {
      notepad_strcpy(app.dialog.input,
                     app.dialog.files[app.dialog.selected_index].filename);
      app.dialog.input_len = (int)np_strlen(app.dialog.input);
    }
    if (app.dialog.input_len > 0) {
      /* Navigate into a directory only if the input text exactly matches
         a directory entry - not just because a dir happens to be selected */
      int sel = app.dialog.selected_index;
      if (sel >= 0 && sel < app.dialog.file_count &&
          app.dialog.files[sel].is_directory &&
          np_strcmp(app.dialog.input, app.dialog.files[sel].filename) == 0) {
        notepad_dialog_navigate_dir(app.dialog.files[sel].filename);
        return;
      }
      if (app.dialog.save_mode)
        notepad_save_file(app.dialog.input);
      else
        notepad_open_file(app.dialog.input);
    }
    notepad_close_dialog();
    return;
  }

  if (ui_contains(L.cancel_btn, mx, my)) {
    notepad_close_dialog();
    return;
  }

  {
    ui_rect_t items_area =
        ui_rect(L.list.x, (int16_t)L.items_y, L.list.w, (uint16_t)L.items_h);
    if (ui_contains(items_area, mx, my)) {
      int item = ((int)my - L.items_y) / DLG_ITEM_H + app.dialog.scroll_offset;
      if (item >= 0 && item < app.dialog.file_count) {
        /* Double-click: if same item clicked again, open/navigate */
        if (app.dialog.selected_index == item) {
          if (app.dialog.files[item].is_directory) {
            notepad_dialog_navigate_dir(app.dialog.files[item].filename);
            return;
          }
          /* Double-click on file: open it */
          if (!app.dialog.save_mode) {
            notepad_open_file(app.dialog.files[item].filename);
            notepad_close_dialog();
            return;
          }
        }
        app.dialog.selected_index = item;
        notepad_strcpy(app.dialog.input, app.dialog.files[item].filename);
        app.dialog.input_len = (int)np_strlen(app.dialog.input);
      }
    }
  }
}

/* Menu system */

static void notepad_menu_action(int menu, int item) {
  app.active_menu = MENU_NONE;

  if (menu == MENU_FILE) {
    switch (item) {
    case FMENU_NEW:
      notepad_do_new();
      break;
    case FMENU_OPEN:
      notepad_do_open();
      break;
    case FMENU_SAVE:
      notepad_do_save();
      break;
    case FMENU_SAVE_AS:
      notepad_do_save_as();
      break;
    case FMENU_EXIT:
      gui_destroy_window(notepad_wid);
      notepad_wid = -1;
      break;
    default:
      break;
    }
  } else if (menu == MENU_EDIT) {
    switch (item) {
    case EMENU_UNDO:
      notepad_do_undo();
      break;
    case EMENU_REDO:
      notepad_do_redo();
      break;
    case EMENU_CUT:
      notepad_copy_selection();
      notepad_delete_selection();
      break;
    case EMENU_COPY:
      notepad_copy_selection();
      break;
    case EMENU_PASTE: {
      const char *data = clipboard_get_data();
      if (data) {
        if (notepad_has_selection())
          notepad_delete_selection();
        else
          notepad_save_undo();
        int len = clipboard_get_length();
        for (int i = 0; i < len; i++) {
          if (data[i] == '\n') {
            notepad_insert_newline();
          } else if (data[i] >= 32 || data[i] == '\t') {
            notepad_insert_char(data[i]);
          }
        }
      }
      break;
    }
    case EMENU_SELECT_ALL:
      notepad_select_all();
      break;
    default:
      break;
    }
  }
}

/* Rendering */

static void notepad_draw_menubar(window_t *win) {
  int16_t mx = (int16_t)(win->x + 1);
  int16_t my = (int16_t)(win->y + TITLEBAR_H);

  /* Menu bar background */
  gfx_fill_rect(mx, my, (uint16_t)(win->width - 2), MENUBAR_H, COLOR_BORDER);

  /* "File" button */
  uint32_t file_color =
      (app.active_menu == MENU_FILE) ? COLOR_HIGHLIGHT : COLOR_BORDER;
  gfx_fill_rect((int16_t)(mx + 2), my, 36, MENUBAR_H, file_color);
  gfx_draw_text((int16_t)(mx + 4), (int16_t)(my + 2), "File", COLOR_BLACK);

  /* "Edit" button */
  uint32_t edit_color =
      (app.active_menu == MENU_EDIT) ? COLOR_HIGHLIGHT : COLOR_BORDER;
  gfx_fill_rect((int16_t)(mx + 40), my, 36, MENUBAR_H, edit_color);
  gfx_draw_text((int16_t)(mx + 42), (int16_t)(my + 2), "Edit", COLOR_BLACK);
}

static void notepad_draw_dropdown(window_t *win) {
  if (app.active_menu == MENU_NONE)
    return;

  int16_t ddx, ddy;
  int item_count;
  const char *items[8];
  const char *shortcuts[8];
  bool separators[8];

  if (app.active_menu == MENU_FILE) {
    ddx = (int16_t)(win->x + 3);
    ddy = (int16_t)(win->y + TITLEBAR_H + MENUBAR_H);
    item_count = FMENU_COUNT;
    items[0] = "New";
    shortcuts[0] = "Ctrl+N";
    separators[0] = false;
    items[1] = "Open...";
    shortcuts[1] = "Ctrl+O";
    separators[1] = false;
    items[2] = "Save";
    shortcuts[2] = "Ctrl+S";
    separators[2] = false;
    items[3] = "Save As...";
    shortcuts[3] = "";
    separators[3] = false;
    items[4] = "";
    shortcuts[4] = "";
    separators[4] = true;
    items[5] = "Exit";
    shortcuts[5] = "Ctrl+Q";
    separators[5] = false;
  } else {
    ddx = (int16_t)(win->x + 41);
    ddy = (int16_t)(win->y + TITLEBAR_H + MENUBAR_H);
    item_count = EMENU_COUNT;
    items[0] = "Undo";
    shortcuts[0] = "Ctrl+Z";
    separators[0] = false;
    items[1] = "Redo";
    shortcuts[1] = "Ctrl+Y";
    separators[1] = false;
    items[2] = "";
    shortcuts[2] = "";
    separators[2] = true;
    items[3] = "Cut";
    shortcuts[3] = "Ctrl+X";
    separators[3] = false;
    items[4] = "Copy";
    shortcuts[4] = "Ctrl+C";
    separators[4] = false;
    items[5] = "Paste";
    shortcuts[5] = "Ctrl+V";
    separators[5] = false;
    items[6] = "";
    shortcuts[6] = "";
    separators[6] = true;
    items[7] = "Select All";
    shortcuts[7] = "Ctrl+A";
    separators[7] = false;
  }

  uint16_t dd_w = 150;
  uint16_t dd_h = (uint16_t)(item_count * 14 + 4);

  /* Dropdown background */
  gfx_fill_rect(ddx, ddy, dd_w, dd_h, COLOR_TEXT_LIGHT);
  gfx_draw_rect(ddx, ddy, dd_w, dd_h, COLOR_BORDER);

  for (int i = 0; i < item_count; i++) {
    int16_t iy = (int16_t)(ddy + 2 + i * 14);

    if (separators[i]) {
      gfx_draw_hline((int16_t)(ddx + 4), (int16_t)(iy + 6),
                     (uint16_t)(dd_w - 8), COLOR_BORDER);
      continue;
    }

    /* Hover highlight */
    if (i == app.hover_item) {
      gfx_fill_rect((int16_t)(ddx + 2), iy, (uint16_t)(dd_w - 4), 14,
                    COLOR_HIGHLIGHT);
    }

    gfx_draw_text((int16_t)(ddx + 8), (int16_t)(iy + 3), items[i], COLOR_BLACK);

    /* Right-aligned shortcut */
    if (shortcuts[i][0]) {
      uint16_t sw = gfx_text_width(shortcuts[i]);
      gfx_draw_text((int16_t)(ddx + (int16_t)dd_w - (int16_t)sw - 8),
                    (int16_t)(iy + 3), shortcuts[i], COLOR_TEXT);
    }
  }
}

static void notepad_draw_text_area(window_t *win) {
  if (app.is_ctxt_file && app.render_mode) {
    notepad_draw_ctxt_area(win);
    return;
  }

  int vis_cols, vis_lines, edit_x, edit_y, edit_w, edit_h;
  notepad_get_viewport(&vis_cols, &vis_lines, &edit_x, &edit_y, &edit_w,
                       &edit_h, win);

  int scale = app.font_scale;
  if (scale < 1)
    scale = 1;
  int char_w = FONT_W * scale;
  int char_h = FONT_H * scale;

  /* White editing background */
  gfx_fill_rect((int16_t)edit_x, (int16_t)edit_y, (uint16_t)edit_w,
                (uint16_t)edit_h, COLOR_TEXT_LIGHT);

  /* Compute selection bounds (normalized) */
  int sel_sl = 0, sel_sc = 0, sel_el = 0, sel_ec = 0;
  bool has_sel = notepad_has_selection();
  if (has_sel) {
    notepad_normalize_selection(&sel_sl, &sel_sc, &sel_el, &sel_ec);
  }

  /* Render text lines */
  for (int row = 0; row < vis_lines; row++) {
    int src_line = row + app.buffer.scroll_y;
    if (src_line >= app.buffer.line_count)
      break;

    const char *text = app.buffer.lines[src_line];
    if (!text)
      text = "";
    int len = (int)np_strlen(text);

    int16_t py = (int16_t)(edit_y + row * char_h);

    for (int col = 0; col < vis_cols; col++) {
      int src_col = col + app.buffer.scroll_x;
      if (src_col >= len)
        break;

      int16_t px = (int16_t)(edit_x + col * char_w);

      /* Check if this char is in selection */
      bool in_sel = false;
      if (has_sel) {
        if (src_line > sel_sl && src_line < sel_el) {
          in_sel = true;
        } else if (src_line == sel_sl && src_line == sel_el) {
          in_sel = (src_col >= sel_sc && src_col < sel_ec);
        } else if (src_line == sel_sl) {
          in_sel = (src_col >= sel_sc);
        } else if (src_line == sel_el) {
          in_sel = (src_col < sel_ec);
        }
      }

      if (in_sel) {
        gfx_fill_rect(px, py, (uint16_t)char_w, (uint16_t)char_h, COLOR_BUTTON);
        if (scale == 1)
          gfx_draw_char(px, py, text[src_col], COLOR_TEXT_LIGHT);
        else
          gfx_draw_char_scaled(px, py, text[src_col], COLOR_TEXT_LIGHT, scale);
      } else {
        if (scale == 1)
          gfx_draw_char(px, py, text[src_col], COLOR_BLACK);
        else
          gfx_draw_char_scaled(px, py, text[src_col], COLOR_BLACK, scale);
      }
    }

    /* Draw selection background for empty part of selected lines */
    if (has_sel) {
      if (src_line > sel_sl && src_line < sel_el) {
        /* Entire line selected - fill remaining space */
        int drawn_cols = len - app.buffer.scroll_x;
        if (drawn_cols < 0)
          drawn_cols = 0;
        if (drawn_cols < vis_cols) {
          gfx_fill_rect((int16_t)(edit_x + drawn_cols * char_w), py,
                        (uint16_t)((vis_cols - drawn_cols) * char_w),
                        (uint16_t)char_h, COLOR_BUTTON);
        }
      }
    }
  }

  /* Draw blinking cursor */
  if (app.cursor_visible && !app.dialog_open) {
    int cursor_screen_row = app.buffer.cursor_line - app.buffer.scroll_y;
    int cursor_screen_col = app.buffer.cursor_col - app.buffer.scroll_x;

    if (cursor_screen_row >= 0 && cursor_screen_row < vis_lines &&
        cursor_screen_col >= 0 && cursor_screen_col < vis_cols) {
      int16_t cx = (int16_t)(edit_x + cursor_screen_col * char_w);
      int16_t cy = (int16_t)(edit_y + cursor_screen_row * char_h);
      gfx_draw_vline(cx, cy, (uint16_t)char_h, COLOR_BLACK);
    }
  }
}

static void notepad_draw_scrollbars(window_t *win) {
  int edit_x, edit_y, edit_w, edit_h;
  int vis_cols, vis_lines;
  notepad_get_viewport(&vis_cols, &vis_lines, &edit_x, &edit_y, &edit_w,
                       &edit_h, win);

  int vscroll_x = edit_x + edit_w;
  int vscroll_y = edit_y;
  int vscroll_h = edit_h;

  /* Background */
  gfx_fill_rect((int16_t)vscroll_x, (int16_t)vscroll_y, VSCROLL_W,
                (uint16_t)vscroll_h, COLOR_BORDER);

  /* Up arrow */
  gfx_fill_rect((int16_t)vscroll_x, (int16_t)vscroll_y, VSCROLL_W,
                SCROLL_ARROW_SIZE, COLOR_TEXT_LIGHT);
  gfx_draw_rect((int16_t)vscroll_x, (int16_t)vscroll_y, VSCROLL_W,
                SCROLL_ARROW_SIZE, COLOR_BORDER);
  /* Triangle pointing up */
  gfx_draw_char((int16_t)(vscroll_x + 2), (int16_t)(vscroll_y + 2), '^',
                COLOR_BLACK);

  /* Down arrow */
  int down_y = vscroll_y + vscroll_h - SCROLL_ARROW_SIZE;
  gfx_fill_rect((int16_t)vscroll_x, (int16_t)down_y, VSCROLL_W,
                SCROLL_ARROW_SIZE, COLOR_TEXT_LIGHT);
  gfx_draw_rect((int16_t)vscroll_x, (int16_t)down_y, VSCROLL_W,
                SCROLL_ARROW_SIZE, COLOR_BORDER);
  gfx_draw_char((int16_t)(vscroll_x + 2), (int16_t)(down_y + 2), 'v',
                COLOR_BLACK);

  /* Thumb */
  int track_h = vscroll_h - 2 * SCROLL_ARROW_SIZE;
  int v_total = app.buffer.line_count;
  int v_view = vis_lines;
  int v_scroll = app.buffer.scroll_y;
  if (app.is_ctxt_file && app.render_mode) {
    v_total = app.ctxt_content_height;
    v_view = edit_h;
    v_scroll = app.ctxt_scroll_px;
  }

  if (track_h > 0 && v_total > v_view) {
    int thumb_h = (track_h * v_view) / v_total;
    if (thumb_h < SCROLL_THUMB_MIN)
      thumb_h = SCROLL_THUMB_MIN;
    if (thumb_h > track_h)
      thumb_h = track_h;

    int thumb_max = track_h - thumb_h;
    int thumb_y_off = 0;
    if (v_total - v_view > 0) {
      thumb_y_off = (v_scroll * thumb_max) / (v_total - v_view);
    }

    int thumb_y = vscroll_y + SCROLL_ARROW_SIZE + thumb_y_off;
    gfx_fill_rect((int16_t)(vscroll_x + 1), (int16_t)thumb_y,
                  (uint16_t)(VSCROLL_W - 2), (uint16_t)thumb_h,
                  COLOR_TEXT_LIGHT);
    gfx_draw_rect((int16_t)(vscroll_x + 1), (int16_t)thumb_y,
                  (uint16_t)(VSCROLL_W - 2), (uint16_t)thumb_h, COLOR_TEXT);
  }

  int hscroll_x = edit_x;
  int hscroll_y = edit_y + edit_h;
  int hscroll_w = edit_w;

  /* Background */
  gfx_fill_rect((int16_t)hscroll_x, (int16_t)hscroll_y, (uint16_t)hscroll_w,
                HSCROLL_H, COLOR_BORDER);

  /* Left arrow */
  gfx_fill_rect((int16_t)hscroll_x, (int16_t)hscroll_y, SCROLL_ARROW_SIZE,
                HSCROLL_H, COLOR_TEXT_LIGHT);
  gfx_draw_rect((int16_t)hscroll_x, (int16_t)hscroll_y, SCROLL_ARROW_SIZE,
                HSCROLL_H, COLOR_BORDER);
  gfx_draw_char((int16_t)(hscroll_x + 3), (int16_t)(hscroll_y + 2), '<',
                COLOR_BLACK);

  /* Right arrow */
  int right_x = hscroll_x + hscroll_w - SCROLL_ARROW_SIZE;
  gfx_fill_rect((int16_t)right_x, (int16_t)hscroll_y, SCROLL_ARROW_SIZE,
                HSCROLL_H, COLOR_TEXT_LIGHT);
  gfx_draw_rect((int16_t)right_x, (int16_t)hscroll_y, SCROLL_ARROW_SIZE,
                HSCROLL_H, COLOR_BORDER);
  gfx_draw_char((int16_t)(right_x + 3), (int16_t)(hscroll_y + 2), '>',
                COLOR_BLACK);

  /* Thumb */
  int track_w = hscroll_w - 2 * SCROLL_ARROW_SIZE;
  int max_w = notepad_max_line_width();
  if (app.is_ctxt_file && app.render_mode) {
    int view_w = edit_w - 16;
    int h_total = app.ctxt_content_width;
    int h_scroll = app.ctxt_scroll_x_px;
    if (view_w < 8)
      view_w = 8;
    if (track_w > 0 && h_total > view_w) {
      int thumb_w = (track_w * view_w) / h_total;
      if (thumb_w < SCROLL_THUMB_MIN)
        thumb_w = SCROLL_THUMB_MIN;
      if (thumb_w > track_w)
        thumb_w = track_w;

      int thumb_max = track_w - thumb_w;
      int thumb_x_off = 0;
      if (h_total - view_w > 0) {
        thumb_x_off = (h_scroll * thumb_max) / (h_total - view_w);
      }

      int thumb_x = hscroll_x + SCROLL_ARROW_SIZE + thumb_x_off;
      gfx_fill_rect((int16_t)thumb_x, (int16_t)(hscroll_y + 1),
                    (uint16_t)thumb_w, (uint16_t)(HSCROLL_H - 2),
                    COLOR_TEXT_LIGHT);
      gfx_draw_rect((int16_t)thumb_x, (int16_t)(hscroll_y + 1),
                    (uint16_t)thumb_w, (uint16_t)(HSCROLL_H - 2), COLOR_TEXT);
    } else {
      gfx_fill_rect((int16_t)(hscroll_x + SCROLL_ARROW_SIZE),
                    (int16_t)(hscroll_y + 1),
                    (uint16_t)(track_w > 0 ? track_w : 0),
                    (uint16_t)(HSCROLL_H - 2), COLOR_BORDER);
    }
  } else if (track_w > 0 && max_w > vis_cols) {
    int thumb_w = (track_w * vis_cols) / max_w;
    if (thumb_w < SCROLL_THUMB_MIN)
      thumb_w = SCROLL_THUMB_MIN;
    if (thumb_w > track_w)
      thumb_w = track_w;

    int thumb_max = track_w - thumb_w;
    int thumb_x_off = 0;
    if (max_w - vis_cols > 0) {
      thumb_x_off = (app.buffer.scroll_x * thumb_max) / (max_w - vis_cols);
    }

    int thumb_x = hscroll_x + SCROLL_ARROW_SIZE + thumb_x_off;
    gfx_fill_rect((int16_t)thumb_x, (int16_t)(hscroll_y + 1), (uint16_t)thumb_w,
                  (uint16_t)(HSCROLL_H - 2), COLOR_TEXT_LIGHT);
    gfx_draw_rect((int16_t)thumb_x, (int16_t)(hscroll_y + 1), (uint16_t)thumb_w,
                  (uint16_t)(HSCROLL_H - 2), COLOR_TEXT);
  }

  /* Corner box where scrollbars meet */
  gfx_fill_rect((int16_t)(edit_x + edit_w), (int16_t)(edit_y + edit_h),
                VSCROLL_W, HSCROLL_H, COLOR_BORDER);
}

static void notepad_draw_statusbar(window_t *win) {
  int16_t sy = (int16_t)(win->y + (int16_t)win->height - STATUSBAR_H - 1);
  int16_t sx = (int16_t)(win->x + 1);
  uint16_t sw = (uint16_t)(win->width - 2);

  gfx_fill_rect(sx, sy, sw, STATUSBAR_H, COLOR_BORDER);

  /* Format "Ln X, Col Y" */
  char status[32];
  int pos = 0;

  /* "Ln " */
  status[pos++] = 'L';
  status[pos++] = 'n';
  status[pos++] = ' ';

  /* Line number (1-based) */
  {
    uint32_t num = (uint32_t)(app.buffer.cursor_line + 1);
    char tmp[12];
    int ti = 0;
    if (num == 0) {
      tmp[ti++] = '0';
    } else {
      while (num > 0) {
        tmp[ti++] = (char)('0' + (char)(num % 10));
        num /= 10;
      }
    }
    for (int i = ti - 1; i >= 0; i--)
      status[pos++] = tmp[i];
  }

  status[pos++] = ',';
  status[pos++] = ' ';
  status[pos++] = 'C';
  status[pos++] = 'o';
  status[pos++] = 'l';
  status[pos++] = ' ';

  /* Column number (1-based) */
  {
    uint32_t num = (uint32_t)(app.buffer.cursor_col + 1);
    char tmp[12];
    int ti = 0;
    if (num == 0) {
      tmp[ti++] = '0';
    } else {
      while (num > 0) {
        tmp[ti++] = (char)('0' + (char)(num % 10));
        num /= 10;
      }
    }
    for (int i = ti - 1; i >= 0; i--)
      status[pos++] = tmp[i];
  }

  status[pos] = '\0';

  /* Right-align */
  uint16_t tw = gfx_text_width(status);
  gfx_draw_text((int16_t)(sx + (int16_t)sw - (int16_t)tw - 4),
                (int16_t)(sy + 1), status, COLOR_TEXT);

  if (app.is_ctxt_file) {
    const char *mode = app.render_mode ? "[RENDERED]" : "[SOURCE]";
    gfx_draw_text((int16_t)(sx + 4), (int16_t)(sy + 1), mode, COLOR_TEXT);
  }

  /* Modified indicator in title bar */
  if (app.buffer.modified) {
    /* Draw '*' at end of title */
    window_t *w = gui_get_window(notepad_wid);
    if (w) {
      int tlen = (int)np_strlen(w->title);
      if (tlen > 0 && w->title[tlen - 1] != '*' && tlen < 63) {
        w->title[tlen] = '*';
        w->title[tlen + 1] = '\0';
      }
    }
  }
}

/* File dialog layout - single source of truth for draw + mouse */

static dlg_layout_t notepad_dialog_get_layout(window_t *win) {
  dlg_layout_t L;
  ui_rect_t wr = ui_rect(win->x, win->y, win->width, win->height);

  /* Center dialog in window */
  L.dialog = ui_center(wr, DLG_W, DLG_H);

  int16_t dx = L.dialog.x;
  int16_t dy = L.dialog.y;

  /* Title bar (inside the 3D outer border, 2px inset) */
  L.titlebar =
      ui_rect((int16_t)(dx + 3), (int16_t)(dy + 3), (uint16_t)(DLG_W - 6), 20);

    /* Current path row */
    L.path_row = ui_rect((int16_t)(dx + 10), (int16_t)(dy + 28),
               (uint16_t)(DLG_W - 20), 10);

  /* File list + scrollbar sunken area */
    int16_t list_x = (int16_t)(dx + 10);
    int16_t list_y = (int16_t)(dy + 40);
    uint16_t list_inner_w = (uint16_t)(DLG_W - 20 - DLG_SCROLLBAR_W);

    L.list_area = ui_rect(list_x, list_y, (uint16_t)(DLG_W - 20), DLG_LIST_H);
  L.list = ui_rect(list_x, list_y, list_inner_w, DLG_LIST_H);
  L.scrollbar =
      ui_rect((int16_t)(list_x + (int16_t)list_inner_w), (int16_t)(list_y + 1),
              DLG_SCROLLBAR_W, (uint16_t)(DLG_LIST_H - 2));

  /* Items area (below column header row) */
  L.items_y = (int)list_y + DLG_ITEM_H + 1;
  L.items_h = DLG_LIST_H - DLG_ITEM_H - 2;
  L.items_visible = L.items_h / DLG_ITEM_H;
  if (L.items_visible < 1)
    L.items_visible = 1;

  /* Input row: "File:" label + text field */
    int16_t row_y = (int16_t)(dy + 40 + DLG_LIST_H + 14);
    L.input_label = ui_rect((int16_t)(dx + 10), row_y, 40, 20);
  L.input_field =
      ui_rect((int16_t)(dx + 50), row_y, (uint16_t)(DLG_W - 60 - 140), 20);

    /* OK / Cancel buttons (bottom-right) */
    int16_t btn_y = row_y;
    L.ok_btn = ui_rect((int16_t)(dx + DLG_W - 140), btn_y, DLG_BTN_W, DLG_BTN_H);
    L.cancel_btn =
      ui_rect((int16_t)(dx + DLG_W - 70), btn_y, DLG_BTN_W, DLG_BTN_H);

    /* File count status text (own line under controls; no overlap) */
    int16_t status_x = (int16_t)(dx + 10);
    int16_t status_y = (int16_t)(dy + DLG_H - 14);
    L.status = ui_rect(status_x, status_y, (uint16_t)(DLG_W - 20), 10);

  return L;
}

static void notepad_draw_file_dialog(window_t *win) {
  if (!app.dialog_open)
    return;

  dlg_layout_t L = notepad_dialog_get_layout(win);

  ui_draw_shadow(L.dialog, COLOR_TEXT, 3);
  ui_draw_panel(L.dialog, COLOR_BORDER, true, true);

  gfx_fill_rect(L.titlebar.x, L.titlebar.y, L.titlebar.w, L.titlebar.h, 0x000080);
  gfx_draw_rect(L.titlebar.x, L.titlebar.y, L.titlebar.w, L.titlebar.h,
                COLOR_TEXT_LIGHT);
  gfx_draw_text((int16_t)(L.titlebar.x + 6), (int16_t)(L.titlebar.y + 6),
                app.dialog.save_mode ? "Save File" : "Open File", 0xFFFFFF);

  {
    char path_disp[64];
    np_copy_limited(path_disp, notepad_dialog_path, 64);
    gfx_draw_text(L.path_row.x, L.path_row.y, path_disp, COLOR_BLACK);
  }

  ui_draw_panel(L.list_area, COLOR_TEXT_LIGHT, true, false);

  /* Column header */
  gfx_fill_rect((int16_t)(L.list.x + 1), (int16_t)(L.list.y + 1),
                (uint16_t)(L.list.w - 1), DLG_ITEM_H, COLOR_BORDER);
  gfx_draw_text((int16_t)(L.list.x + 4), (int16_t)(L.list.y + 2), "Name",
                COLOR_BLACK);
  int size_col_x = (int)L.list.x + (int)L.list.w - 40;
  gfx_draw_text((int16_t)size_col_x, (int16_t)(L.list.y + 2), "Size",
                COLOR_BLACK);
  gfx_draw_vline((int16_t)(size_col_x - 3), (int16_t)(L.list.y + 1), DLG_ITEM_H,
                 COLOR_TEXT);

  for (int i = 0; i < L.items_visible; i++) {
    int fi = i + app.dialog.scroll_offset;
    if (fi >= app.dialog.file_count)
      break;
    int16_t fy = (int16_t)(L.items_y + i * DLG_ITEM_H);
    int16_t list_bottom = (int16_t)(L.list.y + (int16_t)L.list.h - 1);
    if (fy < L.list.y || fy + FONT_H >= list_bottom)
      continue;

    if (fi == app.dialog.selected_index) {
      gfx_fill_rect((int16_t)(L.list.x + 1), fy, (uint16_t)(L.list.w - 1),
                    DLG_ITEM_H, 0x000080);
    }

    uint32_t tc =
        (fi == app.dialog.selected_index) ? 0xFFFFFF : COLOR_BLACK;

    {
      char name_buf[40];
      int max_name_px = size_col_x - ((int)L.list.x + 28) - 4;
      int max_name_chars = max_name_px / FONT_W;
      if (max_name_chars < 1)
        max_name_chars = 1;
      if (max_name_chars > 35)
        max_name_chars = 35;

      int ni = 0;
      const char *src_name = app.dialog.files[fi].filename;
      while (src_name[ni] && ni < max_name_chars) {
        name_buf[ni] = src_name[ni];
        ni++;
      }
      if (src_name[ni] && max_name_chars >= 3) {
        name_buf[max_name_chars - 3] = '.';
        name_buf[max_name_chars - 2] = '.';
        name_buf[max_name_chars - 1] = '.';
        ni = max_name_chars;
      }
      name_buf[ni] = '\0';

      if (app.dialog.files[fi].is_directory) {
        uint32_t dir_col =
          (fi == app.dialog.selected_index) ? COLOR_TEXT_LIGHT : 0x0000AA;
        gfx_draw_text((int16_t)(L.list.x + 3), (int16_t)(fy + 1), "[D]",
                      dir_col);
        gfx_draw_text((int16_t)(L.list.x + 28), (int16_t)(fy + 1), name_buf,
                      tc);
      } else {
        gfx_draw_char((int16_t)(L.list.x + 3), (int16_t)(fy + 1), '|',
                      COLOR_TEXT);
        gfx_draw_char((int16_t)(L.list.x + 8), (int16_t)(fy + 1), '=',
                      COLOR_TEXT);
        gfx_draw_text((int16_t)(L.list.x + 18), (int16_t)(fy + 1), name_buf,
                      tc);
      }
    }

    /* File size */
    if (!app.dialog.files[fi].is_directory) {
      char size_buf[16];
      int sp = 0;
      uint32_t sz = app.dialog.files[fi].size;
      if (sz < 1024) {
        if (sz == 0) {
          size_buf[sp++] = '0';
        } else {
          char tmp[12];
          int ti = 0;
          while (sz > 0) {
            tmp[ti++] = (char)('0' + (sz % 10));
            sz /= 10;
          }
          for (int j = ti - 1; j >= 0; j--)
            size_buf[sp++] = tmp[j];
        }
        size_buf[sp++] = 'B';
      } else {
        uint32_t kb = sz / 1024;
        if (kb == 0)
          kb = 1;
        char tmp[12];
        int ti = 0;
        while (kb > 0) {
          tmp[ti++] = (char)('0' + (kb % 10));
          kb /= 10;
        }
        for (int j = ti - 1; j >= 0; j--)
          size_buf[sp++] = tmp[j];
        size_buf[sp++] = 'K';
      }
      size_buf[sp] = '\0';
      gfx_draw_text((int16_t)size_col_x, (int16_t)(fy + 1), size_buf, tc);
    }
  }

  if (app.dialog.file_count == 0) {
    gfx_draw_text((int16_t)(L.list.x + 8), (int16_t)(L.items_y + 4), "(empty)",
                  COLOR_TEXT);
  }

  ui_draw_vscrollbar(L.scrollbar, app.dialog.file_count, L.items_visible,
                     app.dialog.scroll_offset);

  ui_draw_label(L.input_label, "File:", COLOR_BLACK, UI_ALIGN_LEFT);
  ui_draw_textfield(L.input_field, app.dialog.input, app.dialog.input_len);

  ui_draw_button(L.ok_btn, "OK", true);
  ui_draw_button(L.cancel_btn, "Cancel", true);

  {
    char count_buf[24];
    int cp = 0;
    uint32_t fc = (uint32_t)app.dialog.file_count;
    if (fc == 0) {
      count_buf[cp++] = '0';
    } else {
      char tmp[12];
      int ti = 0;
      while (fc > 0) {
        tmp[ti++] = (char)('0' + (fc % 10));
        fc /= 10;
      }
      for (int j = ti - 1; j >= 0; j--)
        count_buf[cp++] = tmp[j];
    }
    count_buf[cp++] = ' ';
    count_buf[cp++] = 'f';
    count_buf[cp++] = 'i';
    count_buf[cp++] = 'l';
    count_buf[cp++] = 'e';
    count_buf[cp++] = 's';
    count_buf[cp] = '\0';
    ui_draw_label(L.status, count_buf, COLOR_TEXT, UI_ALIGN_LEFT);
  }
}

/* Main redraw callback */

void notepad_redraw(window_t *win) {
  notepad_draw_menubar(win);
  notepad_draw_text_area(win);
  notepad_draw_scrollbars(win);
  notepad_draw_statusbar(win);
  notepad_draw_dropdown(win);    /* Draw dropdown on top */
  notepad_draw_file_dialog(win); /* Draw dialog on top of everything */
}

/* Process entry point */

static void notepad_process_entry(void) {
  while (1) {
    if (notepad_wid < 0 || !gui_get_window(notepad_wid)) {
      notepad_wid = -1;
      app.pid = 0;
      break;
    }
    kernel_check_reschedule();
    process_yield();
  }
}

/* Launch */

static void notepad_on_close(window_t *win) {
  (void)win;
  notepad_dialog_release_files();
  uint32_t pid = app.pid;
  notepad_wid = -1;
  app.pid = 0;
  if (pid > 1) {
    process_kill(pid);
  }
}

void notepad_launch(void) {
  /* Don't open a second instance */
  if (notepad_wid >= 0 && gui_get_window(notepad_wid))
    return;

  memset(&app, 0, sizeof(app));

  notepad_wid =
      gui_create_window(50, 40, NOTEPAD_WIN_W, NOTEPAD_WIN_H, "Notepad");
  if (notepad_wid < 0) {
    KERROR("notepad_launch: failed to create window");
    return;
  }

  app.window_id = notepad_wid;
  app.active_menu = MENU_NONE;
  app.hover_item = -1;
  app.cursor_visible = true;
  app.last_blink_ms = timer_get_uptime_ms();
  app.dialog_open = false;
  app.font_scale = 1;
  app.is_ctxt_file = false;
  app.render_mode = false;
  app.ctxt_theme_light = false;
  app.ctxt_scroll_px = 0;
  app.ctxt_scroll_x_px = 0;
  app.ctxt_line_count = 0;
  app.ctxt_content_height = 0;
  app.ctxt_content_width = 0;
  app.ctxt_link_count = 0;
  app.ctxt_hover_link = -1;

  notepad_init_buffer();
  notepad_clear_selection();
  notepad_free_undo();
  notepad_free_redo();

  window_t *win = gui_get_window(notepad_wid);
  if (win) {
    win->redraw = notepad_redraw;
    win->on_close = notepad_on_close;
  }

  gui_set_focus(notepad_wid);

  /* Spawn as its own process */
  app.pid =
      process_create(notepad_process_entry, "notepad", DEFAULT_STACK_SIZE);
  if (app.pid == 0) {
    KWARN("notepad_launch: failed to create process");
  }

  KINFO("Notepad launched (wid=%d, pid=%u)", notepad_wid, app.pid);
}

void notepad_launch_with_file(const char *vfs_path, const char *save_path) {
  /* Launch notepad if not already open */
  notepad_launch();
  if (notepad_wid < 0)
    return;

  /* If a persistent copy exists on disk, open that instead of the
     ramfs temp so the user's saved edits are preserved. */
  bool opened_persist = false;
  if (save_path && save_path[0]) {
    int fd = vfs_open(save_path, O_RDONLY);
    if (fd >= 0) {
      vfs_close(fd);
      notepad_open_file(save_path);
      opened_persist = true;
    }
  }
  if (!opened_persist) {
    notepad_open_file(vfs_path);
  }

  /* Override the save filename so Ctrl+S writes to persistent storage */
  if (save_path && save_path[0]) {
    int i = 0;
    while (save_path[i] && i < 63) {
      app.buffer.filename[i] = save_path[i];
      i++;
    }
    app.buffer.filename[i] = '\0';

    /* Also update the dialog path so the file dialog points to /home */
    notepad_dialog_path[0] = '/';
    notepad_dialog_path[1] = 'h';
    notepad_dialog_path[2] = 'o';
    notepad_dialog_path[3] = 'm';
    notepad_dialog_path[4] = 'e';
    notepad_dialog_path[5] = '\0';

    /* Update window title with the persistent name */
    window_t *win = gui_get_window(notepad_wid);
    if (win) {
      notepad_strcpy(win->title, "Notepad - ");
      int tlen = (int)np_strlen(win->title);
      int j = 0;
      while (save_path[j] && tlen < 63) {
        win->title[tlen++] = save_path[j++];
      }
      win->title[tlen] = '\0';
    }
  }
}

/* Key handling */

void notepad_handle_key(uint8_t scancode, char character) {
  if (notepad_wid < 0)
    return;
  window_t *win = gui_get_window(notepad_wid);
  if (!win)
    return;
  if (!(win->flags & WINDOW_FLAG_FOCUSED))
    return;

  /* Reset cursor blink */
  app.cursor_visible = true;
  app.last_blink_ms = timer_get_uptime_ms();

  /* File dialog takes priority */
  if (app.dialog_open) {
    notepad_dialog_handle_key(scancode, character);
    win->flags |= WINDOW_FLAG_DIRTY;
    return;
  }

  if (scancode == SC_F2) {
    if (notepad_toggle_ctxt_mode(win))
      return;
  }

  bool ctrl_held = keyboard_get_key_state(SC_LCTRL) || keyboard_get_ctrl();
  bool shift_held = keyboard_get_shift();

  if (ctrl_held && scancode == SC_KEY_R) {
    if (notepad_toggle_ctxt_mode(win))
      return;
  }

  /* Close dropdown on escape */
  if (scancode == SC_ESCAPE) {
    if (app.active_menu != MENU_NONE) {
      app.active_menu = MENU_NONE;
      win->flags |= WINDOW_FLAG_DIRTY;
      return;
    }
  }

  /* Check for Ctrl key combos.
   * The keyboard driver stores ctrl state via scancode 0x1D.
   * We check if ctrl is held by reading the key state or the
   * keyboard_get_ctrl() helper. */

  if (app.is_ctxt_file && app.render_mode) {
    if (ctrl_held && scancode == SC_KEY_S) {
      notepad_do_save();
      goto done;
    }

    if (scancode == SC_ARROW_UP) {
      app.ctxt_scroll_px -= FONT_H;
      if (app.ctxt_scroll_px < 0)
        app.ctxt_scroll_px = 0;
      goto done;
    }
    if (scancode == SC_ARROW_DOWN) {
      int max_scroll = notepad_ctxt_max_scroll(win);
      app.ctxt_scroll_px += FONT_H;
      if (app.ctxt_scroll_px > max_scroll)
        app.ctxt_scroll_px = max_scroll;
      goto done;
    }
    if (scancode == SC_PAGE_UP || scancode == SC_PAGE_DOWN ||
        scancode == SC_HOME || scancode == SC_END) {
      int vis_h = 0;
      int max_scroll = notepad_ctxt_max_scroll(win);
      notepad_get_viewport(NULL, NULL, NULL, NULL, NULL, &vis_h, win);

      if (scancode == SC_PAGE_UP) {
        app.ctxt_scroll_px -= vis_h;
      } else if (scancode == SC_PAGE_DOWN) {
        app.ctxt_scroll_px += vis_h;
      } else if (scancode == SC_HOME) {
        app.ctxt_scroll_px = 0;
      } else {
        app.ctxt_scroll_px = max_scroll;
      }

      if (app.ctxt_scroll_px < 0)
        app.ctxt_scroll_px = 0;
      if (app.ctxt_scroll_px > max_scroll)
        app.ctxt_scroll_px = max_scroll;
      goto done;
    }

    return;
  }

  if (ctrl_held) {
    /* Ctrl + letter: check scancode regardless of character value */
    switch (scancode) {
    case SC_KEY_N:
      notepad_do_new();
      goto done;
    case SC_KEY_O:
      notepad_do_open();
      goto done;
    case SC_KEY_S:
      notepad_do_save();
      goto done;
    case SC_KEY_Q:
      gui_destroy_window(notepad_wid);
      notepad_wid = -1;
      return;
    case SC_KEY_Z:
      notepad_do_undo();
      goto done;
    case SC_KEY_Y:
      notepad_do_redo();
      goto done;
    case SC_KEY_X:
      notepad_copy_selection();
      notepad_delete_selection();
      goto done;
    case SC_KEY_C:
      notepad_copy_selection();
      goto done;
    case SC_KEY_V: {
      const char *data = clipboard_get_data();
      if (data) {
        if (notepad_has_selection())
          notepad_delete_selection();
        else
          notepad_save_undo();
        int len = clipboard_get_length();
        for (int i = 0; i < len; i++) {
          if (data[i] == '\n')
            notepad_insert_newline();
          else if (data[i] >= 32 || data[i] == '\t')
            notepad_insert_char(data[i]);
        }
      }
      goto done;
    }
    case SC_KEY_A:
      notepad_select_all();
      goto done;
    case SC_KEY_EQUALS:
      if (app.font_scale < 3)
        app.font_scale++;
      goto done;
    case SC_KEY_MINUS:
      if (app.font_scale > 1)
        app.font_scale--;
      goto done;
    default:
      break;
    }
  }

  /* Also check for ctrl+letter with character 1-26 (ctrl codes) */
  if (ctrl_held && character >= 1 && character <= 26) {
    switch (character) {
    case 14:
      notepad_do_new();
      goto done; /* Ctrl+N */
    case 15:
      notepad_do_open();
      goto done; /* Ctrl+O */
    case 19:
      notepad_do_save();
      goto done; /* Ctrl+S */
    case 17:     /* Ctrl+Q */
      gui_destroy_window(notepad_wid);
      notepad_wid = -1;
      return;
    case 26:
      notepad_do_undo();
      goto done; /* Ctrl+Z */
    case 25:
      notepad_do_redo();
      goto done; /* Ctrl+Y */
    case 24:     /* Ctrl+X */
      notepad_copy_selection();
      notepad_delete_selection();
      goto done;
    case 3:
      notepad_copy_selection();
      goto done; /* Ctrl+C */
    case 22: {   /* Ctrl+V */
      const char *data = clipboard_get_data();
      if (data) {
        if (notepad_has_selection())
          notepad_delete_selection();
        else
          notepad_save_undo();
        int len = clipboard_get_length();
        for (int i = 0; i < len; i++) {
          if (data[i] == '\n')
            notepad_insert_newline();
          else if (data[i] >= 32 || data[i] == '\t')
            notepad_insert_char(data[i]);
        }
      }
      goto done;
    }
    case 1:
      notepad_select_all();
      goto done; /* Ctrl+A */
    }
  }

  /* Ctrl+=/- zoom: character-based fallback for keyboard layouts where
   * the scancode check above didn't match */
  if (ctrl_held && (character == '=' || character == '+')) {
    if (app.font_scale < 3)
      app.font_scale++;
    goto done;
  }
  if (ctrl_held && (character == '-' || character == '_')) {
    if (app.font_scale > 1)
      app.font_scale--;
    goto done;
  }

  /* Arrow keys with shift for selection */
  if (scancode == SC_ARROW_UP || scancode == SC_ARROW_DOWN ||
      scancode == SC_ARROW_LEFT || scancode == SC_ARROW_RIGHT) {

    if (shift_held) {
      /* Start or extend selection */
      if (!app.selection.active) {
        app.selection.active = true;
        app.selection.start_line = app.buffer.cursor_line;
        app.selection.start_col = app.buffer.cursor_col;
      }
    } else {
      notepad_clear_selection();
    }

    switch (scancode) {
    case SC_ARROW_UP:
      notepad_move_cursor(-1, 0);
      break;
    case SC_ARROW_DOWN:
      notepad_move_cursor(1, 0);
      break;
    case SC_ARROW_LEFT:
      notepad_move_cursor(0, -1);
      break;
    case SC_ARROW_RIGHT:
      notepad_move_cursor(0, 1);
      break;
    }

    if (shift_held) {
      app.selection.end_line = app.buffer.cursor_line;
      app.selection.end_col = app.buffer.cursor_col;
    }
    goto done;
  }

  /* Home / End */
  if (scancode == SC_HOME) {
    if (shift_held && !app.selection.active) {
      app.selection.active = true;
      app.selection.start_line = app.buffer.cursor_line;
      app.selection.start_col = app.buffer.cursor_col;
    } else if (!shift_held) {
      notepad_clear_selection();
    }
    app.buffer.cursor_col = 0;
    if (shift_held) {
      app.selection.end_line = app.buffer.cursor_line;
      app.selection.end_col = app.buffer.cursor_col;
    }
    notepad_ensure_cursor_visible();
    goto done;
  }

  if (scancode == SC_END) {
    if (shift_held && !app.selection.active) {
      app.selection.active = true;
      app.selection.start_line = app.buffer.cursor_line;
      app.selection.start_col = app.buffer.cursor_col;
    } else if (!shift_held) {
      notepad_clear_selection();
    }
    notepad_ensure_line(app.buffer.cursor_line);
    app.buffer.cursor_col =
        (int)np_strlen(app.buffer.lines[app.buffer.cursor_line]);
    if (shift_held) {
      app.selection.end_line = app.buffer.cursor_line;
      app.selection.end_col = app.buffer.cursor_col;
    }
    notepad_ensure_cursor_visible();
    goto done;
  }

  /* Page Up / Page Down */
  if (scancode == SC_PAGE_UP) {
    int vis_lines;
    notepad_get_viewport(NULL, &vis_lines, NULL, NULL, NULL, NULL, win);
    notepad_move_cursor(-vis_lines, 0);
    goto done;
  }
  if (scancode == SC_PAGE_DOWN) {
    int vis_lines;
    notepad_get_viewport(NULL, &vis_lines, NULL, NULL, NULL, NULL, win);
    notepad_move_cursor(vis_lines, 0);
    goto done;
  }

  /* Delete key */
  if (scancode == SC_DELETE) {
    notepad_delete_char();
    goto done;
  }

  /* Backspace */
  if (scancode == SC_BACKSPACE) {
    notepad_backspace();
    goto done;
  }

  /* Enter */
  if (scancode == SC_ENTER) {
    notepad_insert_newline();
    goto done;
  }

  /* Tab - insert 4 spaces */
  if (scancode == SC_TAB) {
    for (int i = 0; i < 4; i++)
      notepad_insert_char(' ');
    goto done;
  }

  /* Regular printable character (suppress if ctrl is held) */
  if (character >= 32 && character < 127 && !ctrl_held) {
    notepad_insert_char(character);
    goto done;
  }

  return;

done:
  win->flags |= WINDOW_FLAG_DIRTY;
}

/* Mouse handling */

void notepad_handle_mouse(int16_t mx, int16_t my, uint8_t buttons,
                          uint8_t prev_buttons) {
  if (notepad_wid < 0)
    return;
  window_t *win = gui_get_window(notepad_wid);
  if (!win)
    return;
  if (!(win->flags & WINDOW_FLAG_FOCUSED))
    return;
  if (win->flags & (WINDOW_FLAG_DRAGGING | WINDOW_FLAG_RESIZING))
    return;

  bool pressed = (buttons & 0x01) && !(prev_buttons & 0x01);
  bool held = (buttons & 0x01) != 0;
  bool released = !(buttons & 0x01) && (prev_buttons & 0x01);

  /* File dialog mouse handling */
  if (app.dialog_open) {
    notepad_dialog_handle_mouse(mx, my, buttons, prev_buttons, win);
    win->flags |= WINDOW_FLAG_DIRTY;
    return;
  }

  /* Check if click is in window content area */
  int content_x = (int)win->x + 1;
  int content_y = (int)win->y + TITLEBAR_H;
  int content_w = (int)win->width - 2;
  int content_h = (int)win->height - TITLEBAR_H - 1;

  if (mx < content_x || mx >= content_x + content_w || my < content_y ||
      my >= content_y + content_h) {
    if (app.is_ctxt_file && app.render_mode && app.ctxt_hover_link != -1) {
      app.ctxt_hover_link = -1;
      win->flags |= WINDOW_FLAG_DIRTY;
    }
    /* Close any open dropdown menu when clicking outside content area */
    if (pressed && app.active_menu != MENU_NONE) {
      app.active_menu = MENU_NONE;
      win->flags |= WINDOW_FLAG_DIRTY;
    }
    return;
  }

  int menu_y = (int)win->y + TITLEBAR_H;
  if (pressed && my >= menu_y && my < menu_y + MENUBAR_H) {
    int rel_x = mx - (int)win->x - 1;

    if (rel_x >= 2 && rel_x < 38) {
      app.active_menu = (app.active_menu == MENU_FILE) ? MENU_NONE : MENU_FILE;
      app.hover_item = -1;
    } else if (rel_x >= 40 && rel_x < 76) {
      app.active_menu = (app.active_menu == MENU_EDIT) ? MENU_NONE : MENU_EDIT;
      app.hover_item = -1;
    } else {
      app.active_menu = MENU_NONE;
    }
    win->flags |= WINDOW_FLAG_DIRTY;
    return;
  }

  if (app.active_menu != MENU_NONE) {
    int ddx, ddy, dd_w, item_count;

    if (app.active_menu == MENU_FILE) {
      ddx = (int)win->x + 3;
      ddy = (int)win->y + TITLEBAR_H + MENUBAR_H;
      dd_w = 150;
      item_count = FMENU_COUNT;
    } else {
      ddx = (int)win->x + 41;
      ddy = (int)win->y + TITLEBAR_H + MENUBAR_H;
      dd_w = 150;
      item_count = EMENU_COUNT;
    }

    int dd_h = item_count * 14 + 4;

    if (mx >= ddx && mx < ddx + dd_w && my >= ddy && my < ddy + dd_h) {
      int item = (my - ddy - 2) / 14;
      if (item >= 0 && item < item_count) {
        app.hover_item = item;

        if (pressed) {
          /* Check if it's a separator */
          bool is_sep = false;
          if (app.active_menu == MENU_FILE && item == FMENU_SEP)
            is_sep = true;
          if (app.active_menu == MENU_EDIT &&
              (item == EMENU_SEP1 || item == EMENU_SEP2))
            is_sep = true;

          if (!is_sep) {
            notepad_menu_action(app.active_menu, item);
          }
        }
      }
      win->flags |= WINDOW_FLAG_DIRTY;
      return;
    }

    /* Click outside dropdown closes it */
    if (pressed) {
      app.active_menu = MENU_NONE;
      win->flags |= WINDOW_FLAG_DIRTY;
      /* Fall through to handle the click normally */
    }
  }

  int edit_x, edit_y, edit_w, edit_h;
  int vis_cols, vis_lines;
  notepad_get_viewport(&vis_cols, &vis_lines, &edit_x, &edit_y, &edit_w,
                       &edit_h, win);

  /* Vertical scrollbar region */
  int vscroll_x = edit_x + edit_w;
  int vscroll_y = edit_y;
  int vscroll_h = edit_h;

  if (mx >= vscroll_x && mx < vscroll_x + VSCROLL_W && my >= vscroll_y &&
      my < vscroll_y + vscroll_h) {
    if (pressed) {
      if (my < vscroll_y + SCROLL_ARROW_SIZE) {
        /* Up arrow */
        if (app.is_ctxt_file && app.render_mode) {
          app.ctxt_scroll_px -= FONT_H;
          if (app.ctxt_scroll_px < 0)
            app.ctxt_scroll_px = 0;
        } else if (app.buffer.scroll_y > 0) {
          app.buffer.scroll_y--;
        }
      } else if (my >= vscroll_y + vscroll_h - SCROLL_ARROW_SIZE) {
        /* Down arrow */
        if (app.is_ctxt_file && app.render_mode) {
          int max_scroll = notepad_ctxt_max_scroll(win);
          app.ctxt_scroll_px += FONT_H;
          if (app.ctxt_scroll_px > max_scroll)
            app.ctxt_scroll_px = max_scroll;
        } else {
          int max_scroll = app.buffer.line_count - vis_lines;
          if (max_scroll < 0)
            max_scroll = 0;
          if (app.buffer.scroll_y < max_scroll)
            app.buffer.scroll_y++;
        }
      } else {
        /* Track click - page up/down */
        int track_mid = vscroll_y + vscroll_h / 2;
        if (app.is_ctxt_file && app.render_mode) {
          int max_scroll = notepad_ctxt_max_scroll(win);
          if (my < track_mid) {
            app.ctxt_scroll_px -= edit_h;
            if (app.ctxt_scroll_px < 0)
              app.ctxt_scroll_px = 0;
          } else {
            app.ctxt_scroll_px += edit_h;
            if (app.ctxt_scroll_px > max_scroll)
              app.ctxt_scroll_px = max_scroll;
          }
        } else {
          if (my < track_mid) {
            app.buffer.scroll_y -= vis_lines;
            if (app.buffer.scroll_y < 0)
              app.buffer.scroll_y = 0;
          } else {
            app.buffer.scroll_y += vis_lines;
            int max_scroll = app.buffer.line_count - vis_lines;
            if (max_scroll < 0)
              max_scroll = 0;
            if (app.buffer.scroll_y > max_scroll)
              app.buffer.scroll_y = max_scroll;
          }
        }
      }
    }
    win->flags |= WINDOW_FLAG_DIRTY;
    return;
  }

  /* Horizontal scrollbar region */
  int hscroll_x = edit_x;
  int hscroll_y = edit_y + edit_h;

  if (my >= hscroll_y && my < hscroll_y + HSCROLL_H && mx >= hscroll_x &&
      mx < hscroll_x + edit_w) {
    if (pressed) {
      if (app.is_ctxt_file && app.render_mode) {
        int max_scroll_x = notepad_ctxt_max_scroll_x(win);
        if (mx < hscroll_x + SCROLL_ARROW_SIZE) {
          if (app.ctxt_scroll_x_px > 0)
            app.ctxt_scroll_x_px -= FONT_W;
          if (app.ctxt_scroll_x_px < 0)
            app.ctxt_scroll_x_px = 0;
        } else if (mx >= hscroll_x + edit_w - SCROLL_ARROW_SIZE) {
          app.ctxt_scroll_x_px += FONT_W;
          if (app.ctxt_scroll_x_px > max_scroll_x)
            app.ctxt_scroll_x_px = max_scroll_x;
        } else {
          int track_mid = hscroll_x + edit_w / 2;
          if (mx < track_mid) {
            app.ctxt_scroll_x_px -= edit_w;
          } else {
            app.ctxt_scroll_x_px += edit_w;
          }
          if (app.ctxt_scroll_x_px < 0)
            app.ctxt_scroll_x_px = 0;
          if (app.ctxt_scroll_x_px > max_scroll_x)
            app.ctxt_scroll_x_px = max_scroll_x;
        }
      } else {
        if (mx < hscroll_x + SCROLL_ARROW_SIZE) {
          /* Left arrow */
          if (app.buffer.scroll_x > 0)
            app.buffer.scroll_x--;
        } else if (mx >= hscroll_x + edit_w - SCROLL_ARROW_SIZE) {
          /* Right arrow */
          int max_w = notepad_max_line_width();
          if (app.buffer.scroll_x < max_w - vis_cols)
            app.buffer.scroll_x++;
        } else {
          /* Track click - page left/right */
          int track_mid = hscroll_x + edit_w / 2;
          if (mx < track_mid) {
            app.buffer.scroll_x -= vis_cols;
            if (app.buffer.scroll_x < 0)
              app.buffer.scroll_x = 0;
          } else {
            app.buffer.scroll_x += vis_cols;
            int max_w = notepad_max_line_width();
            if (app.buffer.scroll_x > max_w - vis_cols)
              app.buffer.scroll_x = max_w - vis_cols;
            if (app.buffer.scroll_x < 0)
              app.buffer.scroll_x = 0;
          }
        }
      }
    }
    win->flags |= WINDOW_FLAG_DIRTY;
    return;
  }

  if (mx >= edit_x && mx < edit_x + edit_w && my >= edit_y &&
      my < edit_y + edit_h) {

    if (app.is_ctxt_file && app.render_mode) {
      int hit = notepad_ctxt_hit_link(mx, my);
      if (hit != app.ctxt_hover_link) {
        app.ctxt_hover_link = hit;
        win->flags |= WINDOW_FLAG_DIRTY;
      }
      if (pressed && hit >= 0 && hit < app.ctxt_link_count) {
        notepad_ctxt_open_link(app.ctxt_links[hit].target);
        win->flags |= WINDOW_FLAG_DIRTY;
      }
      win->flags |= WINDOW_FLAG_DIRTY;
      return;
    }

    int scale = app.font_scale;
    if (scale < 1)
      scale = 1;
    int click_col = (mx - edit_x) / (FONT_W * scale) + app.buffer.scroll_x;
    int click_line = (my - edit_y) / (FONT_H * scale) + app.buffer.scroll_y;

    /* Clamp */
    if (click_line >= app.buffer.line_count)
      click_line = app.buffer.line_count - 1;
    if (click_line < 0)
      click_line = 0;

    notepad_ensure_line(click_line);
    int len = (int)np_strlen(app.buffer.lines[click_line]);
    if (click_col > len)
      click_col = len;
    if (click_col < 0)
      click_col = 0;

    if (pressed) {
      /* Set cursor and start selection anchor */
      app.buffer.cursor_line = click_line;
      app.buffer.cursor_col = click_col;
      app.selection.active = true;
      app.selection.dragging = true;
      app.selection.start_line = click_line;
      app.selection.start_col = click_col;
      app.selection.end_line = click_line;
      app.selection.end_col = click_col;

      /* Reset cursor blink */
      app.cursor_visible = true;
      app.last_blink_ms = timer_get_uptime_ms();
    } else if (held && app.selection.dragging) {
      /* Extend selection */
      app.buffer.cursor_line = click_line;
      app.buffer.cursor_col = click_col;
      app.selection.end_line = click_line;
      app.selection.end_col = click_col;
    }

    if (released) {
      app.selection.dragging = false;
      /* If start == end, clear selection */
      if (app.selection.start_line == app.selection.end_line &&
          app.selection.start_col == app.selection.end_col) {
        app.selection.active = false;
      }
    }

    win->flags |= WINDOW_FLAG_DIRTY;
  }
}

/* Scroll wheel */

void notepad_handle_scroll(int delta) {
  if (notepad_wid < 0)
    return;
  window_t *win = gui_get_window(notepad_wid);
  if (!win)
    return;
  if (!(win->flags & WINDOW_FLAG_FOCUSED))
    return;

  /* If file dialog is open, scroll the file list instead */
  if (app.dialog_open) {
    dlg_layout_t L = notepad_dialog_get_layout(win);
    int max_scroll = app.dialog.file_count - L.items_visible;
    if (max_scroll < 0)
      max_scroll = 0;

    app.dialog.scroll_offset += delta;
    if (app.dialog.scroll_offset < 0)
      app.dialog.scroll_offset = 0;
    if (app.dialog.scroll_offset > max_scroll)
      app.dialog.scroll_offset = max_scroll;

    win->flags |= WINDOW_FLAG_DIRTY;
    return;
  }

  if (app.is_ctxt_file && app.render_mode) {
    if (keyboard_get_shift()) {
      int max_scroll_x = notepad_ctxt_max_scroll_x(win);
      app.ctxt_scroll_x_px += delta * FONT_W;
      if (app.ctxt_scroll_x_px > max_scroll_x)
        app.ctxt_scroll_x_px = max_scroll_x;
      if (app.ctxt_scroll_x_px < 0)
        app.ctxt_scroll_x_px = 0;
    } else {
      int max_scroll = notepad_ctxt_max_scroll(win);

      app.ctxt_scroll_px += delta * FONT_H;
      if (app.ctxt_scroll_px > max_scroll)
        app.ctxt_scroll_px = max_scroll;
      if (app.ctxt_scroll_px < 0)
        app.ctxt_scroll_px = 0;
    }

    win->flags |= WINDOW_FLAG_DIRTY;
    return;
  }

  if (keyboard_get_shift()) {
    int vis_cols = 0;
    notepad_get_viewport(&vis_cols, NULL, NULL, NULL, NULL, NULL, win);
    int max_w = notepad_max_line_width();
    int max_scroll_x = max_w - vis_cols;
    if (max_scroll_x < 0)
      max_scroll_x = 0;

    app.buffer.scroll_x += delta;
    if (app.buffer.scroll_x > max_scroll_x)
      app.buffer.scroll_x = max_scroll_x;
    if (app.buffer.scroll_x < 0)
      app.buffer.scroll_x = 0;

    win->flags |= WINDOW_FLAG_DIRTY;
    return;
  }

  app.buffer.scroll_y += delta;

  int vis_lines;
  notepad_get_viewport(NULL, &vis_lines, NULL, NULL, NULL, NULL, win);
  int max_scroll = app.buffer.line_count - vis_lines;
  if (max_scroll < 0)
    max_scroll = 0;

  if (app.buffer.scroll_y > max_scroll)
    app.buffer.scroll_y = max_scroll;
  if (app.buffer.scroll_y < 0)
    app.buffer.scroll_y = 0;

  win->flags |= WINDOW_FLAG_DIRTY;
}

/* Cursor blink tick */

void notepad_tick(void) {
  if (notepad_wid < 0)
    return;
  window_t *win = gui_get_window(notepad_wid);
  if (!win)
    return;
  if (!(win->flags & WINDOW_FLAG_FOCUSED))
    return;

  if (app.is_ctxt_file && app.render_mode)
    return;

  uint32_t now = timer_get_uptime_ms();
  if (now - app.last_blink_ms >= CURSOR_BLINK_MS) {
    app.cursor_visible = !app.cursor_visible;
    app.last_blink_ms = now;
    win->flags |= WINDOW_FLAG_DIRTY;
  }
}

/* Query */

int notepad_get_wid(void) { return notepad_wid; }
