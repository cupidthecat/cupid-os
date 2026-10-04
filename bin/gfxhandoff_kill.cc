//help: Holds nested fullscreen ownership until another process kills it.
//help: Usage: gfxhandoff_kill <pid>

int main(void) {
  char *args = get_args();
  int pid = 0;
  int i = 0;
  if (!is_gui_mode()) {
    serial_printf("[gfxhandoff_kill] FAIL requires GUI mode\n");
    return 1;
  }
  if (!args || !args[0]) {
    serial_printf("[gfxhandoff_kill] FAIL requires target PID\n");
    return 1;
  }
  while (args[i] >= '0' && args[i] <= '9') {
    pid = pid * 10 + (args[i] - '0');
    i++;
  }
  if (pid <= 1 || args[i] != 0) {
    serial_printf("[gfxhandoff_kill] FAIL invalid target PID\n");
    return 1;
  }
  gfx2d_fullscreen_enter();
  gfx2d_fullscreen_enter();
  serial_printf("[gfxhandoff_kill] nested owner waiting for remote kill\n");
  if (!process_kill_after_ms(pid, 7000)) {
    serial_printf("[gfxhandoff_kill] FAIL delayed killer unavailable\n");
    return 1;
  }
  while (1)
    yield();
  return 1;
}
