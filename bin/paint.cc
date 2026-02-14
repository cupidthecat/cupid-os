//icon: "Paint"
//icon_desc: "CupidPaint - Drawing Program"
//icon_x: 10
//icon_y: 190
//icon_type: app
//icon_color: 0xFF6600

/* paint.cc — CupidPaint
 *
 * A Win95-style paint program for CupidOS.
 * REFACTOR: Removed preprocessor macros for CupidC compatibility.
 */

/* ── Constants ────────────────────────────────────────────────────── */

/* Replaced #define with global variables */
int TOOL_PENCIL = 0;
int TOOL_LINE = 1;
int TOOL_RECT = 2;
int TOOL_CIRCLE = 3;
int TOOL_FILL = 4;
int TOOL_SELECT = 5;

int TOOLBAR_H = 20;

int CANVAS_X = 56;
int CANVAS_Y = 20;
int CANVAS_W = 584;
int CANVAS_H = 428;

int TOOL_GRID_Y = 10;
int TOOL_BTN_W = 20;
int TOOL_BTN_H = 20;
int TOOL_GAP_X = 4;
int TOOL_GAP_Y = 4;

int BRUSH_PLUS_Y = 120;
int BRUSH_SIZE_Y = 145;
int BRUSH_MINUS_Y = 170;

int ZOOM_PLUS_Y = 205;
int ZOOM_SIZE_Y = 230;
int ZOOM_MINUS_Y = 255;

int CROP_Y = 290;
int RESIZE_UP_Y = 315;
int RESIZE_DOWN_Y = 340;

int SAVE_Y = 365;
int SAVE_AS_Y = 390;
int LOAD_Y = 415;

/* Palette colors (16 standard VGA/Win95 colors) */
int palette[16];

/* ── Global State ─────────────────────────────────────────────────── */

int canvas_surf = -1;
int current_tool = 0;         /* TOOL_PENCIL */
int current_color = 0x000000; /* Black */
int brush_size = 1;
int zoom_level = 1;
int view_x = 0;
int view_y = 0;

int* canvas_snapshot = 0;

int mouse_prev_x = 0;
int mouse_prev_y = 0;
int mouse_is_down = 0;

int drag_start_x = 0;
int drag_start_y = 0;
int is_dragging = 0;
int sel_active = 0;
int sel_x1 = 0;
int sel_y1 = 0;
int sel_x2 = 0;
int sel_y2 = 0;
int sel_move_active = 0;
int sel_move_off_x = 0;
int sel_move_off_y = 0;
int sel_move_draw_x = 0;
int sel_move_draw_y = 0;
int sel_buf_w = 0;
int sel_buf_h = 0;
int* sel_buffer = 0;

int pan_is_down = 0;
int pan_start_mouse_x = 0;
int pan_start_mouse_y = 0;
int pan_start_view_x = 0;
int pan_start_view_y = 0;
int canvas_dirty = 1;

/* Buffer for file I/O (One row at a time) */
int row_buffer[600];

/* Current file tracking for Save/Save As pattern */
char current_file_path[128];
int has_current_file = 0;

void reset_runtime_state() {
  current_tool = TOOL_PENCIL;
  current_color = 0x000000;
  brush_size = 1;
  zoom_level = 1;
  view_x = 0;
  view_y = 0;

  canvas_snapshot = 0;

  mouse_prev_x = 0;
  mouse_prev_y = 0;
  mouse_is_down = 0;

  drag_start_x = 0;
  drag_start_y = 0;
  is_dragging = 0;

  sel_active = 0;
  sel_x1 = 0;
  sel_y1 = 0;
  sel_x2 = 0;
  sel_y2 = 0;
  sel_move_active = 0;
  sel_move_off_x = 0;
  sel_move_off_y = 0;
  sel_move_draw_x = 0;
  sel_move_draw_y = 0;
  sel_buf_w = 0;
  sel_buf_h = 0;
  sel_buffer = 0;

  pan_is_down = 0;
  pan_start_mouse_x = 0;
  pan_start_mouse_y = 0;
  pan_start_view_x = 0;
  pan_start_view_y = 0;
  canvas_dirty = 1;

  current_file_path[0] = '\0';
  has_current_file = 0;
}

/* ── Initialization ───────────────────────────────────────────────── */

void init_palette() {
  palette[0] = 0x000000;  /* Black */
  palette[1] = 0x808080;  /* Dark Gray */
  palette[2] = 0xC0C0C0;  /* Light Gray */
  palette[3] = 0xFFFFFF;  /* White */
  palette[4] = 0x800000;  /* Maroon */
  palette[5] = 0xFF0000;  /* Red */
  palette[6] = 0x808000;  /* Olive */
  palette[7] = 0xFFFF00;  /* Yellow */
  palette[8] = 0x008000;  /* Green */
  palette[9] = 0x00FF00;  /* Lime */
  palette[10] = 0x008080; /* Teal */
  palette[11] = 0x00FFFF; /* Aqua */
  palette[12] = 0x000080; /* Navy */
  palette[13] = 0x0000FF; /* Blue */
  palette[14] = 0x800080; /* Purple */
  palette[15] = 0xFF00FF; /* Fuchsia */
}

/* ── Helpers ──────────────────────────────────────────────────────── */

/* Simple sleep wrapper if needed, or rely on built-in if available */
/* We will just use 'yield()' loops for now if sleep isn't guaranteed,
   but 'sleep_ms' is likely available in the symbol table. */
void my_sleep(int ms) {
  /* spin or sleep */
  /* sleep_ms(ms); -- assuming implicit declaration works or built-in */
  /* For safety in small-C, checking if we can just return. */
}

