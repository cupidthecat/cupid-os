/* gfxdemo.cc — 2D graphics showcase for cupid-os gfx2d library
 *
 * Demonstrates gfx2d capabilities:
 *   - Animated gradient background
 *   - Bezier curves with bounce tweening
 *   - Filled shapes (rectangles, circles, triangles)
 *   - Particle system
 *   - Text rendering with shadow
 */

int main() {
  gfx2d_fullscreen_enter();

  int tick = 0;
  int particles = gfx2d_particles_create();
  int done = 0;

  while (!done) {
    /* Smooth animated gradient background */
    int t = tick / 4;
    int c1 = ((t * 2) % 128) << 16;
    int c2 = ((t * 3) % 128) << 8 | ((t * 5) % 128);
    gfx2d_gradient_v(0, 0, 640, 480, c1, c2);

    /* Bouncing bezier curves */
    int bx = gfx2d_tween_bounce(tick % 256, 100, 540, 255);
    gfx2d_bezier(50, 150, bx, 50, 590, 150, 0xFFFFFF);
    gfx2d_bezier(50, 180, bx, 280, 590, 180, 0xFFDD88);

    /* Animated circles */
    int cx = 320 + gfx2d_tween_ease_in_out(tick % 128, -100, 100, 127);
    gfx2d_circle_fill(cx, 300, 40, 0xFF4488);
    gfx2d_circle(cx, 300, 45, 0xFFFFFF);

    /* Bouncing triangle */
    int tri_x = gfx2d_tween_elastic(tick % 128, 450, 550, 127);
    gfx2d_tri_fill(tri_x, 380, tri_x + 60, 380, tri_x + 30, 320, 0x88FF88);

    /* Rectangles with different styles */
    gfx2d_rect_fill(50, 350, 100, 80, 0x4488FF);
    gfx2d_rect(50, 350, 100, 80, 0xFFFFFF);
    gfx2d_rect_round_fill(180, 350, 100, 80, 10, 0xFF8844);

    /* Particle emitter - colorful sparkles */
    if ((tick % 3) == 0) {
      int vx = ((tick * 37) % 12) - 6;
      int vy = -4 - ((tick * 13) % 3);
      int color = 0xFF0000 | ((tick * 17) % 256) << 8 | ((tick * 7) % 256);
      gfx2d_particle_emit(particles, 320, 240, vx, vy, color, 60);
    }
    gfx2d_particles_update(particles, 1);
    gfx2d_particles_draw(particles);

    /* Title text with shadow */
    gfx2d_text_shadow(180, 30, "cupid-os gfx2d demo", 0xFFFFFF, 0x000000, 2);

    /* Instructions */
    gfx2d_text(220, 450, "demo will exit automatically", 0xAAAAAA, 1);

    gfx2d_flip();
    tick++;

    /* Exit after 400 frames */
    if (tick > 400)
      done = 1;
    yield();
  }

  gfx2d_particles_free(particles);
  gfx2d_fullscreen_exit();
  return 0;
}
