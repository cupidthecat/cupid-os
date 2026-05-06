Source: https://github.com/ozkl/doomgeneric
Commit: dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284
Cloned: 2026-05-06

License: GPL-2 (DOOM core, id Software) + BSD-3 (doomgeneric shim, ozkl)

Local modifications:
  See git log under kernel/doom/src/.

Removed files (replaced by CupidOS platform code):
  - doomgeneric_sdl.c       (SDL2 platform)
  - doomgeneric_xlib.c      (X11 platform)
  - doomgeneric_allegro.c   (Allegro platform)
  - doomgeneric_win.c       (Win32 platform)
  - doomgeneric_emscripten.c (Emscripten platform)
  - doomgeneric_soso.c      (SOS platform)
  - doomgeneric_sosox.c     (SOS-X platform)
  - doomgeneric_linuxvt.c   (Linux VT platform)
  - i_sound.c               (stubbed — replaced in Task 16 by i_sound_cupidos.c)
  - i_sdlsound.c            (SDL2 sound backend)
  - i_sdlmusic.c            (SDL2 music backend)
  - i_allegromusic.c        (Allegro music backend)
  - i_allegrosound.c        (Allegro sound backend)
  - i_cdmus.c               (CD music backend)
  - memio.c / memio.h       (already vendored in kernel/audio/ with kernel modifications)
  - mus2mid.c               (already vendored in kernel/audio/mus2midi.c)

Modified files (compile fixes under -nostdinc -ffreestanding):
  All modifications are in-place patches for:
  - Removing <stdarg.h> usage (handled by dglibc_compat.h)
  - Removing <stdbool.h> usage (handled by kernel/types.h via dglibc_compat.h)
  - Fixing va_list references
  - Patching boolean type conflicts

  Per-file notes recorded in git commit messages.