void settle_selection_before_file_op() {
  if (sel_move_active == 0)
    return;

  if (sel_buffer == 0 || sel_buf_w <= 0 || sel_buf_h <= 0) {
    sel_move_active = 0;
    if (sel_buffer) {
      kfree(sel_buffer);
      sel_buffer = 0;
    }
    sel_buf_w = 0;
    sel_buf_h = 0;
    return;
  }

  gfx2d_surface_set_active(canvas_surf);
  int y = 0;
  while (y < sel_buf_h) {
    int x = 0;
    while (x < sel_buf_w) {
      gfx2d_pixel(sel_move_draw_x + x, sel_move_draw_y + y,
                  sel_buffer[y * sel_buf_w + x]);
      x++;
    }
    y++;
  }
  gfx2d_surface_unset_active();

  sel_x1 = sel_move_draw_x;
  sel_y1 = sel_move_draw_y;
  sel_x2 = sel_move_draw_x + sel_buf_w - 1;
  sel_y2 = sel_move_draw_y + sel_buf_h - 1;
  sel_active = 1;
  sel_move_active = 0;
  canvas_dirty = 1;

  kfree(sel_buffer);
  sel_buffer = 0;
  sel_buf_w = 0;
  sel_buf_h = 0;
}

/* ── File I/O ─────────────────────────────────────────────────────── */

void show_message(char* msg, int color) {
  gfx2d_rect_fill(CANVAS_X + 10, CANVAS_Y + 10, 100, 30, color);
  gfx2d_text(CANVAS_X + 20, CANVAS_Y + 20, msg, 0x000000, 1);
  gfx2d_flip();
}

int save_to_bmp(char* path) {
  int w = CANVAS_W;
  int h = CANVAS_H;

  /* Allocate buffer for canvas pixels (600x448x4 = ~1MB) */
  int* pixel_buffer = (int*)kmalloc(w * h * 4);
  if (pixel_buffer == 0) {
    return -1;
  }

  /* Read all pixels from canvas surface */
  gfx2d_surface_set_active(canvas_surf);
  gfx2d_clip_clear();
  gfx2d_blend_mode(0);

  int y = 0;
  while (y < h) {
    int x = 0;
    while (x < w) {
      pixel_buffer[y * w + x] = gfx2d_getpixel(x, y);
      x++;
    }
    y++;
  }

  gfx2d_surface_unset_active();

  /* Encode to BMP file */
  int result = bmp_encode(path, pixel_buffer, w, h);

  /* Verify by decoding and comparing (prevents silent corrupted saves) */
  if (result == 0) {
    int info[4];
    if (bmp_get_info(path, info) != 0 || info[0] != w || info[1] != h) {
      result = -1;
    } else {
      int* verify_buffer = (int*)kmalloc(info[3]);
      if (verify_buffer == 0) {
        result = -1;
      } else {
        if (bmp_decode(path, verify_buffer, info[3]) != 0) {
          result = -1;
        } else {
          int i = 0;
          int total = w * h;
          while (i < total) {
            if ((verify_buffer[i] & 0x00FFFFFF) != (pixel_buffer[i] & 0x00FFFFFF)) {
              result = -1;
              break;
            }
            i++;
          }
        }
        kfree(verify_buffer);
      }
    }

    /* Retry once if verification failed */
    if (result != 0) {
      result = bmp_encode(path, pixel_buffer, w, h);
      if (result == 0) {
        int info2[4];
        if (bmp_get_info(path, info2) != 0 || info2[0] != w || info2[1] != h) {
          result = -1;
        } else {
          int* verify2 = (int*)kmalloc(info2[3]);
          if (verify2 == 0) {
            result = -1;
          } else {
            if (bmp_decode(path, verify2, info2[3]) != 0) {
              result = -1;
            } else {
              int j = 0;
              int total2 = w * h;
              while (j < total2) {
                if ((verify2[j] & 0x00FFFFFF) != (pixel_buffer[j] & 0x00FFFFFF)) {
                  result = -1;
                  break;
                }
                j++;
              }
            }
            kfree(verify2);
          }
        }
      }
    }
  }

  /* Free the buffer */
  kfree(pixel_buffer);

  if (result == 0) {
    return 0;
  }
  return -1;
}

int load_from_bmp(char* path) {
  /* Get BMP info (bmp_info_t has 4 uint32 fields: width, height, bpp, data_size) */
  int info[4];
  if (bmp_get_info(path, info) != 0) {
    return -1;
  }

  int bmp_w = info[0];
  int bmp_h = info[1];
  int data_size = info[3];

  /* Allocate buffer for decoded pixels */
  int* pixel_buffer = (int*)kmalloc(data_size);
  if (pixel_buffer == 0) {
    return -1;
  }

  /* Decode BMP into buffer */
  if (bmp_decode(path, pixel_buffer, data_size) != 0) {
    kfree(pixel_buffer);
    return -1;
  }

  /* Clear canvas to white first */
  gfx2d_surface_set_active(canvas_surf);
  gfx2d_clip_clear();
  gfx2d_blend_mode(0);
  gfx2d_clear(0xFFFFFF);

  /* Draw pixels onto canvas (clamp to canvas bounds) */
  int draw_h = bmp_h;
  if (draw_h > CANVAS_H) draw_h = CANVAS_H;
  int draw_w = bmp_w;
  if (draw_w > CANVAS_W) draw_w = CANVAS_W;

  int y = 0;
  while (y < draw_h) {
    int x = 0;
    while (x < draw_w) {
      gfx2d_pixel(x, y, pixel_buffer[y * bmp_w + x]);
      x++;
    }
    y++;
  }

  gfx2d_surface_unset_active();

  /* Free the buffer */
  kfree(pixel_buffer);

  canvas_dirty = 1;

  return 0;
}

void save_drawing() {
  char path[128];

  settle_selection_before_file_op();

  /* If we have a current file, save directly without dialog */
  if (has_current_file) {
    int i = 0;
    while (i < 128) {
      path[i] = current_file_path[i];
      if (path[i] == 0) break;
      i++;
    }
  } else {
    /* No current file - show save dialog */
    int result = file_dialog_save("/home", "untitled.bmp", path, ".bmp");
    if (result != 1) {
      return;
    }
  }

  /* Perform the save */
  if (save_to_bmp(path) == 0) {
    /* Success - update current file */
    int i = 0;
    while (i < 128 && path[i] != 0) {
      current_file_path[i] = path[i];
      i++;
    }
    current_file_path[i] = '\0';
    has_current_file = 1;

    show_message("Saved!", 0x00FF00);
  } else {
    show_message("Error!", 0xFF0000);
  }
}

