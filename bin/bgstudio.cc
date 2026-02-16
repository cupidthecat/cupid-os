//icon: "BG Studio"
//icon_desc: "Cupid Animated Background Studio"
//icon_x: 80
//icon_y: 250
//icon_type: app
//icon_color: 0x44AAFF

/* BG Studio — Desktop background editor */

int clamp255(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return v;
}

int rgb_to_hsv_pack(int rgb) {
  int r = (rgb >> 16) & 255;
  int g = (rgb >> 8) & 255;
  int b = rgb & 255;
  int max = r;
  int min = r;
  int d;
  int h = 0;
  int s;
  int v;

  if (g > max) max = g;
  if (b > max) max = b;
  if (g < min) min = g;
  if (b < min) min = b;

  d = max - min;
  v = max;

  if (max == 0) {
    s = 0;
  } else {
    s = (d * 255) / max;
  }

  if (d == 0) {
    h = 0;
  } else if (max == r) {
    h = (60 * (g - b)) / d;
  } else if (max == g) {
    h = 120 + (60 * (b - r)) / d;
  } else {
    h = 240 + (60 * (r - g)) / d;
  }

  while (h < 0) h = h + 360;
  while (h >= 360) h = h - 360;

  return (h << 16) | (s << 8) | v;
}

int point_in(int px, int py, int x, int y, int w, int h) {
  if (px < x) return 0;
  if (py < y) return 0;
  if (px >= x + w) return 0;
  if (py >= y + h) return 0;
  return 1;
}

int ui_button(int x, int y, int w, int h, char *label,
              int mx, int my, int click, int active) {
  int hover = point_in(mx, my, x, y, w, h);
  int bg = 0xD8E8FF;
  if (active) bg = 0x9FC6FF;
  if (hover && !active) bg = 0xC8DCF8;

  gfx2d_rect_fill(x, y, w, h, bg);
  gfx2d_rect(x, y, w, h, 0x304058);
  gfx2d_text(x + 6, y + 4, label, 0x102030, 1);

  if (hover && click) return 1;
  return 0;
}

void draw_swatch(int x, int y, int c, int selected) {
  gfx2d_rect_fill(x, y, 18, 18, c);
  if (selected) {
    gfx2d_rect(x - 1, y - 1, 20, 20, 0xFFFFFF);
  }
  gfx2d_rect(x, y, 18, 18, 0x202020);
}

int color_lerp(int c1, int c2, int t, int max) {
  int r1 = (c1 >> 16) & 255;
  int g1 = (c1 >> 8) & 255;
  int b1 = c1 & 255;
  int r2 = (c2 >> 16) & 255;
  int g2 = (c2 >> 8) & 255;
  int b2 = c2 & 255;

  int r = (r1 * (max - t) + r2 * t) / max;
  int g = (g1 * (max - t) + g2 * t) / max;
  int b = (b1 * (max - t) + b2 * t) / max;
  return (r << 16) | (g << 8) | b;
}

void show_tile_bmp_error(int rc) {
  if (rc == -1) {
    message_dialog("Tile BMP invalid path");
  } else if (rc == -2) {
    message_dialog("Tile BMP unsupported format");
  } else if (rc == -3) {
    message_dialog("Tile BMP file I/O error");
  } else if (rc == -4) {
    message_dialog("Tile BMP out of memory");
  } else {
    message_dialog("Tile BMP load failed");
  }
}

int animated_preview_top(int tick) {
  int t = tick & 511;
  if (t < 170) return color_lerp(0xDCC3FF, 0xEEDDFF, t, 170);
  if (t < 340) return color_lerp(0xEEDDFF, 0xFFF0E0, t - 170, 170);
  return color_lerp(0xFFF0E0, 0xDCC3FF, t - 340, 172);
}

void draw_preview_kitty(int tick, int sw, int sh) {
  int y = sh - 28;
  int x = (tick % (sw + 40)) - 20;
  int leg = (tick >> 3) & 1;

  gfx2d_rect_fill(x + 4, y + 8, 18, 10, 0xD8C2A8);
  gfx2d_rect_fill(x + 18, y + 5, 10, 10, 0xE8D0B7);
  gfx2d_rect_fill(x + 19, y + 2, 3, 4, 0xB89270);
  gfx2d_rect_fill(x + 24, y + 2, 3, 4, 0xB89270);
  if (leg) {
    gfx2d_rect_fill(x + 4, y + 18, 4, 4, 0x8E6D4F);
    gfx2d_rect_fill(x + 14, y + 18, 4, 4, 0x8E6D4F);
  } else {
    gfx2d_rect_fill(x + 8, y + 18, 4, 4, 0x8E6D4F);
    gfx2d_rect_fill(x + 18, y + 18, 4, 4, 0x8E6D4F);
  }
}

