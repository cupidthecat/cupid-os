/* gfxgui_test.cc - Comprehensive gfx2d/gui smoke test for CupidC bindings */

int make_test_bmp(const char *path) {
  int w = 64;
  int h = 64;
  int i = 0;
  int *buf = (int *)kmalloc(w * h * 4);
  if (buf == 0) {
    return -1;
  }

  while (i < w * h) {
    int x = i % w;
    int y = i / w;
    int r = (x * 4) & 255;
    int g = (y * 4) & 255;
    int b = ((x + y) * 2) & 255;
    buf[i] = (r << 16) | (g << 8) | b;
    i++;
  }

  i = bmp_encode(path, buf, w, h);
  kfree(buf);
  return i;
}

void draw_base_scene(int tick) {
  int c1 = ((tick * 2) & 255) << 16;
  int c2 = ((tick * 3) & 255) << 8;
  gfx2d_gradient_v(0, 0, 640, 480, c1, c2);

  gfx2d_rect_fill(10, 10, 620, 460, 0x101010);
  gfx2d_rect(10, 10, 620, 460, 0xFFFFFF);

  gfx2d_text_shadow(20, 20, "gfxgui_test: assets + transform + effects + themes",
                    0xFFFFFF, 0x000000, 1);
}

void test_effects_cycle(int tick) {
  int phase = (tick / 24) % 10;
  int k3[9];
  int k5[25];
  int i = 0;

  if (phase == 0)
    gfx2d_blur_box(20, 60, 280, 180, 1);
  if (phase == 1)
    gfx2d_blur_gaussian(20, 60, 280, 180, 1);
  if (phase == 2)
    gfx2d_blur_motion(20, 60, 280, 180, tick % 360, 4);
  if (phase == 3)
    gfx2d_brightness(20, 60, 280, 180, 16);
  if (phase == 4)
    gfx2d_contrast(20, 60, 280, 180, 18);
  if (phase == 5)
    gfx2d_saturation(20, 60, 280, 180, 330);
  if (phase == 6)
    gfx2d_hue_shift(20, 60, 280, 180, tick % 360);
  if (phase == 7)
    gfx2d_edges(20, 60, 280, 180, 0x00FF00);
  if (phase == 8)
    gfx2d_emboss(20, 60, 280, 180, 45);
  if (phase == 9)
    gfx2d_posterize(20, 60, 280, 180, 6);

  i = 0;
  while (i < 9) {
    k3[i] = 0;
    i++;
  }
  k3[1] = -1;
  k3[3] = -1;
  k3[4] = 5;
  k3[5] = -1;
  k3[7] = -1;
  gfx2d_convolve_3x3(320, 60, 280, 180, k3, 1);

  i = 0;
  while (i < 25) {
    k5[i] = 1;
    i++;
  }
  gfx2d_convolve_5x5(320, 60, 280, 180, k5, 25);

  gfx2d_tint_ex(20, 250, 280, 180, 0x44AAFF, 90, 1);
  gfx2d_chromatic_aberration(320, 250, 280, 180, 1);
  gfx2d_scanlines_ex(320, 250, 280, 180, 48, (tick / 80) % 4);
  gfx2d_noise(20, 250, 280, 180, 10, (tick * 33) + 7);
}

int main() {
  int tick = 0;
  int img = -1;
  int surf = -1;
  int fnt = -1;
  int fnt_fd = -1;
  int px = 0;
  int m[6];
  int ox = 0;
  int oy = 0;

  println("[gfxgui_test] init");

  if (!is_gui_mode()) {
    println("[gfxgui_test] requires GUI mode");
    println("[gfxgui_test] open Desktop terminal and run again");
    return 1;
  }

  /* Module init calls (bindings smoke test) */
  gfx2d_assets_init();
  gfx2d_transform_init();
  gfx2d_effects_init();
  gui_widgets_init();
  gui_containers_init();
  gui_menus_init();
  gui_events_init();
  gui_themes_init();

  /* Theme API smoke test */
  ui_theme_set(ui_theme_windows95());
  ui_theme_set(ui_theme_dark_mode());
  ui_theme_set(ui_theme_pastel_dream());
  ui_theme_reset_default();
  ui_theme_save("/home/gfxgui_test.theme");
  ui_theme_load("/home/gfxgui_test.theme");

  /* Asset API smoke test */
  make_test_bmp("/home/gfxgui_test.bmp");
  img = gfx2d_image_load("/home/gfxgui_test.bmp");
  fnt_fd = vfs_open("/home/gfxgui_test.fnt", 0);
  if (fnt_fd >= 0) {
    vfs_close(fnt_fd);
    fnt = gfx2d_font_load("/home/gfxgui_test.fnt");
  }
  if (fnt >= 0) {
    gfx2d_font_set_default(fnt);
  }

  gfx2d_fullscreen_enter();

  surf = gfx2d_surface_alloc(96, 96);
  if (surf >= 0) {
    gfx2d_surface_set_active(surf);
    gfx2d_clear(0x223344);
    gfx2d_rect_fill(8, 8, 80, 80, 0x88CCFF);
    gfx2d_circle_fill(48, 48, 24, 0xFF8844);
    gfx2d_blur_box_surface(surf, 1);
    gfx2d_surface_unset_active();
  }

  while (tick < 260) {
    draw_base_scene(tick);

    if (img >= 0) {
      gfx2d_image_draw(img, 40, 90);
      gfx2d_image_draw_scaled(img, 120, 90, 96, 96);
      gfx2d_image_draw_region(img, 8, 8, 32, 32, 240, 90);
      px = gfx2d_image_get_pixel(img, 10, 10);
      gfx2d_rect_fill(240, 140, 24, 24, px);

      /* Transform API smoke test */
      gfx2d_push_transform();
      gfx2d_reset_transform();
      gfx2d_translate(460, 150);
      gfx2d_rotate((tick * 4) % 360);
      gfx2d_scale(fp_div(fp_from_int(3), 2), fp_div(fp_from_int(3), 2));
      gfx2d_image_draw_transformed(img, -32, -32);
      gfx2d_text_transformed(-48, 48, "transform", 0xFFFFFF, 1);
      gfx2d_get_matrix(m);
      m[4] = m[4] + 8;
      m[5] = m[5] + 4;
      gfx2d_set_matrix(m);
      gfx2d_transform_point(0, 0, &ox, &oy);
      gfx2d_circle_fill(ox, oy, 3, 0xFFFF00);
      gfx2d_pop_transform();
    }

    if (surf >= 0) {
      gfx2d_surface_blit(surf, 520, 340);
      gfx2d_surface_blit_alpha(surf, 420, 340, 160);
    }

    if ((tick % 2) == 0) {
      test_effects_cycle(tick);
    }

    if (fnt >= 0) {
      gfx2d_text_ex(24, 448, "text_ex + loaded font", 0xFFFFFF, fnt, 1 | 2 | 4);
    } else {
      gfx2d_text_ex(24, 448, "text_ex + fallback font", 0xFFFFFF, -1, 1 | 2 | 8);
    }

    gfx2d_text(24, 462, "This is a binding smoke test, not visual QA", 0xAAAAAA,
               1);

    gfx2d_flip();
    tick++;
    yield();
  }

  if (img >= 0) {
    gfx2d_image_free(img);
  }
  if (fnt >= 0) {
    gfx2d_font_free(fnt);
  }
  if (surf >= 0) {
    gfx2d_surface_free(surf);
  }

  gfx2d_fullscreen_exit();

  println("[gfxgui_test] done");
  return 0;
}