void save_drawing_as() {
  char path[128];

  settle_selection_before_file_op();

  /* Always show dialog for Save As */
  int result = file_dialog_save("/home", "untitled.bmp", path, ".bmp");
  if (result != 1) {
    return;
  }

  /* Perform the save */
  if (save_to_bmp(path) == 0) {
    /* Success - update current file */
    int i = 0;
    while (i < 128 && path[i] != 0) {
      current_file_path[i] = path[i];
      i++;
    }
    current_file_path[i] = '\0';
    has_current_file = 1;

    show_message("Saved!", 0x00FF00);
  } else {
    show_message("Error!", 0xFF0000);
  }
}

void load_drawing() {
  char path[128];

  settle_selection_before_file_op();

  /* Show open dialog */
  int result = file_dialog_open("/home", path, ".bmp");
  if (result != 1) {
    return;
  }

  /* Load the BMP */
  if (load_from_bmp(path) == 0) {
    /* Success - update current file */
    int i = 0;
    while (i < 128 && path[i] != 0) {
      current_file_path[i] = path[i];
      i++;
    }
    current_file_path[i] = '\0';
    has_current_file = 1;

    show_message("Loaded!", 0x00FF00);
  } else {
    show_message("Error!", 0xFF0000);
  }
}

/* ── UI Drawing ───────────────────────────────────────────────────── */

void draw_toolbar() {
  /* Side toolbar background (below app toolbar) */
  gfx2d_panel(0, TOOLBAR_H, CANVAS_X, 480 - TOOLBAR_H);

  /* Tools: 2 columns x 3 rows (P, L, R, C, F, S) */
  int i = 0;
  while (i < 6) {
    int col = i % 2;
    int row = i / 2;
    int x = 4 + col * (TOOL_BTN_W + TOOL_GAP_X);
    int y = TOOLBAR_H + TOOL_GRID_Y + row * (TOOL_BTN_H + TOOL_GAP_Y);
    int selected = 0;
    if (current_tool == i)
      selected = 1;

    gfx2d_bevel(x, y, TOOL_BTN_W, TOOL_BTN_H, !selected);

    if (i == 0)
      gfx2d_text(x + 7, y + 6, "P", 0x000000, 1);
    if (i == 1)
      gfx2d_text(x + 7, y + 6, "L", 0x000000, 1);
    if (i == 2)
      gfx2d_text(x + 7, y + 6, "R", 0x000000, 1);
    if (i == 3)
      gfx2d_text(x + 7, y + 6, "C", 0x000000, 1);
    if (i == 4)
      gfx2d_text(x + 7, y + 6, "F", 0x000000, 1);
    if (i == 5)
      gfx2d_text(x + 7, y + 6, "S", 0x000000, 1);

    i++;
  }

  /* Brush Size Controls */
  /* [+] */
  gfx2d_bevel(4, TOOLBAR_H + BRUSH_PLUS_Y, 48, 20, 1);
  gfx2d_text(15, TOOLBAR_H + BRUSH_PLUS_Y + 6, "+", 0x000000, 1);

  /* Size Indicator */
  char size_str[4];
  size_str[0] = ' ';
  size_str[1] = ' ';
  size_str[2] = 0;

  if (brush_size < 10) {
    size_str[0] = '0' + brush_size;
    size_str[1] = 0;
  } else {
    size_str[0] = '1';
    size_str[1] = '0';
    size_str[2] = 0;
  }

  gfx2d_rect_fill(10, TOOLBAR_H + BRUSH_SIZE_Y, 30, 10, 0xC0C0C0);
  gfx2d_text(15, TOOLBAR_H + BRUSH_SIZE_Y, size_str, 0x000000, 0);

  /* [-] */
  gfx2d_bevel(4, TOOLBAR_H + BRUSH_MINUS_Y, 48, 20, 1);
  gfx2d_text(15, TOOLBAR_H + BRUSH_MINUS_Y + 6, "-", 0x000000, 1);

  /* Zoom Controls */
  gfx2d_bevel(4, TOOLBAR_H + ZOOM_PLUS_Y, 48, 20, 1);
  gfx2d_text(15, TOOLBAR_H + ZOOM_PLUS_Y + 6, "+", 0x000000, 1);

  char zoom_str[4];
  zoom_str[0] = '1';
  zoom_str[1] = 'x';
  zoom_str[2] = 0;
  if (zoom_level == 2)
    zoom_str[0] = '2';
  if (zoom_level == 3)
    zoom_str[0] = '3';
  if (zoom_level == 4)
    zoom_str[0] = '4';

  gfx2d_rect_fill(9, TOOLBAR_H + ZOOM_SIZE_Y, 34, 10, 0xC0C0C0);
  gfx2d_text(10, TOOLBAR_H + ZOOM_SIZE_Y, zoom_str, 0x000000, 0);

  gfx2d_bevel(4, TOOLBAR_H + ZOOM_MINUS_Y, 48, 20, 1);
  gfx2d_text(15, TOOLBAR_H + ZOOM_MINUS_Y + 6, "-", 0x000000, 1);

  /* Crop / Resize */
  gfx2d_bevel(4, TOOLBAR_H + CROP_Y, 48, 20, 1);
  gfx2d_text(8, TOOLBAR_H + CROP_Y + 6, "CR", 0x000000, 0);

  gfx2d_bevel(4, TOOLBAR_H + RESIZE_UP_Y, 48, 20, 1);
  gfx2d_text(8, TOOLBAR_H + RESIZE_UP_Y + 6, "R+", 0x000000, 0);

  gfx2d_bevel(4, TOOLBAR_H + RESIZE_DOWN_Y, 48, 20, 1);
  gfx2d_text(8, TOOLBAR_H + RESIZE_DOWN_Y + 6, "R-", 0x000000, 0);

  /* Save/Load Buttons */
  gfx2d_bevel(4, TOOLBAR_H + SAVE_Y, 48, 20, 1);
  gfx2d_text(8, TOOLBAR_H + SAVE_Y + 6, "SV", 0x000000, 0);

  /* Save As button */
  gfx2d_bevel(4, TOOLBAR_H + SAVE_AS_Y, 48, 20, 1);
  gfx2d_text(8, TOOLBAR_H + SAVE_AS_Y + 6, "SA", 0x000000, 0);

  /* Load button */
  gfx2d_bevel(4, TOOLBAR_H + LOAD_Y, 48, 20, 1);
  gfx2d_text(8, TOOLBAR_H + LOAD_Y + 6, "LD", 0x000000, 0);
}