/* Optimized tiled preview — uses hlines instead of per-pixel gfx2d_pixel */
void draw_tiled_preview(int pattern, int fg, int bg, int x0, int y0, int w, int h) {
  int y;
  if (pattern == 1) {
    /* Sparse dots: only 2 pixels on per 8x8 tile — draw bg fill then dots */
    gfx2d_rect_fill(x0, y0, w, h, bg);
    for (y = 0; y < h; y++) {
      int ly = y & 7;
      if (ly == 1 || ly == 5) {
        int x;
        for (x = 0; x < w; x++) {
          int lx = x & 7;
          if (lx == 1 || lx == 5) {
            gfx2d_pixel(x0 + x, y0 + y, fg);
          }
        }
      }
    }
    return;
  }
  if (pattern == 2) {
    /* 2x2 checker using dither_rect (GFX2D_DITHER_CHECKER=0) */
    gfx2d_dither_rect(x0, y0, w, h, bg, fg, 0);
    return;
  }
  /* pattern == 0: 2x2 block checker — draw as alternating 2-pixel hlines */
  for (y = 0; y < h; y++) {
    int ly = y & 7;
    int row_phase = (ly >> 1) & 1;
    int x;
    for (x = 0; x < w; x += 4) {
      /* Each 4-pixel run: 2 of first color, 2 of second */
      int run1 = (x + 2 <= w) ? 2 : (w - x);
      int run2 = (x + 4 <= w) ? 2 : ((w - x - run1 > 0) ? (w - x - run1) : 0);
      if (row_phase == 0) {
        gfx2d_hline(x0 + x,          y0 + y, run1, bg);
        if (run2 > 0) gfx2d_hline(x0 + x + 2, y0 + y, run2, fg);
      } else {
        gfx2d_hline(x0 + x,          y0 + y, run1, fg);
        if (run2 > 0) gfx2d_hline(x0 + x + 2, y0 + y, run2, bg);
      }
    }
  }
}

void draw_live_backdrop(int mode, int r, int g, int b, int tick,
                        int sw, int sh, int backdrop) {
  int top;
  int bottom;

  if (mode == 0) {
    int pulse = animated_preview_top(tick);
    top = color_lerp((r << 16) | (g << 8) | b, pulse, 4, 10);
    bottom = color_lerp(top, 0x3A3A52, 7, 10);
    gfx2d_gradient_v(0, 0, sw, sh, top, bottom);
    draw_preview_kitty(tick, sw, sh);
    return;
  }

  if (mode == 1) {
    top = (r << 16) | (g << 8) | b;
    bottom = color_lerp(top, 0x223044, 8, 10);
    gfx2d_gradient_v(0, 0, sw, sh, top, bottom);
    return;
  }

  if (mode == 3) {
    top = (r << 16) | (g << 8) | b;
    bottom = color_lerp(top, 0x101A2A, 7, 10);
    gfx2d_gradient_v(0, 0, sw, sh, top, bottom);
    return;
  }

  if (mode == 4) {
    int fg = color_lerp((r << 16) | (g << 8) | b, 0xFFFFFF, 2, 10);
    int bg = color_lerp((r << 16) | (g << 8) | b, 0x000000, 6, 10);
    draw_tiled_preview(0, fg, bg, 0, 0, sw, sh);
    return;
  }

  if (backdrop >= 0) {
    gfx2d_surface_blit(backdrop, 0, 0);
  }
}

