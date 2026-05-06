# DOOM on CupidOS — Port + Audio Stack

**Status:** design  •  **Date:** 2026-05-06  •  **Target:** Freedoom 1+2 booting from `doom` shell command, full SFX + music

## Goal

Run id Software's DOOM as a first-class CupidOS shell program. Boots from `doom` at the prompt, draws into the existing 640×480×32 VBE framebuffer, takes PS/2 keyboard input, and produces full audio (SFX + OPL3-synthesised music) via a new AC97 driver and software mixer. Saves persist to homefs.

The work has two halves:

1. **Audio stack** (independent, reusable) — AC97 PCI driver, 16-channel software mixer, Nuked-OPL3 emulator, MUS→MIDI converter, GENMIDI FM synth. Useful beyond DOOM (smoke-test app, future music player).
2. **DOOM port** — vendored doomgeneric tree, CupidOS platform shim (video/input/timer), kernel-resident libc shim (`malloc`/`fopen`/`printf`/string ops mapped to existing kernel APIs), `i_sound` bridge wired to the audio stack.

## Non-Goals (v1)

- Multiplayer / netcode
- Mouse look (kbd-only controls; mouse can land later)
- HDA audio driver (AC97 only — covers QEMU + most pre-2010 hw; HDA gets its own spec)
- SB16 / PC speaker audio paths
- DMX OPL2 (SB Pro) mode — OPL3-only synth
- Cheats UI changes (works by default — type `iddqd` etc. on kbd)
- DEHACKED hot-load editor
- Demo recording (playback works; recording skipped)
- ENDOOM screen on quit

## Source upstream