void draw_palette() {
  /* Palette background */
  int palette_y = CANVAS_Y + CANVAS_H;
  gfx2d_panel(CANVAS_X, palette_y, CANVAS_W, 32);

  int i = 0;
  while (i < 16) {
    int x = CANVAS_X + 4 + i * 32;
    int y = palette_y + 4;

    /* Highlight selected color */
    if (palette[i] == current_color) {
      gfx2d_rect(x - 2, y - 2, 28, 28, 0xFF0000); /* Red border */
    }

    /* Color box */
    gfx2d_rect_fill(x, y, 24, 24, palette[i]);
    gfx2d_bevel(x, y, 24, 24, 0); /* Sunken */
    i++;
  }
}

int screen_to_canvas_x(int sx) {
  if (sx < CANVAS_X)
    return -1;
  return view_x + (sx - CANVAS_X) / zoom_level;
}

int screen_to_canvas_y(int sy) {
  if (sy < CANVAS_Y)
    return -1;
  return view_y + (sy - CANVAS_Y) / zoom_level;
}

int canvas_to_screen_x(int cx) {
  return CANVAS_X + (cx - view_x) * zoom_level;
}

int canvas_to_screen_y(int cy) {
  return CANVAS_Y + (cy - view_y) * zoom_level;
}

void normalize_rect(int* x1, int* y1, int* x2, int* y2) {
  if (*x2 < *x1) {
    int t = *x1;
    *x1 = *x2;
    *x2 = t;
  }
  if (*y2 < *y1) {
    int t = *y1;
    *y1 = *y2;
    *y2 = t;
  }
}

int point_in_selection(int cx, int cy) {
  if (sel_active == 0)
    return 0;

  int x1 = sel_x1;
  int y1 = sel_y1;
  int x2 = sel_x2;
  int y2 = sel_y2;
  normalize_rect(&x1, &y1, &x2, &y2);

  if (cx < x1 || cx > x2 || cy < y1 || cy > y2)
    return 0;
  return 1;
}

void selection_move_start(int cx, int cy) {
  int x1 = sel_x1;
  int y1 = sel_y1;
  int x2 = sel_x2;
  int y2 = sel_y2;
  normalize_rect(&x1, &y1, &x2, &y2);

  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 >= CANVAS_W) x2 = CANVAS_W - 1;
  if (y2 >= CANVAS_H) y2 = CANVAS_H - 1;

  sel_buf_w = x2 - x1 + 1;
  sel_buf_h = y2 - y1 + 1;
  if (sel_buf_w <= 0 || sel_buf_h <= 0)
    return;

  if (sel_buffer)
    kfree(sel_buffer);
  sel_buffer = (int*)kmalloc(sel_buf_w * sel_buf_h * 4);
  if (sel_buffer == 0) {
    sel_buf_w = 0;
    sel_buf_h = 0;
    return;
  }

  gfx2d_surface_set_active(canvas_surf);

  int y = 0;
  while (y < sel_buf_h) {
    int x = 0;
    while (x < sel_buf_w) {
      sel_buffer[y * sel_buf_w + x] = gfx2d_getpixel(x1 + x, y1 + y);
      x++;
    }
    y++;
  }

  /* Clear lifted area */
  y = 0;
  while (y < sel_buf_h) {
    int x = 0;
    while (x < sel_buf_w) {
      gfx2d_pixel(x1 + x, y1 + y, 0xFFFFFF);
      x++;
    }
    y++;
  }

  gfx2d_surface_unset_active();

  sel_move_active = 1;
  sel_move_off_x = cx - x1;
  sel_move_off_y = cy - y1;
  sel_move_draw_x = x1;
  sel_move_draw_y = y1;
  canvas_dirty = 1;
}

void selection_move_update(int cx, int cy) {
  if (sel_move_active == 0)
    return;

  int nx = cx - sel_move_off_x;
  int ny = cy - sel_move_off_y;

  if (nx < 0)
    nx = 0;
  if (ny < 0)
    ny = 0;
  if (nx + sel_buf_w > CANVAS_W)
    nx = CANVAS_W - sel_buf_w;
  if (ny + sel_buf_h > CANVAS_H)
    ny = CANVAS_H - sel_buf_h;

  sel_move_draw_x = nx;
  sel_move_draw_y = ny;
}

void selection_move_commit() {
  if (sel_move_active == 0 || sel_buffer == 0)
    return;

  gfx2d_surface_set_active(canvas_surf);
  int y = 0;
  while (y < sel_buf_h) {
    int x = 0;
    while (x < sel_buf_w) {
      gfx2d_pixel(sel_move_draw_x + x, sel_move_draw_y + y,
                  sel_buffer[y * sel_buf_w + x]);
      x++;
    }
    y++;
  }
  gfx2d_surface_unset_active();

  sel_x1 = sel_move_draw_x;
  sel_y1 = sel_move_draw_y;
  sel_x2 = sel_move_draw_x + sel_buf_w - 1;
  sel_y2 = sel_move_draw_y + sel_buf_h - 1;
  sel_active = 1;
  sel_move_active = 0;
  canvas_dirty = 1;

  if (sel_buffer) {
    kfree(sel_buffer);
    sel_buffer = 0;
  }
  sel_buf_w = 0;
  sel_buf_h = 0;
}

void crop_to_selection() {
  if (sel_active == 0)
    return;

  if (sel_move_active) {
    selection_move_commit();
  }

  int x1 = sel_x1;
  int y1 = sel_y1;
  int x2 = sel_x2;
  int y2 = sel_y2;
  normalize_rect(&x1, &y1, &x2, &y2);

  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 >= CANVAS_W) x2 = CANVAS_W - 1;
  if (y2 >= CANVAS_H) y2 = CANVAS_H - 1;

  int cw = x2 - x1 + 1;
  int ch = y2 - y1 + 1;
  if (cw <= 0 || ch <= 0)
    return;

  int* tmp = (int*)kmalloc(CANVAS_W * CANVAS_H * 4);
  if (tmp == 0)
    return;

  gfx2d_surface_set_active(canvas_surf);
  int y = 0;
  while (y < ch) {
    int x = 0;
    while (x < cw) {
      tmp[y * CANVAS_W + x] = gfx2d_getpixel(x1 + x, y1 + y);
      x++;
    }
    y++;
  }

  gfx2d_clear(0xFFFFFF);
  y = 0;
  while (y < ch) {
    int x = 0;
    while (x < cw) {
      gfx2d_pixel(x, y, tmp[y * CANVAS_W + x]);
      x++;
    }
    y++;
  }
  gfx2d_surface_unset_active();

  kfree(tmp);
  sel_active = 0;
  view_x = 0;
  view_y = 0;
  canvas_dirty = 1;
}

