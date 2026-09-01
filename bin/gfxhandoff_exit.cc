//help: Verifies fullscreen ownership cleanup when a child process exits.
//help: Usage: gfxhandoff_exit

int main(void) {
  if (!is_gui_mode()) {
    serial_printf("[gfxhandoff_exit] FAIL requires GUI mode\n");
    return 1;
  }

  gfx2d_fullscreen_enter();
  gfx2d_fullscreen_enter();
  if (!process_kill_after_ms(0, 0)) {
    serial_printf("[gfxhandoff_exit] FAIL delayed stale-kill unavailable\n");
    return 1;
  }
  serial_printf("[gfxhandoff_exit] nested owner exiting\n");
  exit();

  serial_printf("[gfxhandoff_exit] FAIL exit returned\n");
  return 1;
}
