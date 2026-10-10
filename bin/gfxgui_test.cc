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

int make_test_font(const char *path) {
  int header[7];
  char glyph[8];
  int fd = -1;

  header[0] = 0x00544E46;
  header[1] = 1;
  header[2] = 8;
  header[3] = 8;
  header[4] = 65;
  header[5] = 65;
  header[6] = 0;

  glyph[0] = 0xFF;
  glyph[1] = 0x24;
  glyph[2] = 0x42;
  glyph[3] = 0x7E;
  glyph[4] = 0x42;
  glyph[5] = 0x42;
  glyph[6] = 0x42;
  glyph[7] = 0;

  fd = vfs_open(path, 0x0301);
  if (fd < 0) {
    return -1;
  }
  if (vfs_write(fd, header, 28) != 28 ||
      vfs_write(fd, glyph, 8) != 8) {
    vfs_close(fd);
    return -1;
  }
  vfs_close(fd);
  return 0;
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
  int px = 0;
  int m[6];
  int ox = 0;
  int oy = 0;
  int handoff = 0;
  int transform_failed = 0;

  println("[gfxgui_test] init");
  serial_printf("[gfxgui_test] init\n");

  if (!is_gui_mode()) {
    serial_printf("[gfxgui_test] FAIL requires GUI mode\n");
    println("[gfxgui_test] requires GUI mode");
    println("[gfxgui_test] open Desktop terminal and run again");
    return 1;
  }

  /* Module init calls (bindings smoke test) */
  gfx2d_assets_init();
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
  if (ui_theme_save("/gfxgui_test.theme") != 0 ||
      ui_theme_load("/gfxgui_test.theme") != 0) {
    serial_printf("[gfxgui_test] FAIL theme round trip\n");
    return 1;
  }

  /* Asset API smoke test */
  if (make_test_bmp("/gfxgui_test.bmp") != 0) {
    serial_printf("[gfxgui_test] FAIL BMP creation\n");
    return 1;
  }
  img = gfx2d_image_load("/gfxgui_test.bmp");
  if (img < 0) {
    serial_printf("[gfxgui_test] FAIL image load\n");
    return 1;
  }
  if (make_test_font("/gfxgui_test.fnt") != 0) {
    serial_printf("[gfxgui_test] FAIL font creation\n");
    gfx2d_image_free(img);
    return 1;
  }
  fnt = gfx2d_font_load("/gfxgui_test.fnt");
  if (fnt < 0) {
    serial_printf("[gfxgui_test] FAIL font load\n");
    gfx2d_image_free(img);
    return 1;
  }
  serial_printf("[gfxgui_test] assets ready\n");

  for (handoff = 0; handoff < 32; handoff++) {
    gfx2d_fullscreen_enter();
    gfx2d_blend_mode(0);
    if (handoff == 0)
      serial_printf("[gfxgui_test] fullscreen\n");
    gfx2d_rect_fill(16, 16, 8, 8, 0x010203);
    gfx2d_text_ex(16, 16, "A", 0xFFFFFF, fnt, 0);
    px = gfx2d_getpixel(16, 16) & 0x00FFFFFF;
    if (px != 0x00FFFFFF) {
      serial_printf("[gfxgui_test] FAIL font pixel handoff=%d value=%x\n",
                    handoff, px);
      gfx2d_fullscreen_exit();
      gfx2d_font_free(fnt);
      gfx2d_image_free(img);
      return 1;
    }
    gfx2d_fullscreen_exit();
    yield();
  }
  gfx2d_fullscreen_enter();
  gfx2d_blend_mode(0);
  gfx2d_transform_init();
  gfx2d_font_set_default(fnt);
  serial_printf("[gfxgui_test] font ready\n");

  gfx2d_pixel(4, 4, 0x123456);
  surf = gfx2d_surface_alloc(96, 96);
  if (surf < 0) {
    serial_printf("[gfxgui_test] FAIL surface allocation\n");
    gfx2d_fullscreen_exit();
    gfx2d_font_free(fnt);
    gfx2d_image_free(img);
    return 1;
  }
  gfx2d_surface_set_active(surf);
  gfx2d_clear(0x223344);
  gfx2d_rect_fill(8, 8, 80, 80, 0x88CCFF);
  gfx2d_circle_fill(48, 48, 24, 0xFF8844);
  gfx2d_rect_fill(3, 3, 3, 3, 0x000000);
  gfx2d_pixel(4, 4, 0xFFFFFF);
  gfx2d_surface_unset_active();
  gfx2d_blur_box_surface(surf, 1);
  gfx2d_surface_set_active(surf);
  px = gfx2d_getpixel(4, 4) & 0x00FFFFFF;
  gfx2d_surface_unset_active();
  if (px != 0x001C1C1C) {
    serial_printf("[gfxgui_test] FAIL surface blur pixel %x\n", px);
    gfx2d_surface_free(surf);
    gfx2d_fullscreen_exit();
    gfx2d_font_free(fnt);
    gfx2d_image_free(img);
    return 1;
  }
  if ((gfx2d_getpixel(4, 4) & 0x00FFFFFF) != 0x00123456) {
    serial_printf("[gfxgui_test] FAIL surface isolation\n");
    gfx2d_surface_free(surf);
    gfx2d_fullscreen_exit();
    gfx2d_font_free(fnt);
    gfx2d_image_free(img);
    return 1;
  }
  serial_printf("[gfxgui_test] surface ready\n");

  gfx2d_push_transform();
  gfx2d_translate(100, 100);
  gfx2d_rotate(90);
  gfx2d_scale(fp_from_int(2), fp_from_int(3));
  gfx2d_transform_point(2, 3, &ox, &oy);
  if (ox != 91 || oy != 104) {
    serial_printf("[gfxgui_test] FAIL transform linear %d,%d\n", ox, oy);
    transform_failed = 1;
  }
  gfx2d_pop_transform();
  gfx2d_get_matrix(m);
  if (m[0] != fp_from_int(1) || m[1] != 0 || m[2] != 0 ||
      m[3] != fp_from_int(1) || m[4] != 0 || m[5] != 0) {
    serial_printf("[gfxgui_test] FAIL transform restore\n");
    transform_failed = 1;
  }
  if (transform_failed) {
    gfx2d_surface_free(surf);
    gfx2d_fullscreen_exit();
    gfx2d_font_free(fnt);
    gfx2d_image_free(img);
    return 1;
  }

  while (tick < 260) {
    if ((tick % 24) == 0) {
      serial_printf("[gfxgui_test] frame %d begin\n", tick);
    }
    draw_base_scene(tick);

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
    gfx2d_scale(fp_div(fp_from_int(3), fp_from_int(2)),
                fp_div(fp_from_int(3), fp_from_int(2)));
    gfx2d_image_draw_transformed(img, -32, -32);
    if (tick == 0 &&
        (gfx2d_getpixel(460, 150) & 0x00FFFFFF) != 0x00808080) {
      serial_printf("[gfxgui_test] FAIL transformed pixel\n");
      transform_failed = 1;
    }
    if (tick == 0 &&
        (gfx2d_getpixel(484, 150) & 0x00FFFFFF) != 0x00BC809E) {
      serial_printf("[gfxgui_test] FAIL transformed scale pixel\n");
      transform_failed = 1;
    }
    gfx2d_text_transformed(-48, 48, "A", 0xFFFFFF, fnt);
    gfx2d_get_matrix(m);
    m[4] = m[4] + fp_from_int(8);
    m[5] = m[5] + fp_from_int(4);
    gfx2d_set_matrix(m);
    gfx2d_transform_point(0, 0, &ox, &oy);
    if (ox != 468 || oy != 154) {
      serial_printf("[gfxgui_test] FAIL transform point %d,%d\n", ox, oy);
      transform_failed = 1;
    }
    gfx2d_circle_fill(ox, oy, 3, 0xFFFF00);
    gfx2d_pop_transform();
    if (tick == 0 && !transform_failed) {
      serial_printf("[gfxgui_test] transform ready\n");
    }
    if (transform_failed) {
      break;
    }

    gfx2d_surface_blit(surf, 520, 340);
    gfx2d_surface_blit_alpha(surf, 420, 340, 160);

    if ((tick % 2) == 0) {
      test_effects_cycle(tick);
    }

    gfx2d_text_ex(24, 448, "A", 0xFFFFFF, fnt, 1 | 2 | 4);

    gfx2d_text(24, 462, "This is a binding smoke test, not visual QA", 0xAAAAAA,
               1);

    gfx2d_flip();
    if ((tick % 24) == 0) {
      serial_printf("[gfxgui_test] frame %d done\n", tick);
    }
    tick++;
    yield();
  }

  gfx2d_image_free(img);
  gfx2d_font_free(fnt);
  gfx2d_surface_free(surf);

  gfx2d_fullscreen_exit();

  if (transform_failed) {
    return 1;
  }

  println("[gfxgui_test] done");
  serial_printf("[gfxgui_test] done\n");
  return 0;
}