void resize_canvas_200() {
  if (sel_move_active) {
    selection_move_commit();
  }

  if (sel_active) {
    int x1 = sel_x1;
    int y1 = sel_y1;
    int x2 = sel_x2;
    int y2 = sel_y2;
    normalize_rect(&x1, &y1, &x2, &y2);

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= CANVAS_W) x2 = CANVAS_W - 1;
    if (y2 >= CANVAS_H) y2 = CANVAS_H - 1;

    int src_w = x2 - x1 + 1;
    int src_h = y2 - y1 + 1;
    if (src_w <= 0 || src_h <= 0)
      return;

    int dst_w = src_w * 2;
    int dst_h = src_h * 2;
    if (x1 + dst_w > CANVAS_W)
      dst_w = CANVAS_W - x1;
    if (y1 + dst_h > CANVAS_H)
      dst_h = CANVAS_H - y1;
    if (dst_w <= 0 || dst_h <= 0)
      return;

    int* src = (int*)kmalloc(src_w * src_h * 4);
    if (src == 0)
      return;

    gfx2d_surface_set_active(canvas_surf);
    int y = 0;
    while (y < src_h) {
      int x = 0;
      while (x < src_w) {
        src[y * src_w + x] = gfx2d_getpixel(x1 + x, y1 + y);
        x++;
      }
      y++;
    }

    /* Clear old selection area */
    y = 0;
    while (y < src_h) {
      int x = 0;
      while (x < src_w) {
        gfx2d_pixel(x1 + x, y1 + y, 0xFFFFFF);
        x++;
      }
      y++;
    }

    /* Draw stretched selection */
    y = 0;
    while (y < dst_h) {
      int sy = (y * src_h) / dst_h;
      int x = 0;
      while (x < dst_w) {
        int sx = (x * src_w) / dst_w;
        gfx2d_pixel(x1 + x, y1 + y, src[sy * src_w + sx]);
        x++;
      }
      y++;
    }
    gfx2d_surface_unset_active();

    kfree(src);
    sel_x1 = x1;
    sel_y1 = y1;
    sel_x2 = x1 + dst_w - 1;
    sel_y2 = y1 + dst_h - 1;
    canvas_dirty = 1;
    return;
  }

  int* src = (int*)kmalloc(CANVAS_W * CANVAS_H * 4);
  if (src == 0)
    return;

  gfx2d_surface_set_active(canvas_surf);
  int y = 0;
  while (y < CANVAS_H) {
    int x = 0;
    while (x < CANVAS_W) {
      src[y * CANVAS_W + x] = gfx2d_getpixel(x, y);
      x++;
    }
    y++;
  }

  gfx2d_clear(0xFFFFFF);
  y = 0;
  while (y < CANVAS_H) {
    int sy = y / 2;
    int x = 0;
    while (x < CANVAS_W) {
      int sx = x / 2;
      gfx2d_pixel(x, y, src[sy * CANVAS_W + sx]);
      x++;
    }
    y++;
  }
  gfx2d_surface_unset_active();

  kfree(src);
  sel_active = 0;
  canvas_dirty = 1;
}

void resize_canvas_50() {
  if (sel_move_active) {
    selection_move_commit();
  }

  if (sel_active) {
    int x1 = sel_x1;
    int y1 = sel_y1;
    int x2 = sel_x2;
    int y2 = sel_y2;
    normalize_rect(&x1, &y1, &x2, &y2);

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= CANVAS_W) x2 = CANVAS_W - 1;
    if (y2 >= CANVAS_H) y2 = CANVAS_H - 1;

    int src_w = x2 - x1 + 1;
    int src_h = y2 - y1 + 1;
    if (src_w <= 0 || src_h <= 0)
      return;

    int dst_w = src_w / 2;
    int dst_h = src_h / 2;
    if (dst_w < 1)
      dst_w = 1;
    if (dst_h < 1)
      dst_h = 1;
    if (x1 + dst_w > CANVAS_W)
      dst_w = CANVAS_W - x1;
    if (y1 + dst_h > CANVAS_H)
      dst_h = CANVAS_H - y1;
    if (dst_w <= 0 || dst_h <= 0)
      return;

    int* src = (int*)kmalloc(src_w * src_h * 4);
    if (src == 0)
      return;

    gfx2d_surface_set_active(canvas_surf);
    int y = 0;
    while (y < src_h) {
      int x = 0;
      while (x < src_w) {
        src[y * src_w + x] = gfx2d_getpixel(x1 + x, y1 + y);
        x++;
      }
      y++;
    }

    /* Clear old selection area */
    y = 0;
    while (y < src_h) {
      int x = 0;
      while (x < src_w) {
        gfx2d_pixel(x1 + x, y1 + y, 0xFFFFFF);
        x++;
      }
      y++;
    }

    /* Draw shrunken selection */
    y = 0;
    while (y < dst_h) {
      int sy = (y * src_h) / dst_h;
      int x = 0;
      while (x < dst_w) {
        int sx = (x * src_w) / dst_w;
        gfx2d_pixel(x1 + x, y1 + y, src[sy * src_w + sx]);
        x++;
      }
      y++;
    }
    gfx2d_surface_unset_active();

    kfree(src);
    sel_x1 = x1;
    sel_y1 = y1;
    sel_x2 = x1 + dst_w - 1;
    sel_y2 = y1 + dst_h - 1;
    canvas_dirty = 1;
    return;
  }

  int* src = (int*)kmalloc(CANVAS_W * CANVAS_H * 4);
  if (src == 0)
    return;

  gfx2d_surface_set_active(canvas_surf);
  int y = 0;
  while (y < CANVAS_H) {
    int x = 0;
    while (x < CANVAS_W) {
      src[y * CANVAS_W + x] = gfx2d_getpixel(x, y);
      x++;
    }
    y++;
  }

  gfx2d_clear(0xFFFFFF);
  int new_w = CANVAS_W / 2;
  int new_h = CANVAS_H / 2;
  y = 0;
  while (y < new_h) {
    int x = 0;
    while (x < new_w) {
      gfx2d_pixel(x, y, src[(y * 2) * CANVAS_W + (x * 2)]);
      x++;
    }
    y++;
  }
  gfx2d_surface_unset_active();

  kfree(src);
  sel_active = 0;
  canvas_dirty = 1;
}

