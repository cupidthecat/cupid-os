/* gfxtest.cc — Simple gfx2d test to verify rendering works */

int main() {
  gfx2d_fullscreen_enter();

  int tick = 0;
  int done = 0;

  while (!done) {
    /* Simple clear to color based on tick */
    int r = (tick * 2) % 256;
    int g = (tick * 3) % 256;
    int b = (tick * 5) % 256;
    gfx2d_clear((r << 16) | (g << 8) | b);

    /* Draw a simple rectangle */
    gfx2d_rect_fill(100, 100, 200, 150, 0xFFFFFF);

    /* Draw some text */
    gfx2d_text(120, 150, "gfx2d works!", 0x000000, 1);

    gfx2d_flip();
    tick++;

    /* Exit after 100 frames */
    if (tick > 100)
      done = 1;
    yield();
  }

  gfx2d_fullscreen_exit();
  return 0;
}