- **doomgeneric** by ozkl (https://github.com/ozkl/doomgeneric) — Chocolate Doom 1.10 derivative with a single-file platform shim. Vendored under `kernel/doom/src/`.
- **Nuked-OPL3** by nukeykt — single-file OPL3 emulator, ~1200 LOC, bit-accurate. Vendored under `kernel/audio/nuked_opl3.c`.
- **mus2midi** routines — extracted from Chocolate Doom (`mus2mid.c`, ~200 LOC), vendored under `kernel/audio/mus2midi.c`.
- **GENMIDI** patch bank — read at runtime from the loaded WAD (DOOM ships a `GENMIDI` lump; Freedoom too).

All three upstreams are GPL/LGPL-compatible.

## Architecture

```
                    ┌──────────────────────────────────────────┐
                    │           shell builtin: doom            │
                    └──────────────────────────────────────────┘
                                       │
                                       ▼
   ┌──────────────────── kernel/doom/ ────────────────────┐
   │  doomgeneric_cupidos.c   platform shim (DG_* funcs)  │
   │  dglibc.c                libc bridge                 │
   │  i_sound_cupidos.c       sound + music API           │
   │  src/                    vendored doomgeneric tree   │
   └──────────────────────────────────────────────────────┘
        │              │              │              │
        ▼              ▼              ▼              ▼
   graphics.c     keyboard.c       vfs / kmalloc    audio mixer
   (VBE blit)     (PS/2 ringbuf)   (file + heap)         │
                                                         ▼
                              ┌──── kernel/audio/ ─────────────────┐
                              │  mixer.c        16-ch sw mixer     │
                              │  ac97.c         PCI bus master out │
                              │  nuked_opl3.c   FM synthesiser     │
                              │  mus2midi.c     MUS → MIDI         │
                              │  midiopl.c      MIDI → OPL3 regs   │
                              └────────────────────────────────────┘
```

Each layer has one job. AC97 owns the hardware ring; mixer owns the sample-mixing math; OPL3 owns FM synthesis; midiopl owns MIDI events; mus2midi owns DOOM's quirky MUS lump format. doomgeneric stays unmodified except where the platform shim plugs in.

## §1. AC97 driver (`kernel/audio/ac97.{c,h}`)

### Hardware target

Intel ICH AC97 (PCI VID 0x8086 DID 0x2415 / 0x2425 / 0x2445 / 0x2485 / 0x24C5 / 0x24D5 / 0x266E). QEMU emulates 0x2415 with `-device AC97`. Two BARs:

- **BAR0 (NAM, mixer registers)** — 256 bytes IO. Master/PCM-out volume, codec.
- **BAR1 (NABM, bus master)** — 64 bytes IO. PI/PO/MC stream descriptors, BDL pointers, run/pause, IRQ status.

We use only the **PCM Out** stream (NABM offset 0x10–0x1B).

### Init sequence

1. `pci_find_by_vendor_device` for known IDs; fallback to scan by class 0x04 subclass 0x01.
2. Read BAR0/BAR1 from PCI config; `pci_enable_bus_master`.
3. Cold-reset codec: write 0xFFFFFFFF to NABM `GLOB_CNT` reset bit, poll until clear.
4. Set master volume 0x0000 (loudest, both channels), PCM-out volume 0x0808.
5. Configure variable rate (codec ext-audio reg 0x2A): enable VRA, write sample rate 22050 to reg 0x2C.
6. Allocate **32-entry BDL** (Buffer Descriptor List), 8 bytes/entry, page-aligned via `kmalloc`. Each entry points at a 4 KB DMA buffer (1024 frames @ s16-stereo). 32 × 4 KB = 128 KB DMA pool.
7. Write BDL physical addr to NABM `PO_BDBAR` (0x10).
8. Set `PO_LVI` (last valid index) to 31.
9. `irq_install_handler(line, ac97_isr)` on PCI IRQ line.
10. Enable interrupts: `PO_CR` (0x1B) ← bits IOCE | LVBIE | FEIE | RPBM (run).

### IRQ path

Each time the DMA pointer crosses a buffer boundary, AC97 fires IRQ with `PO_SR` bit BCIS set. ISR:

1. Ack: write `PO_SR` |= BCIS.
2. Compute the buffer that just emptied: `(PO_CIV - 1) & 31`.
3. Call mixer fill callback to refill that buffer.
4. Bump `PO_LVI` by one to keep the ring rolling.

ISR runs under BKL. Mixer fill is bounded (≤ 2 ms on QEMU at 1024-frame buffers).

### API

```c
int  ac97_init(void);
void ac97_set_fill_callback(void (*fill)(int16_t *buf, uint32_t frames));
void ac97_start(void);
void ac97_stop(void);
void ac97_set_master_volume(uint8_t pct); /* 0–100 */
```

### Failure modes

- No AC97 device → `ac97_init` returns negative; mixer/DOOM run silent.
- IRQ never fires → watchdog at first DG_DrawFrame logs warning, audio is muted.

## §2. Audio mixer (`kernel/audio/mixer.{c,h}`)

### Format

s16 stereo little-endian @ 22050 Hz. Matches AC97 stream and DOOM's preferred output rate (DOOM SFX are u8 mono @ 11025 Hz — 2× upsample on load).

### Channels

16 fixed slots:

- 0–7: DOOM SFX (one per `S_NUMCHANNELS` doomgeneric default)
- 8: music render (OPL3 output)
- 9–15: reserved (audiotest, future music player, system bell)

Each slot:

```c
typedef struct {
    int16_t       *pcm;          /* s16 mono or stereo */
    uint32_t       len_frames;
    uint32_t       pos_frames;
    uint8_t        channels;     /* 1 or 2 */
    uint8_t        loop;
    uint8_t        active;
    uint8_t        vol_l, vol_r; /* 0–127 */
} mixer_slot_t;
```

### Fill function

Called by AC97 ISR with destination buffer + frame count:

```
for f in 0..frames:
    sum_l = 0; sum_r = 0
    for slot in 0..16 if slot.active:
        sample = slot.pcm[slot.pos_frames * slot.channels]
        sum_l += sample * slot.vol_l
        sum_r += sample * slot.vol_r (or sample for mono)
        slot.pos_frames++
        if slot.pos_frames >= slot.len_frames:
            if slot.loop: pos_frames = 0
            else: slot.active = 0
    out[f*2 + 0] = clamp16(sum_l >> 7)
    out[f*2 + 1] = clamp16(sum_r >> 7)
```

Fixed-point. No floats in the hot path — keeps ISR cheap.

### API

```c
int  mixer_init(void);
int  mixer_play(int slot, const int16_t *pcm, uint32_t frames,
                uint8_t channels, uint8_t loop, uint8_t vol_l, uint8_t vol_r);
void mixer_stop(int slot);
int  mixer_active(int slot);
void mixer_set_volume(int slot, uint8_t vol_l, uint8_t vol_r);
```

## §3. Nuked-OPL3 emulator (`kernel/audio/nuked_opl3.c`)

Vendored as-is (single .c file). Public API (declared in `nuked_opl3.h`):

```c
typedef struct opl3_chip opl3_chip;
void OPL3_Reset(opl3_chip *chip, uint32_t samplerate);
void OPL3_WriteRegBuffered(opl3_chip *chip, uint16_t reg, uint8_t v);
void OPL3_GenerateStream(opl3_chip *chip, int16_t *buf, uint32_t numsamples);
```

Configured at 49716 Hz (OPL3 native), then resampled in `midiopl.c` to 22050 Hz with a 4-tap linear-interpolation kernel (good enough for FM).

Memory: `opl3_chip` ~10 KB, single static instance in `midiopl.c`.

## §4. MUS → MIDI converter (`kernel/audio/mus2midi.{c,h}`)

DOOM stores music as MUS (id format) lumps. mus2midi parses MUS events and emits standard MIDI on-the-fly. We don't write a `.mid` file — events go straight to the synth.

### API

```c
typedef struct mus_player mus_player;
mus_player *mus_open(const uint8_t *mus_data, uint32_t len, uint8_t looping);
void        mus_close(mus_player *p);

/* Pull next MIDI event; returns deltatime in MIDI ticks, or -1 on end. */
int  mus_next_event(mus_player *p, uint8_t out[4]);
```

Loop handling: when MUS hits its end-of-track marker, rewind if `looping` was set.

## §5. MIDI → OPL3 synth (`kernel/audio/midiopl.{c,h}`)

Drives Nuked-OPL3 from MIDI events. Reads GENMIDI lump from the active WAD at music-system init and converts each of 175 GENMIDI patches (128 GM + 47 percussion) to an OPL3 voice config. Adlib voice allocation is round-robin across 18 OPL3 channels (9 melodic + 9 percussion-capable in 4-op mode disabled — keep it simple, all 18 melodic).

### API

```c
int  midiopl_init(const uint8_t *genmidi_lump, uint32_t lump_len);
void midiopl_reset(void);
void midiopl_event(const uint8_t midi[4], uint8_t len);
void midiopl_render(int16_t *out_stereo, uint32_t frames); /* @ 22050 Hz */
void midiopl_set_volume(uint8_t vol_0_127);
```

Render path: `midiopl_render` advances a per-call sub-tick clock, dispatches due events from `mus_player`, calls `OPL3_GenerateStream` at 49716 Hz into a scratch buffer, downsamples to 22050 Hz into `out_stereo`.

Music thread (kernel thread, kicked from `I_PlaySong`) calls `midiopl_render` to fill mixer slot 8 every ~46 ms.

## §6. DOOM port — libc shim (`kernel/doom/dglibc.{c,h}`)

doomgeneric and Chocolate Doom call standard C library functions. CupidOS builds with `-nostdlib -nostdinc`. We supply just enough to satisfy the port.

### Required symbols

| C library | Bridge target |
|---|---|
| `malloc / calloc / realloc / free` | `kmalloc / kfree` (memory.h) |
| `memcpy / memmove / memset / memcmp` | kernel/string.c |
| `strlen / strcpy / strncpy / strcat / strncat` | kernel/string.c |
| `strcmp / strncmp / strcasecmp / strncasecmp` | kernel/string.c (add `strcasecmp` if missing) |
| `strchr / strrchr / strstr / strdup` | kernel/string.c (add `strdup` via kmalloc) |
| `atoi / atol` | kernel/string.c |
| `sprintf / snprintf / vsprintf / vsnprintf` | minimal impl in dglibc (subset: `%d %u %x %s %c %p`) — DOOM doesn't use floats in format strings |
| `fprintf(stderr, ...) / printf` | format → `serial_write_string` |
| `puts / putchar` | `serial_write_string` |
| `fopen / fclose` | wraps `vfs_open` / `vfs_close`; FILE* is opaque struct holding fd + buffered EOF bit |
| `fread / fwrite` | `vfs_read` / `vfs_write` |
| `fseek / ftell / rewind` | `vfs_seek`; ftell tracked in FILE struct |
| `fgets / fgetc / fputc` | implement on top of fread/fwrite |
| `feof / ferror / clearerr` | flag in FILE struct |
| `exit / abort` | `longjmp` back to `doom_main`'s setjmp; print serial msg, return to shell |
| `getenv` | return NULL (DOOM uses `HOME`, `DOOMWADDIR` — we substitute defaults) |
| `time / gettimeofday` | wrap PIT ticks; `time()` returns seconds-since-boot |
| `qsort` | small introsort impl |
| `tolower / toupper / isdigit / isalpha / isspace / isprint` | dglibc |
| `assert` | macro → panic on failure (debug build) or no-op (release) |

### File layout

```c
struct FILE { int fd; int eof; int err; off_t pos; };
extern FILE *stdin, *stdout, *stderr; /* sentinel pointers; ops route to serial */
```

### setjmp / longjmp

DOOM uses `setjmp`/`longjmp` for the soft-restart exit path. CupidOS doesn't have these in libc. Add minimal x86-32 impl to `dglibc.c` (~30 lines asm — save ebx/esi/edi/ebp/esp/eip).

### `getenv` substitutions

```c
HOME       → "/home/doom"
DOOMWADDIR → "/disk/wads"
```

## §7. DOOM port — platform shim (`kernel/doom/doomgeneric_cupidos.c`)

Implements doomgeneric's contract:

```c
void DG_Init(void);
void DG_DrawFrame(void);
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs(void);
int DG_GetKey(int *pressed, unsigned char *doomkey);
void DG_SetWindowTitle(const char *title);
```

`DG_ScreenBuffer` is a doomgeneric global (640×400 ARGB8888 array). doomgeneric paints into it; we blit out.

### `DG_Init`

- `mixer_init`, `ac97_init` (best-effort — silent if absent), `ac97_start`.
- Allocate input ring buffer (256 events).
- Hook keyboard scancode callback (extend `drivers/keyboard.h` with a raw-event subscriber API; see §10).
- Save framebuffer pointer and dimensions.

### `DG_DrawFrame`

- Blit `DG_ScreenBuffer` into VBE backbuffer at `(0, 40)` (40-px black bar top + bottom).
- `gfx_present` (existing flip).
- No throttling — DOOM internally caps to 35 Hz via its own ticker.

### `DG_GetKey`

- Pop next event from input ring. Returns 1 if event present, 0 otherwise.
- `*pressed` = 1 for press, 0 for release.
- `*doomkey` = translated DOOM keycode (KEY_RIGHTARROW etc, see Chocolate Doom's `i_video.h`).

### Scancode → DOOM key map (§10)

| PS/2 scancode | DOOM key |
|---|---|
| 0x4B | KEY_LEFTARROW |
| 0x4D | KEY_RIGHTARROW |
| 0x48 | KEY_UPARROW |
| 0x50 | KEY_DOWNARROW |
| 0x1D | KEY_FIRE (Ctrl) |
| 0x39 | KEY_USE (Space) |
| 0x2A / 0x36 | KEY_RSHIFT (run) |
| 0x38 | KEY_RALT (strafe modifier) |
| 0x33 | ',' (strafe L) |
| 0x34 | '.' (strafe R) |
| 0x02–0x08 | '1'–'7' (weapons) |
| 0x01 | KEY_ESCAPE |
| 0x1C | KEY_ENTER |
| 0x0F | KEY_TAB (map) |
| 0x3B–0x44 | KEY_F1..KEY_F10 |
| 0x15 / 0x31 | 'y' / 'n' (dialog) |

Done in a static lookup table.

### `DG_SleepMs`

Yield-loop on PIT ticks (`pit_get_ticks` × 10 ms tick → ms). Calls `process_yield` to be cooperative with shell.

### `DG_GetTicksMs`

`pit_get_ticks() * 10` (PIT runs at 100 Hz in CupidOS).

### `DG_SetWindowTitle`

No-op (no window — fullscreen).

## §8. DOOM port — i_sound bridge (`kernel/doom/i_sound_cupidos.c`)

Replaces doomgeneric's stub `i_sound.c`. Provides the SFX + music functions DOOM core calls.

### SFX path

doomgeneric loads SFX lumps from WAD as `sfxinfo_t` records. We hook `I_StartSound` to:

1. Look up cached u8 PCM for this `sfxinfo`. If absent: 8-bit unsigned mono → s16 mono, 2× linear-interp upsample to 22050 Hz, cache via `kmalloc`.
2. Pick mixer slot 0–7 (LRU within DOOM's channel allocator — already done in `s_sound.c`, we just respect the channel index DOOM passes us).
3. `mixer_play(channel, pcm, frames, 1, 0, vol_l, vol_r)` with stereo separation from DOOM's `sep` arg.

### Music path

1. `I_PlaySong(handle, looping)` — `handle` is index into music lump table.
2. Read MUS lump from WAD via DOOM's lump reader (W_CacheLumpNum).
3. `mus_open(data, len, looping)` → `mus_player`.
4. `midiopl_reset()`.
5. Spawn (or reuse) music thread; thread loops:
   - Pull next event, advance synth tick clock.
   - When mixer slot 8 has < 1 buffer queued, call `midiopl_render` and feed via `mixer_play`-like primitive (extend mixer with a streaming-source variant).

`I_StopSong` joins thread, mutes slot 8.

`I_SetMusicVolume` updates `midiopl_set_volume` and slot 8 mixer volume.

### Streaming source (mixer extension)

Slot 8 needs to *pull* from a callback rather than hold a fixed PCM. Add:

```c
int mixer_play_stream(int slot,
                      void (*pull)(int16_t *out_stereo, uint32_t frames, void *ctx),
                      void *ctx,
                      uint8_t vol_l, uint8_t vol_r);
```

Pull happens inside `mixer_fill`, before final summation.

## §9. DOOM port — vendored tree (`kernel/doom/src/`)

Vendor doomgeneric verbatim (~80 .c files, ~250 KB compiled). Files live under `kernel/doom/src/`. We do **not** modify the source. All adaptations happen via shim files.

### Per-file build flag override

DOOM source won't compile under the kernel's strict flags (`-pedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wmissing-prototypes -Wstrict-prototypes`). Add a Makefile pattern rule for `kernel/doom/src/%.o` with relaxed flags:

```
kernel/doom/src/%.o: CFLAGS_OVERRIDE = -m32 -fno-pie -fno-stack-protector \
    -nostdlib -nostdinc -ffreestanding -c -I./kernel -I./drivers \
    -I./kernel/doom -I./kernel/doom/src -I./kernel/audio \
    -mfpmath=sse -msse -msse2 -mstackrealign -fno-omit-frame-pointer \
    -DDOOM_PORT_CUPIDOS -DNO_INTRO -O2 -Wno-unused -Wno-unused-result
```

Pattern rule overrides the kernel-wide `CFLAGS` for this subtree.

`kernel/doom/dglibc.o`, `kernel/doom/doomgeneric_cupidos.o`, `kernel/doom/i_sound_cupidos.o`, and `kernel/audio/*.o` build with strict flags — we own those.

### `doom_main(int argc, char **argv)`

Entry point exposed for shell. Calls doomgeneric's `D_DoomMain` after argv plumbing. setjmp envelope catches `exit()` → returns to shell.

## §10. Keyboard subscriber API (driver extension)

Existing `keyboard.h` exposes polling-style getters. DOOM needs an event stream (press + release). Extend with:

```c
typedef void (*kbd_event_cb)(uint8_t scancode, bool pressed, void *ctx);
int  keyboard_subscribe(kbd_event_cb cb, void *ctx);   /* returns handle */
void keyboard_unsubscribe(int handle);
```

Single subscriber slot is enough for v1 (DOOM is the only consumer). Subscriber callback fires from the IRQ tail in `drivers/keyboard.c`.

`DG_GetKey` simply drains an internal ring populated by the subscriber callback.

## §11. WAD packaging

WADs ship on the FAT16 partition at `/disk/wads/` so the kernel binary stays small and users can drop in extra WADs.

### Source paths (build host)

```
/usr/share/games/doom/freedoom1.wad
/usr/share/games/doom/freedoom2.wad
```

Detected via Makefile: `WADS := $(wildcard /usr/share/games/doom/freedoom*.wad)`. If empty, the build still succeeds — `doom` will just print "no WAD found".

### Image-build step

After kernel link and FAT16 image creation, use `mtools` to copy:

```
mmd  -i cupidos.img@@$(FAT_OFFSET_BYTES) ::/wads
mcopy -i cupidos.img@@$(FAT_OFFSET_BYTES) /usr/share/games/doom/freedoom1.wad ::/wads/
mcopy -i cupidos.img@@$(FAT_OFFSET_BYTES) /usr/share/games/doom/freedoom2.wad ::/wads/
```

`mtools` is on Arch by default (`pacman -Q mtools`). The Makefile already uses raw `dd` for FAT16 — adding `mtools` is an additive dep; document in README.

### Discovery order

1. argv `-iwad <path>`
2. `/disk/wads/freedoom1.wad`
3. `/disk/wads/freedoom2.wad`
4. `/home/doom/*.wad` (first match)
5. fail with helpful message

## §12. Save games

DOOM writes `doomsav0.dsg` … `doomsav5.dsg` via `fopen` in CWD. doomgeneric's `DEFAULT_SAVEGAMEDIR` macro controls path. Override at compile time:

```
-DDEFAULT_SAVEGAMEDIR="/home/doom/"
```

Shim ensures `/home/doom/` exists at first boot via `vfs_mkdir`.

Configuration file `default.cfg` likewise lives at `/home/doom/default.cfg`.

## §13. Shell wiring

### `kernel/shell.c`

Add to builtin command table:

```c
{ "doom", doom_main },
{ "audiotest", audiotest_main },
```

`doom_main` declared in `kernel/doom/doomgeneric_cupidos.h`. Returns to shell on quit (via setjmp envelope).

### `bin/doom.cc` (CupidC stub)

5-line CupidC wrapper that calls the kernel builtin — lets `doom` be visible from `ls /bin/`. Same pattern as existing apps.

## §14. Build system changes

### `Makefile`

```make
# DOOM source list (vendored)
DOOM_SRC := $(wildcard kernel/doom/src/*.c)
DOOM_SRC_OBJS := $(DOOM_SRC:.c=.o)
DOOM_OBJS := $(DOOM_SRC_OBJS) \
             kernel/doom/dglibc.o \
             kernel/doom/doomgeneric_cupidos.o \
             kernel/doom/i_sound_cupidos.o

# Audio stack
AUDIO_OBJS := kernel/audio/ac97.o \
              kernel/audio/mixer.o \
              kernel/audio/nuked_opl3.o \
              kernel/audio/mus2midi.o \
              kernel/audio/midiopl.o

KERNEL_OBJS += $(AUDIO_OBJS) $(DOOM_OBJS)

# Per-file relaxed flags for vendored DOOM tree
CFLAGS_DOOM := -m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc \
               -ffreestanding -c -I./kernel -I./drivers -I./kernel/doom \
               -I./kernel/doom/src -I./kernel/audio -mfpmath=sse -msse -msse2 \
               -mstackrealign -fno-omit-frame-pointer -O2 \
               -DDOOM_PORT_CUPIDOS \
               -DDEFAULT_SAVEGAMEDIR=\"/home/doom/\" \
               -Wno-unused -Wno-unused-result -Wno-implicit-function-declaration

kernel/doom/src/%.o: kernel/doom/src/%.c
	$(CC) $(CFLAGS_DOOM) -o $@ $<

kernel/audio/nuked_opl3.o: kernel/audio/nuked_opl3.c
	$(CC) $(CFLAGS_DOOM) -o $@ $<
```

`CFLAGS_DOOM` is also used for Nuked-OPL3 (third-party, won't pass strict flags).

### QEMU run target

```make
QEMU_FLAGS += -device AC97 -audiodev $(QEMU_AUDIODEV)
```

`QEMU_AUDIODEV` already defaults to `alsa,id=speaker`.

### Image-staging hook

Insert WAD copy step into the `cupidos.img` rule, after FAT16 init.

## §15. Memory budget

| Component | Bytes |
|---|---|
| AC97 BDL + DMA buffers | 32 × 4 KB = 128 KB |
| OPL3 chip state | 10 KB |
| OPL3 render scratch | 4 KB |
| GENMIDI patch table | ~40 KB (175 patches × ~230 bytes) |
| MUS player + MIDI ring | 8 KB |
| DOOM zone alloc (`Z_Init`) | 8 MB (DOOM default) |
| SFX cache (avg 60 sounds × 6 KB upsampled) | ~360 KB |
| Mixer slot table | 1 KB |
| Input ring | 1 KB |
| **Total** | **~9 MB** |

Kernel heap is 128 MB. Comfortable.

## §16. Performance budget

QEMU baseline (i7 host, KVM disabled — pessimistic):

| Path | Frequency | Budget | Expected |
|---|---|---|---|
| AC97 ISR + mixer fill | every 46 ms | 5 ms | ~2 ms |
| Music thread render (49716→22050) | every 46 ms | 5 ms | ~2 ms |
| `DG_DrawFrame` blit (640×400×4 → backbuffer + present) | every 28 ms (35 Hz) | 4 ms | ~1.5 ms |
| DOOM tic | every 28 ms | 15 ms | varies |

Total CPU well under 50% on QEMU at 2 GHz host.

## §17. Test plan

### Audio smoke (`bin/audiotest.cc` → `audiotest`)

- `audiotest sine` — 440 Hz tone, 2 s, both channels
- `audiotest sweep` — log sweep 50 Hz → 8 kHz, 4 s, verifies sample-rate path
- `audiotest opl` — three-note OPL3 chord, 2 s, verifies FM synth + register writes
- `audiotest pan` — 1 kHz, 4 s, panning L↔R, verifies stereo mix

### DOOM smoke

- `doom -iwad /disk/wads/freedoom1.wad` — title screen draws, demo loop plays with audio
- New game E1M1 — kills imp, picks up shotgun, hears music
- `F2 → save → F3 → load` — savegame round-trip
- Volume keys in menu attenuate music + SFX
- Quit (`F10 → y`) returns to shell prompt
- `doom -iwad /disk/wads/freedoom2.wad` — switches IWAD
- `doom -iwad /home/doom/custom.wad` — homefs path

### Headless (`make run-headless`)

- AC97 init returns failure cleanly (no `-device AC97` in headless run); DOOM still launches and runs silent. Shell tests pass.

### Real hardware

- Boot on a 2008-era box with AC97 codec — manual smoke per above.
- USB keyboard already supported — tested incidentally.

## §18. Risks and open questions

- **GENMIDI parsing.** Format is documented but quirky (different layouts for 2-op vs 4-op patches; we treat all as 2-op via OPL3-fallback bits). Plan to validate against Chocolate Doom's `genmidi.c` byte-for-byte.
- **AC97 IRQ sharing.** PCI line may be shared with USB or ATA. ISR must not block; LVI bookkeeping must be correct or DMA will underrun. Plan: log a debug line on every IRQ for the first 10 frames to verify.
- **DOOM's `setjmp` quitpath.** If our setjmp impl mishandles SSE state, returning from DOOM may corrupt FPU/SSE. Save/restore `MXCSR` + x87 state in `setjmp` impl. (CupidOS already FXSAVEs across context switches, so we only need to handle within-thread setjmp.)
- **Stack depth.** DOOM's BSP traversal recurses ~64 levels. Default kernel stack on shell-process is 16 KB — should hold, but monitor and bump to 64 KB if needed.
- **Music thread vs scheduler.** Need a kernel thread API that can sleep on a condition. CupidOS scheduler is preemptive RR with `process_yield` — sufficient. Music thread sleeps on a 46 ms timer.
- **AC97 absent on real hw without it.** Audio silently degrades. We do not attempt PC speaker fallback (would be a misuse).

## §19. Out-of-scope work that landed-on-top later

These get their own specs:

- **HDA driver** — modern motherboards.
- **Mouse look** — DOOM mouse aiming via PS/2 mouse driver.
- **Music player app** — generic `bin/play` for `.wav` / `.mid` / `.mus` standalone.
- **Volume keys in shell** — system-level audio control.
- **Demo recording.**

## File summary

```
kernel/audio/
  ac97.c            ~500 LOC      PCI AC97 driver
  ac97.h            ~50 LOC
  mixer.c           ~250 LOC      16-ch s16 mixer + streaming source
  mixer.h           ~50 LOC
  nuked_opl3.c      ~1200 LOC     vendored
  nuked_opl3.h      ~50 LOC       vendored
  mus2midi.c        ~200 LOC      vendored from Chocolate Doom
  mus2midi.h        ~30 LOC
  midiopl.c         ~600 LOC      MIDI → OPL3 + GENMIDI parser
  midiopl.h         ~40 LOC

kernel/doom/
  dglibc.c          ~600 LOC      libc shim
  dglibc.h          ~80 LOC
  doomgeneric_cupidos.c  ~400 LOC platform shim + scancode map + main entry
  doomgeneric_cupidos.h  ~30 LOC
  i_sound_cupidos.c ~300 LOC      SFX + music bridge
  src/                            vendored doomgeneric (~80 files, ~30 KLOC)

bin/
  doom.cc           ~10 LOC       CupidC stub → kernel doom_main
  audiotest.cc      ~80 LOC       smoke test (sine/sweep/opl/pan)

drivers/
  keyboard.c        +30 LOC       subscriber API
  keyboard.h        +10 LOC

kernel/
  shell.c           +5 LOC        register doom + audiotest
```

Total new code (excluding vendored): ~3.5 KLOC. Vendored: ~30 KLOC doomgeneric + 1.5 KLOC OPL3/MUS.

## Implementation order (preview — full plan in writing-plans)

1. PS/2 keyboard subscriber API (smallest, decouples).
2. AC97 driver + audiotest sine/sweep.
3. Mixer + audiotest pan.
4. Nuked-OPL3 vendor + audiotest opl.
5. mus2midi + midiopl + GENMIDI parser.
6. dglibc shim (no DOOM yet — unit-tested via tiny dummy program).
7. doomgeneric vendor + platform shim — title screen.
8. i_sound bridge — SFX working.
9. Music wiring — full audio.
10. Savegames + cfg + polish.