void draw_selection_overlay() {
  if (sel_active == 0)
    return;

  int x1 = sel_x1;
  int y1 = sel_y1;
  int x2 = sel_x2;
  int y2 = sel_y2;

  if (sel_move_active && sel_buffer) {
    x1 = sel_move_draw_x;
    y1 = sel_move_draw_y;
    x2 = sel_move_draw_x + sel_buf_w - 1;
    y2 = sel_move_draw_y + sel_buf_h - 1;

    int dy = 0;
    while (dy < sel_buf_h) {
      int sy = canvas_to_screen_y(y1 + dy);
      int dx = 0;
      while (dx < sel_buf_w) {
        int sx = canvas_to_screen_x(x1 + dx);
        int col = sel_buffer[dy * sel_buf_w + dx];
        if (zoom_level <= 1) {
          gfx2d_pixel(sx, sy, col);
        } else {
          gfx2d_rect_fill(sx, sy, zoom_level, zoom_level, col);
        }
        dx++;
      }
      dy++;
    }
  }

  normalize_rect(&x1, &y1, &x2, &y2);

  int sx1 = canvas_to_screen_x(x1);
  int sy1 = canvas_to_screen_y(y1);
  int sx2 = canvas_to_screen_x(x2);
  int sy2 = canvas_to_screen_y(y2);
  gfx2d_rect(sx1, sy1, sx2 - sx1 + zoom_level, sy2 - sy1 + zoom_level, 0x0066AAFF);
}

void refresh_canvas_snapshot() {
  if (canvas_snapshot == 0)
    return;

  gfx2d_surface_set_active(canvas_surf);
  int y = 0;
  while (y < CANVAS_H) {
    int x = 0;
    while (x < CANVAS_W) {
      canvas_snapshot[y * CANVAS_W + x] = gfx2d_getpixel(x, y);
      x++;
    }
    y++;
  }
  gfx2d_surface_unset_active();
  canvas_dirty = 0;
}

void clamp_view_origin() {
  int view_w = CANVAS_W / zoom_level;
  int view_h = CANVAS_H / zoom_level;

  if (view_w < 1)
    view_w = 1;
  if (view_h < 1)
    view_h = 1;

  int max_x = CANVAS_W - view_w;
  int max_y = CANVAS_H - view_h;

  if (max_x < 0)
    max_x = 0;
  if (max_y < 0)
    max_y = 0;

  if (view_x < 0)
    view_x = 0;
  if (view_y < 0)
    view_y = 0;
  if (view_x > max_x)
    view_x = max_x;
  if (view_y > max_y)
    view_y = max_y;
}

void draw_canvas_view() {
  clamp_view_origin();

  if (zoom_level <= 1) {
    gfx2d_surface_blit(canvas_surf, CANVAS_X, CANVAS_Y);
    return;
  }

  if (canvas_snapshot == 0)
    return;

  if (canvas_dirty)
    refresh_canvas_snapshot();

  int y = 0;

  y = 0;
  while (y < CANVAS_H) {
    int src_y = view_y + y / zoom_level;
    int x = 0;
    while (x < CANVAS_W) {
      int src_x = view_x + x / zoom_level;
      int color = canvas_snapshot[src_y * CANVAS_W + src_x];
      gfx2d_pixel(CANVAS_X + x, CANVAS_Y + y, color);
      x++;
    }
    y++;
  }
}

/* ── Tool Logic ───────────────────────────────────────────────────── */

void use_tool(int x, int y, int dragging) {
  /* Adjust to canvas coordinates */
  int cx = screen_to_canvas_x(x);
  int cy = screen_to_canvas_y(y);

  if (cx < 0 || cy < 0 || cx >= CANVAS_W || cy >= CANVAS_H)
    return;

  gfx2d_surface_set_active(canvas_surf);

  if (current_tool == TOOL_PENCIL) {
    int prev_cx = screen_to_canvas_x(mouse_prev_x);
    int prev_cy = screen_to_canvas_y(mouse_prev_y);
    if (prev_cx >= 0 && prev_cx < CANVAS_W && prev_cy >= 0 && prev_cy < CANVAS_H) {
      if (brush_size == 1) {
        gfx2d_line(prev_cx, prev_cy, cx, cy, current_color);
      } else {
        /* Simulated thick line */
        int x1 = prev_cx;
        int y1 = prev_cy;
        int x2 = cx;
        int y2 = cy;

        int dx = x2 - x1;
        int dy = y2 - y1;
        int steps = 0;
        if (dx * dx > dy * dy) {
          if (dx > 0)
            steps = dx;
          else
            steps = -dx;
        } else {
          if (dy > 0)
            steps = dy;
          else
            steps = -dy;
        }

        if (steps == 0) {
          gfx2d_circle_fill(x2, y2, brush_size, current_color);
        } else {
          int i = 0;
          while (i <= steps) {
            int px = x1 + (dx * i) / steps;
            int py = y1 + (dy * i) / steps;
            gfx2d_circle_fill(px, py, brush_size, current_color);
            i++;
          }
        }
      }
    } else {
      if (brush_size == 1) {
        gfx2d_pixel(cx, cy, current_color);
      } else {
        gfx2d_circle_fill(cx, cy, brush_size, current_color);
      }
    }
  }

  if (current_tool == TOOL_FILL) {
    if (dragging == 0) {
      gfx2d_flood_fill(cx, cy, current_color);
    }
  }

  gfx2d_surface_unset_active();
  canvas_dirty = 1;
}