int main() {
  gfx2d_fullscreen_enter();

  int done = 0;
  int input_grace_until = uptime_ms() + 3000;
  int prev_buttons = mouse_buttons();
  int sw = gfx2d_width();
  int sh = gfx2d_height();
  int anim_tick = 0;
  int anim_frame = 0;   /* throttle anim_tick to every 4 renders */
  int mode = desktop_bg_get_mode();
  int applied_flash = 0;
  int drag_picker = 0;
  int drag_hue = 0;
  int tile_pattern = desktop_bg_get_tiled_pattern();
  int tile_use_bmp = desktop_bg_get_tiled_use_bmp();
  char tile_bmp_path[256];
  tile_bmp_path[0] = 0;

  if (mode != 0 && mode != 1 && mode != 3 && mode != 4) mode = 1;
  if (tile_pattern < 0 || tile_pattern > 2) tile_pattern = 0;

  int solid = desktop_bg_get_solid_color();
  int hsv = rgb_to_hsv_pack(solid);
  int hue = (hsv >> 16) & 511;
  int sat = (hsv >> 8) & 255;
  int val = hsv & 255;

  int presets[10];
  presets[0] = 0xDCC3FF;
  presets[1] = 0xEEDDFF;
  presets[2] = 0xFFF0E0;
  presets[3] = 0xD8F0FF;
  presets[4] = 0xB0D8FF;
  presets[5] = 0x112233;
  presets[6] = 0x223344;
  presets[7] = 0x3A5068;
  presets[8] = 0x6F2DBD;
  presets[9] = 0x0B132B;

  /* Capture backdrop using fast memcpy instead of pixel-by-pixel */
  int backdrop = gfx2d_surface_alloc(sw, sh);
  if (backdrop >= 0) {
    gfx2d_capture_screen_to_surface(backdrop);
  }

  gfx2d_window_reset(90, 70, 460, 300);

  {
    int i = 0;
    while (i < 64) {
      poll_key();
      i++;
    }
  }

  /* Cache the SV color picker to avoid per-pixel HSV recalc every frame */
  int picker_w = 170;
  int picker_h = 70;   /* will be updated once we know the layout */
  int sv_cache = gfx2d_surface_alloc(picker_w, picker_h);
  int sv_cache_hue = -1;  /* invalid — force first draw */

  while (!done) {
    int mx = mouse_x();
    int my = mouse_y();
    int btns = mouse_buttons();
    int left_click = (btns & 1) && !(prev_buttons & 1);
    int left_down = (btns & 1);
    int current = gfx2d_color_hsv(hue, sat, val);
    int r = (current >> 16) & 255;
    int g = (current >> 8) & 255;
    int b = current & 255;

    gfx2d_cursor_hide();
    draw_live_backdrop(mode, r, g, b, anim_tick, sw, sh, backdrop);

    int win_action = gfx2d_window_frame("BG Studio", mx, my,
                                        left_click, left_down);

    if (uptime_ms() > input_grace_until) {
      if (win_action == 1) done = 1;
      if (win_action == 2) {
        gfx2d_minimize("BG Studio");
        /* Fast screen capture after restore */
        if (backdrop >= 0) {
          gfx2d_capture_screen_to_surface(backdrop);
        }
        prev_buttons = mouse_buttons();
        continue;
      }
    }

    int cx = gfx2d_window_content_x();
    int cy = gfx2d_window_content_y();
    int cw = gfx2d_window_content_w();
    int ch = gfx2d_window_content_h();

    gfx2d_rect_fill(cx, cy, cw, ch, 0xEEF3FB);
    gfx2d_rect(cx, cy, cw, ch, 0x9AAAC4);

    gfx2d_text(cx + 12, cy + 10, "Desktop Background Studio", 0x203048, 1);
    gfx2d_text(cx + 12, cy + 24, "Choose mode, color, and apply to desktop.",
               0x40546E, 1);

    {
      int px = cx + 12;
      int py = cy + 42;
      int pw = cw - 24;
      int ph = 74;
      int top;
      int bottom;

      if (mode == 0) {
        int pulse = animated_preview_top(anim_tick);
        top = color_lerp((r << 16) | (g << 8) | b, pulse, 4, 10);
      } else if (mode == 4) {
        top = (r << 16) | (g << 8) | b;
      } else if (mode == 3) {
        top = (r << 16) | (g << 8) | b;
      } else {
        top = (r << 16) | (g << 8) | b;
      }
      bottom = color_lerp(top, 0x223044, 5, 10);

      gfx2d_gradient_v(px, py, pw, ph, top, bottom);
      gfx2d_rect(px, py, pw, ph, 0x344760);
      gfx2d_text(px + 8, py + 8, "Preview", 0xFFFFFF, 1);
      if (mode == 0) {
        gfx2d_text(px + 8, py + 24, "Mode: Animated", 0xEAF3FF, 1);
        gfx2d_text(px + 8, py + 38, "Kitty", 0xEAF3FF, 1);
      } else if (mode == 4) {
        gfx2d_text(px + 8, py + 24, "Mode: Tiled", 0xEAF3FF, 1);
        if (tile_use_bmp) gfx2d_text(px + 8, py + 38, "BMP Tile", 0xEAF3FF, 1);
        else gfx2d_text(px + 8, py + 38, "8x8 Pattern", 0xEAF3FF, 1);
      } else if (mode == 3) {
        gfx2d_text(px + 8, py + 24, "Mode: Gradient", 0xEAF3FF, 1);
      } else {
        gfx2d_text(px + 8, py + 24, "Mode: Solid", 0xEAF3FF, 1);
      }
    }

    {
      int my0 = cy + 124;
      if (ui_button(cx + 12, my0, 78, 20, "Animated", mx, my, left_click,
                    mode == 0)) {
        mode = 0;
      }
      if (ui_button(cx + 94, my0, 56, 20, "Solid", mx, my, left_click,
                    mode == 1)) {
        mode = 1;
      }
      if (ui_button(cx + 154, my0, 78, 20, "Gradient", mx, my, left_click,
                    mode == 3)) {
        mode = 3;
      }
      if (ui_button(cx + 236, my0, 52, 20, "Tiled", mx, my, left_click,
                    mode == 4)) {
        mode = 4;
      }
      if (ui_button(cx + 292, my0, 56, 20, "Apply", mx, my, left_click, 0)) {
        if (mode == 0) {
          desktop_bg_set_mode_solid(current);
          desktop_bg_set_anim_theme(2);
        } else if (mode == 4) {
          if (tile_use_bmp && tile_bmp_path[0]) {
            int rc = desktop_bg_set_mode_tiled_bmp(tile_bmp_path);
            if (rc != 0) {
              show_tile_bmp_error(rc);
            }
          } else {
            int fg = color_lerp(current, 0xFFFFFF, 2, 10);
            int bg = color_lerp(current, 0x000000, 6, 10);
            desktop_bg_set_mode_tiled_pattern(tile_pattern, fg, bg);
          }
        } else if (mode == 3) {
          desktop_bg_set_mode_gradient(current, color_lerp(current, 0x101A2A, 7, 10));
        } else {
          desktop_bg_set_mode_solid(current);
        }
        applied_flash = 90;
      }
    }

    if (mode == 4) {
      int ty = cy + 146;
      if (ui_button(cx + 12, ty, 36, 18, "P1", mx, my, left_click,
                    !tile_use_bmp && tile_pattern == 0)) {
        tile_pattern = 0;
        tile_use_bmp = 0;
      }
      if (ui_button(cx + 52, ty, 36, 18, "P2", mx, my, left_click,
                    !tile_use_bmp && tile_pattern == 1)) {
        tile_pattern = 1;
        tile_use_bmp = 0;
      }
      if (ui_button(cx + 92, ty, 36, 18, "P3", mx, my, left_click,
                    !tile_use_bmp && tile_pattern == 2)) {
        tile_pattern = 2;
        tile_use_bmp = 0;
      }
      if (ui_button(cx + 132, ty, 86, 18, "Tile BMP", mx, my, left_click,
                    tile_use_bmp)) {
        char path[256];
        if (file_dialog_open("/home", path, ".bmp") == 1) {
          strcpy(tile_bmp_path, path);
          tile_use_bmp = 1;
        }
      }
      if (tile_use_bmp && tile_bmp_path[0]) {
        gfx2d_text(cx + 222, ty + 4, "BMP selected", 0x203048, 1);
      }
    }

    {
      int px = cx + 12;
      int py = (mode == 4) ? (cy + 184) : (cy + 172);
      int pw = 170;
      int ph = (mode == 4) ? 56 : 70;
      int hx = px + pw + 8;
      int hw = 14;

      gfx2d_text(px, py - 12, "Color Picker", 0x203048, 1);

      /* Redraw SV cache only when hue changed (or first frame) */
      if (hue != sv_cache_hue) {
        sv_cache_hue = hue;
        if (sv_cache >= 0) {
          /* Reallocate if size changed */
          gfx2d_surface_set_active(sv_cache);
          gfx2d_color_picker_draw_sv(0, 0, pw, ph, hue, sat, val);
          gfx2d_surface_unset_active();
        } else {
          gfx2d_color_picker_draw_sv(px, py, pw, ph, hue, sat, val);
        }
      }

      /* Blit cached SV surface or redraw directly */
      if (sv_cache >= 0) {
        gfx2d_surface_blit(sv_cache, px, py);
        /* Draw the sat/val crosshair on top */
        {
          int crosshair_x = px + ((sat * (pw - 1)) / 255);
          int crosshair_y = py + (((255 - val) * (ph - 1)) / 255);
          gfx2d_circle(crosshair_x, crosshair_y, 4, 0xFFFFFF);
          gfx2d_circle(crosshair_x, crosshair_y, 5, 0x000000);
        }
        gfx2d_rect(px, py, pw, ph, 0x203040);
      } else {
        gfx2d_color_picker_draw_sv(px, py, pw, ph, hue, sat, val);
      }

      gfx2d_color_picker_draw_hue(hx, py, hw, ph, hue);

      if (!left_down) {
        drag_picker = 0;
        drag_hue = 0;
      }

      if (left_click && point_in(mx, my, px, py, pw, ph)) drag_picker = 1;
      if (left_click && point_in(mx, my, hx, py, hw, ph)) drag_hue = 1;

      if (left_down && drag_picker) {
        sat = gfx2d_color_picker_pick_sat(px, py, pw, ph, mx, my);
        val = gfx2d_color_picker_pick_val(px, py, pw, ph, mx, my);
        /* Force SV cache update when dragging (sat/val changed) */
        sv_cache_hue = -1;
      }
      if (left_down && drag_hue) {
        hue = gfx2d_color_picker_pick_hue(hx, py, hw, ph, mx, my);
      }

      gfx2d_rect_fill(hx + hw + 8, py, 20, 20, current);
      gfx2d_rect(hx + hw + 8, py, 20, 20, 0x304058);
      gfx2d_text(hx + hw + 8, py + 24, "RGB", 0x304058, 1);
    }

    {
      int sy = cy + ch - 30;
      int sx = cx + 12;
      int i = 0;
      while (i < 10) {
        int x = sx + i * 21;
        int c = presets[i];
        int selected = (current == c);
        draw_swatch(x, sy, c, selected);
        if (left_click && point_in(mx, my, x, sy, 18, 18)) {
          int phsv = rgb_to_hsv_pack(c);
          hue = (phsv >> 16) & 511;
          sat = (phsv >> 8) & 255;
          val = phsv & 255;
        }
        i = i + 1;
      }
    }

    if (applied_flash > 0) {
      gfx2d_text(cx + cw - 110, cy + ch - 28, "Applied", 0x228833, 1);
      applied_flash = applied_flash - 1;
    }

    {
      char key = poll_key();
      if (uptime_ms() > input_grace_until) {
        if (key == 27) done = 1;
        if (key == 'a' || key == 'A') mode = 0;
        if (key == 's' || key == 'S') mode = 1;
        if (key == 'g' || key == 'G') mode = 3;
        if (key == 't' || key == 'T') mode = 4;
        if (key == 'p' || key == 'P') {
          if (mode == 0) {
            desktop_bg_set_mode_solid(current);
            desktop_bg_set_anim_theme(2);
          }
          else if (mode == 4) {
            if (tile_use_bmp && tile_bmp_path[0]) {
              int rc = desktop_bg_set_mode_tiled_bmp(tile_bmp_path);
              if (rc != 0) show_tile_bmp_error(rc);
            } else {
              int fg = color_lerp(current, 0xFFFFFF, 2, 10);
              int bg = color_lerp(current, 0x000000, 6, 10);
              desktop_bg_set_mode_tiled_pattern(tile_pattern, fg, bg);
            }
          }
          else if (mode == 3) desktop_bg_set_mode_gradient(current, color_lerp(current, 0x101A2A, 7, 10));
          else desktop_bg_set_mode_solid(current);
          applied_flash = 90;
        }
      }
    }

    prev_buttons = btns;
    gfx2d_draw_cursor();
    gfx2d_flip();

    /* Throttle animation tick to every 4 frames */
    anim_frame = anim_frame + 1;
    if (anim_frame >= 4) {
      anim_frame = 0;
      anim_tick = anim_tick + 1;
    }

    yield();
  }

  if (sv_cache >= 0) {
    gfx2d_surface_free(sv_cache);
  }
  if (backdrop >= 0) {
    gfx2d_surface_free(backdrop);
  }
  gfx2d_fullscreen_exit();
  return 0;
}
