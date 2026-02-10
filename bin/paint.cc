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

int TOOLBAR_H = 20;

int CANVAS_X = 40;
int CANVAS_Y = 20;
int CANVAS_W = 600;
int CANVAS_H = 428;

/* Palette colors (16 standard VGA/Win95 colors) */
int palette[16];

/* ── Global State ─────────────────────────────────────────────────── */

int canvas_surf = -1;
int current_tool = 0;         /* TOOL_PENCIL */
int current_color = 0x000000; /* Black */
int brush_size = 1;

int mouse_prev_x = 0;
int mouse_prev_y = 0;
int mouse_is_down = 0;

int drag_start_x = 0;
int drag_start_y = 0;
int is_dragging = 0;

/* Buffer for file I/O (One row at a time) */
int row_buffer[600];

/* Current file tracking for Save/Save As pattern */
char current_file_path[128];
int has_current_file = 0;

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

  return 0;
}

void save_drawing() {
  char path[128];

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
  gfx2d_panel(0, TOOLBAR_H, 40, 480 - TOOLBAR_H);

  /* Tools */
  /* Replaced array of strings with explicit calls because array init might be
   * limited */
  /* "P", "L", "R", "C", "F" */

  int i = 0;
  while (i < 5) {
    int y = TOOLBAR_H + 10 + i * 40;
    int selected = 0;
    if (current_tool == i)
      selected = 1;

    gfx2d_bevel(4, y, 32, 32, !selected); /* !selected = raised if not active */

    /* Manual switch for labels */
    if (i == 0)
      gfx2d_text(15, y + 12, "P", 0x000000, 1);
    if (i == 1)
      gfx2d_text(15, y + 12, "L", 0x000000, 1);
    if (i == 2)
      gfx2d_text(15, y + 12, "R", 0x000000, 1);
    if (i == 3)
      gfx2d_text(15, y + 12, "C", 0x000000, 1);
    if (i == 4)
      gfx2d_text(15, y + 12, "F", 0x000000, 1);

    i++;
  }

  /* Brush Size Controls */
  /* [+] */
  gfx2d_bevel(4, TOOLBAR_H + 250, 32, 20, 1);
  gfx2d_text(15, TOOLBAR_H + 256, "+", 0x000000, 1);

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

  gfx2d_rect_fill(10, TOOLBAR_H + 275, 20, 10, 0xC0C0C0);
  gfx2d_text(15, TOOLBAR_H + 275, size_str, 0x000000, 0);

  /* [-] */
  gfx2d_bevel(4, TOOLBAR_H + 290, 32, 20, 1);
  gfx2d_text(15, TOOLBAR_H + 296, "-", 0x000000, 1);

  /* Save/Load Buttons */
  gfx2d_bevel(4, TOOLBAR_H + 400, 32, 20, 1);
  gfx2d_text(8, TOOLBAR_H + 406, "SV", 0x000000, 0);

  /* Save As button */
  gfx2d_bevel(4, TOOLBAR_H + 425, 32, 20, 1);
  gfx2d_text(8, TOOLBAR_H + 431, "SA", 0x000000, 0);

  /* Load button */
  gfx2d_bevel(4, TOOLBAR_H + 450, 32, 20, 1);
  gfx2d_text(8, TOOLBAR_H + 456, "LD", 0x000000, 0);
}