void draw_preview(int mx, int my) {
  if (is_dragging == 0)
    return;

  int x1 = screen_to_canvas_x(drag_start_x);
  int y1 = screen_to_canvas_y(drag_start_y);
  int x2 = screen_to_canvas_x(mx);
  int y2 = screen_to_canvas_y(my);

  if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0)
    return;

  /* Clip preview to canvas */
  if (x1 < 0)
    x1 = 0;
  if (x2 < 0)
    x2 = 0;
  if (y1 < 0)
    y1 = 0;
  if (y2 < 0)
    y2 = 0;
  if (x1 >= CANVAS_W)
    x1 = CANVAS_W - 1;
  if (x2 >= CANVAS_W)
    x2 = CANVAS_W - 1;
  if (y1 >= CANVAS_H)
    y1 = CANVAS_H - 1;
  if (y2 >= CANVAS_H)
    y2 = CANVAS_H - 1;

  int sx1 = canvas_to_screen_x(x1);
  int sy1 = canvas_to_screen_y(y1);
  int sx2 = canvas_to_screen_x(x2);
  int sy2 = canvas_to_screen_y(y2);

  /* Draw primitives directly to screen (over canvas blit) */
  if (current_tool == TOOL_LINE) {
    gfx2d_line(sx1, sy1, sx2, sy2, current_color);
  }
  if (current_tool == TOOL_RECT) {
    gfx2d_rect(sx1, sy1, sx2 - sx1, sy2 - sy1, current_color);
  }
  if (current_tool == TOOL_CIRCLE) {
    int r = (sx2 - sx1);
    if (r < 0)
      r = -r;
    gfx2d_circle(sx1, sy1, r, current_color);
  }
  if (current_tool == TOOL_SELECT) {
    gfx2d_rect(sx1, sy1, sx2 - sx1, sy2 - sy1, 0x0066AAFF);
  }
}

void commit_shape(int mx, int my) {
  int x1 = screen_to_canvas_x(drag_start_x);
  int y1 = screen_to_canvas_y(drag_start_y);
  int x2 = screen_to_canvas_x(mx);
  int y2 = screen_to_canvas_y(my);

  if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0)
    return;

  if (x1 < 0)
    x1 = 0;
  if (x2 < 0)
    x2 = 0;
  if (y1 < 0)
    y1 = 0;
  if (y2 < 0)
    y2 = 0;
  if (x1 >= CANVAS_W)
    x1 = CANVAS_W - 1;
  if (x2 >= CANVAS_W)
    x2 = CANVAS_W - 1;
  if (y1 >= CANVAS_H)
    y1 = CANVAS_H - 1;
  if (y2 >= CANVAS_H)
    y2 = CANVAS_H - 1;

  if (current_tool == TOOL_SELECT) {
    sel_x1 = x1;
    sel_y1 = y1;
    sel_x2 = x2;
    sel_y2 = y2;
    sel_active = 1;
    return;
  }

  gfx2d_surface_set_active(canvas_surf);

  if (current_tool == TOOL_LINE) {
    gfx2d_line(x1, y1, x2, y2, current_color);
  }
  if (current_tool == TOOL_RECT) {
    int rx = x1;
    int ry = y1;
    int rw = x2 - x1;
    int rh = y2 - y1;
    if (rw < 0) {
      rx = x2;
      rw = -rw;
    }
    if (rh < 0) {
      ry = y2;
      rh = -rh;
    }
    gfx2d_rect(rx, ry, rw, rh, current_color);
  }
  if (current_tool == TOOL_CIRCLE) {
    int r = (x2 - x1);
    if (r < 0)
      r = -r;
    gfx2d_circle(x1, y1, r, current_color);
  }

  gfx2d_surface_unset_active();
  canvas_dirty = 1;
}

/* ── Main Loop ────────────────────────────────────────────────────── */

int main() {
  reset_runtime_state();
  init_palette();

  /* Enter graphics mode */
  gfx2d_init();
  gfx2d_fullscreen_enter();

  /* Create canvas surface */
  canvas_surf = gfx2d_surface_alloc(CANVAS_W, CANVAS_H);
  canvas_snapshot = (int*)kmalloc(CANVAS_W * CANVAS_H * 4);

  if (canvas_surf < 0 || canvas_snapshot == 0) {
    if (canvas_snapshot) {
      kfree(canvas_snapshot);
      canvas_snapshot = 0;
    }
    if (canvas_surf >= 0) {
      gfx2d_surface_free(canvas_surf);
      canvas_surf = -1;
    }
    gfx2d_fullscreen_exit();
    return 1;
  }

  /* Initialize file tracking */
  current_file_path[0] = '\0';
  has_current_file = 0;

  /* Clear canvas to white */
  gfx2d_surface_set_active(canvas_surf);
  gfx2d_clear(0xFFFFFF);
  gfx2d_surface_unset_active();
  canvas_dirty = 1;

  int quit = 0;
  int prev_buttons = 0;

  while (quit == 0) {
    /* Input */
    int mx = mouse_x();
    int my = mouse_y();
    int b = mouse_buttons();
    int scroll_dz = mouse_scroll();
    int shift_held = key_shift_held();
    int click = (b & 1);
    int right_click = (b & 2);
    int left_click = (b & 1) && !(prev_buttons & 1);

    /* Mouse wheel: Shift+wheel zooms, wheel alone pans vertically */
    if (scroll_dz != 0) {
      if (shift_held) {
        if (scroll_dz < 0) {
          if (zoom_level < 4)
            zoom_level++;
        } else {
          if (zoom_level > 1)
            zoom_level--;
        }
        clamp_view_origin();
      } else {
        if (zoom_level > 1) {
          view_y = view_y + scroll_dz * 12;
          clamp_view_origin();
        }
      }
    }

    /* Right-drag panning when zoomed in */
    if (right_click) {
      if (pan_is_down == 0) {
        if (zoom_level > 1 && mx >= CANVAS_X && mx < CANVAS_X + CANVAS_W &&
            my >= CANVAS_Y && my < CANVAS_Y + CANVAS_H) {
          pan_is_down = 1;
          pan_start_mouse_x = mx;
          pan_start_mouse_y = my;
          pan_start_view_x = view_x;
          pan_start_view_y = view_y;
          is_dragging = 0;
        }
      } else {
        int dx = mx - pan_start_mouse_x;
        int dy = my - pan_start_mouse_y;
        view_x = pan_start_view_x - (dx / zoom_level);
        view_y = pan_start_view_y - (dy / zoom_level);
        clamp_view_origin();
      }
    } else {
      pan_is_down = 0;
    }

    /* Handle Click/Drag */
    if (click && pan_is_down == 0) {
      if (mouse_is_down == 0) {
        /* Just pressed */
        mouse_is_down = 1;
        drag_start_x = mx;
        drag_start_y = my;

        /* UI Interaction — skip if click is in the title bar area */
        if (my < TOOLBAR_H) {
          /* Handled by gfx2d_app_toolbar */
        } else if (mx < CANVAS_X) { /* Side Toolbar */
          if (my >= TOOLBAR_H + TOOL_GRID_Y &&
              my < TOOLBAR_H + TOOL_GRID_Y + 3 * (TOOL_BTN_H + TOOL_GAP_Y) - TOOL_GAP_Y) {
            int rel_y = my - (TOOLBAR_H + TOOL_GRID_Y);
            int row = rel_y / (TOOL_BTN_H + TOOL_GAP_Y);
            int col = 0;
            if (mx >= 4 + TOOL_BTN_W + TOOL_GAP_X)
              col = 1;
            int t = row * 2 + col;
            if (t >= 0 && t <= TOOL_SELECT)
              current_tool = t;
          }
          if (my >= TOOLBAR_H + BRUSH_PLUS_Y &&
              my < TOOLBAR_H + BRUSH_PLUS_Y + 20) {
            /* Brush Size + */
            if (brush_size < 10)
              brush_size++;
          }
          if (my >= TOOLBAR_H + BRUSH_MINUS_Y &&
              my < TOOLBAR_H + BRUSH_MINUS_Y + 20) {
            /* Brush Size - */
            if (brush_size > 1)
              brush_size--;
          }
          if (my >= TOOLBAR_H + ZOOM_PLUS_Y && my < TOOLBAR_H + ZOOM_PLUS_Y + 20) {
            if (zoom_level < 4)
              zoom_level++;
            clamp_view_origin();
          }
          if (my >= TOOLBAR_H + ZOOM_MINUS_Y &&
              my < TOOLBAR_H + ZOOM_MINUS_Y + 20) {
            if (zoom_level > 1)
              zoom_level--;
            clamp_view_origin();
          }
          if (my >= TOOLBAR_H + CROP_Y && my < TOOLBAR_H + CROP_Y + 20) {
            crop_to_selection();
          }
          if (my >= TOOLBAR_H + RESIZE_UP_Y && my < TOOLBAR_H + RESIZE_UP_Y + 20) {
            resize_canvas_200();
          }
          if (my >= TOOLBAR_H + RESIZE_DOWN_Y && my < TOOLBAR_H + RESIZE_DOWN_Y + 20) {
            resize_canvas_50();
          }
          if (my >= TOOLBAR_H + SAVE_Y && my < TOOLBAR_H + SAVE_Y + 20) {
            save_drawing();
          }
          if (my >= TOOLBAR_H + SAVE_AS_Y && my < TOOLBAR_H + SAVE_AS_Y + 20) {
            save_drawing_as();
          }
          if (my >= TOOLBAR_H + LOAD_Y && my < TOOLBAR_H + LOAD_Y + 20) {
            load_drawing();
          }
        } else {
          if (my >= CANVAS_Y + CANVAS_H) { /* Palette */
            int col_idx = (mx - (CANVAS_X + 4)) / 32;
            if (col_idx >= 0 && col_idx < 16) {
              current_color = palette[col_idx];
            }
          } else {
            if (mx >= CANVAS_X && mx < CANVAS_X + CANVAS_W && my >= CANVAS_Y &&
                my < CANVAS_Y + CANVAS_H) { /* Canvas */
              int ccx = screen_to_canvas_x(mx);
              int ccy = screen_to_canvas_y(my);
              if (current_tool == TOOL_SELECT && point_in_selection(ccx, ccy)) {
                selection_move_start(ccx, ccy);
                is_dragging = 0;
              } else {
                is_dragging = 1;
              }

              if (is_dragging &&
                  (current_tool == TOOL_PENCIL || current_tool == TOOL_FILL)) {
                use_tool(mx, my, 0);
              }
            }
          }
        }
      } else {
        /* Holding/Dragging */
        if (sel_move_active) {
          int ccx = screen_to_canvas_x(mx);
          int ccy = screen_to_canvas_y(my);
          if (ccx >= 0 && ccy >= 0)
            selection_move_update(ccx, ccy);
        } else if (is_dragging) {
          if (current_tool == TOOL_PENCIL) {
            use_tool(mx, my, 1);
          }
        }
      }
    } else {
      if (mouse_is_down) {
        /* Released */
        if (sel_move_active) {
          selection_move_commit();
        } else if (is_dragging) {
          if (current_tool != TOOL_PENCIL) {
            if (current_tool != TOOL_FILL) {
              commit_shape(mx, my);
            }
          }
          is_dragging = 0;
        }
        mouse_is_down = 0;
      }
    }

    /* Update Prev Mouse */
    mouse_prev_x = mx;
    mouse_prev_y = my;
    prev_buttons = b;

    /* Render */
    gfx2d_clear(0xC0C0C0); /* Desktop BG */

    /* Draw App Toolbar (title bar with close/minimize) */
    int tb_action = gfx2d_app_toolbar("CupidPaint", mx, my, left_click);
    if (tb_action == 1 || gfx2d_should_quit()) {
      quit = 1; /* Close */
    }
    if (tb_action == 2) {
      gfx2d_minimize("CupidPaint"); /* Minimize to taskbar */
    }

    /* Draw Canvas */
    draw_canvas_view();

    /* Draw persistent selection */
    draw_selection_overlay();

    /* Draw Preview (if any) */
    draw_preview(mx, my);

    /* Draw UI */
    draw_toolbar();
    draw_palette();

    /* Draw Cursor */
    gfx2d_draw_cursor();

    /* Flip */
    gfx2d_flip();
  }

  gfx2d_fullscreen_exit();
  if (canvas_surf >= 0)
    gfx2d_surface_free(canvas_surf);
  if (canvas_snapshot)
    kfree(canvas_snapshot);
  if (sel_buffer)
    kfree(sel_buffer);
  return 0;
}