void draw_palette() {
  /* Palette background */
  int palette_y = CANVAS_Y + CANVAS_H;
  gfx2d_panel(40, palette_y, 600, 32);

  int i = 0;
  while (i < 16) {
    int x = 40 + 4 + i * 32;
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

/* ── Tool Logic ───────────────────────────────────────────────────── */

void use_tool(int x, int y, int dragging) {
  /* Adjust to canvas coordinates */
  int cx = x - CANVAS_X;
  int cy = y - CANVAS_Y;

  gfx2d_surface_set_active(canvas_surf);

  if (current_tool == TOOL_PENCIL) {
    if (mouse_prev_x > CANVAS_X) {
      if (brush_size == 1) {
        gfx2d_line(mouse_prev_x - CANVAS_X, mouse_prev_y - CANVAS_Y, cx, cy,
                   current_color);
      } else {
        /* Simulated thick line */
        int x1 = mouse_prev_x - CANVAS_X;
        int y1 = mouse_prev_y - CANVAS_Y;
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
}

void draw_preview(int mx, int my) {
  if (is_dragging == 0)
    return;

  int x1 = drag_start_x;
  int y1 = drag_start_y;
  int x2 = mx;
  int y2 = my;

  /* Clip preview to canvas */
  if (x1 < CANVAS_X)
    x1 = CANVAS_X;
  if (x2 < CANVAS_X)
    x2 = CANVAS_X;
  if (y1 > CANVAS_H)
    y1 = CANVAS_H;
  if (y2 > CANVAS_H)
    y2 = CANVAS_H;

  /* Draw primitives directly to screen (over canvas blit) */
  if (current_tool == TOOL_LINE) {
    gfx2d_line(x1, y1, x2, y2, current_color);
  }
  if (current_tool == TOOL_RECT) {
    gfx2d_rect(x1, y1, x2 - x1, y2 - y1, current_color);
  }
  if (current_tool == TOOL_CIRCLE) {
    int r = (x2 - x1);
    if (r < 0)
      r = -r;
    gfx2d_circle(x1, y1, r, current_color);
  }
}

void commit_shape(int mx, int my) {
  int x1 = drag_start_x - CANVAS_X;
  int y1 = drag_start_y - CANVAS_Y;
  int x2 = mx - CANVAS_X;
  int y2 = my - CANVAS_Y;

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
}

/* ── Main Loop ────────────────────────────────────────────────────── */

int main() {
  init_palette();

  /* Enter graphics mode */
  gfx2d_init();
  gfx2d_fullscreen_enter();

  /* Create canvas surface */
  canvas_surf = gfx2d_surface_alloc(CANVAS_W, CANVAS_H);

  /* Initialize file tracking */
  current_file_path[0] = '\0';
  has_current_file = 0;

  /* Clear canvas to white */
  gfx2d_surface_set_active(canvas_surf);
  gfx2d_clear(0xFFFFFF);
  gfx2d_surface_unset_active();

  int quit = 0;
  int prev_buttons = 0;

  while (quit == 0) {
    /* Input */
    int mx = mouse_x();
    int my = mouse_y();
    int b = mouse_buttons();
    int click = (b & 1);
    int left_click = (b & 1) && !(prev_buttons & 1);

    /* Handle Click/Drag */
    if (click) {
      if (mouse_is_down == 0) {
        /* Just pressed */
        mouse_is_down = 1;
        drag_start_x = mx;
        drag_start_y = my;

        /* UI Interaction — skip if click is in the title bar area */
        if (my < TOOLBAR_H) {
          /* Handled by gfx2d_app_toolbar */
        } else if (mx < 40) { /* Side Toolbar */
          if (my >= TOOLBAR_H + 10 && my < TOOLBAR_H + 210) {
            current_tool = (my - TOOLBAR_H - 10) / 40;
            if (current_tool > 4)
              current_tool = 4;
          }
          if (my >= TOOLBAR_H + 250 && my < TOOLBAR_H + 270) {
            /* Brush Size + */
            if (brush_size < 10)
              brush_size++;
          }
          if (my >= TOOLBAR_H + 290 && my < TOOLBAR_H + 310) {
            /* Brush Size - */
            if (brush_size > 1)
              brush_size--;
          }
          if (my >= TOOLBAR_H + 400 && my < TOOLBAR_H + 420) {
            save_drawing();
          }
          if (my >= TOOLBAR_H + 425 && my < TOOLBAR_H + 445) {
            save_drawing_as();
          }
          if (my >= TOOLBAR_H + 450 && my < TOOLBAR_H + 470) {
            load_drawing();
          }
        } else {
          if (my > TOOLBAR_H + 428) { /* Palette */
            int col_idx = (mx - 44) / 32;
            if (col_idx >= 0 && col_idx < 16) {
              current_color = palette[col_idx];
            }
          } else {
            if (mx >= CANVAS_X && my >= CANVAS_Y && my <= CANVAS_Y + CANVAS_H) { /* Canvas */
              is_dragging = 1;
              if (current_tool == TOOL_PENCIL || current_tool == TOOL_FILL) {
                use_tool(mx, my, 0);
              }
            }
          }
        }
      } else {
        /* Holding/Dragging */
        if (is_dragging) {
          if (current_tool == TOOL_PENCIL) {
            use_tool(mx, my, 1);
          }
        }
      }
    } else {
      if (mouse_is_down) {
        /* Released */
        if (is_dragging) {
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
    gfx2d_surface_blit(canvas_surf, CANVAS_X, CANVAS_Y);

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
  return 0;
}
