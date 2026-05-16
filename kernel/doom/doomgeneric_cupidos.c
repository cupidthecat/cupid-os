/* doomgeneric_cupidos.c
 * Task 13: Real CupidOS platform shim for doomgeneric.
 *
 * Replaces the Task 12 stubs with:
 *   DG_DrawFrame  - blits 640x400 ARGB into VBE backbuffer (y=40 letterbox)
 *   DG_GetKey     - drains ring buffer fed by keyboard_subscribe callback
 *   DG_SleepMs    - timer_sleep_ms wrapper
 *   DG_GetTicksMs - timer_get_uptime_ms wrapper
 *   DG_Init       - registers kbd subscriber, ensures /home/doom exists
 *   doom_main     - entry with dg_setjmp envelope so dg_exit returns to shell
 *
 * Built with CFLAGS_DOOM (no -include dglibc_compat.h).
*/

#include "doomgeneric_cupidos.h"
#include "dglibc.h"
#include "vga.h"
#include "keyboard.h"
#include "serial.h"
#include "timer.h"
#include "vfs.h"
#include "types.h"

extern void process_yield(void);

/* doomgeneric global framebuffer: 640*400 ARGB.
 * Defined inside kernel/doom/src/doomgeneric.c.*/
extern uint32_t *DG_ScreenBuffer;
extern void doomgeneric_Tick(void);

#define DG_RESX      640
#define DG_RESY      400
#define DOOMGENERIC_RESX DG_RESX
#define DOOMGENERIC_RESY DG_RESY
#define INPUT_RING   256

/* Keyboard ring buffer */

typedef struct { uint8_t scancode; uint8_t pressed; } kbd_evt_t;
static kbd_evt_t s_kbd_ring[INPUT_RING];
static volatile uint32_t s_kbd_head = 0;
static volatile uint32_t s_kbd_tail = 0;

/* DOOM key codes match chocolate-doom doomkeys.h */
#define DOOM_KEY_RIGHTARROW  0xAE
#define DOOM_KEY_LEFTARROW   0xAC
#define DOOM_KEY_UPARROW     0xAD
#define DOOM_KEY_DOWNARROW   0xAF
#define DOOM_KEY_FIRE        0xA3   /* CTRL */
#define DOOM_KEY_USE         0x20   /* SPACE */
#define DOOM_KEY_RSHIFT      0xA0
#define DOOM_KEY_RALT        0xA2
#define DOOM_KEY_ESCAPE      27
#define DOOM_KEY_ENTER       13
#define DOOM_KEY_TAB         9
#define DOOM_KEY_F1          (0x80 + 0x3B)
#define DOOM_KEY_F2          (0x80 + 0x3C)
#define DOOM_KEY_F3          (0x80 + 0x3D)
#define DOOM_KEY_F4          (0x80 + 0x3E)
#define DOOM_KEY_F5          (0x80 + 0x3F)
#define DOOM_KEY_F6          (0x80 + 0x40)
#define DOOM_KEY_F7          (0x80 + 0x41)
#define DOOM_KEY_F8          (0x80 + 0x42)
#define DOOM_KEY_F9          (0x80 + 0x43)
#define DOOM_KEY_F10         (0x80 + 0x44)

static unsigned char s_scan_to_doom[128] = {
    [0x4B] = DOOM_KEY_LEFTARROW,
    [0x4D] = DOOM_KEY_RIGHTARROW,
    [0x48] = DOOM_KEY_UPARROW,
    [0x50] = DOOM_KEY_DOWNARROW,
    [0x1D] = DOOM_KEY_FIRE,
    [0x39] = DOOM_KEY_USE,
    [0x2A] = DOOM_KEY_RSHIFT,
    [0x36] = DOOM_KEY_RSHIFT,
    [0x38] = DOOM_KEY_RALT,
    [0x33] = ',',
    [0x34] = '.',
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7',
    [0x01] = DOOM_KEY_ESCAPE,
    [0x1C] = DOOM_KEY_ENTER,
    [0x0F] = DOOM_KEY_TAB,
    [0x3B] = DOOM_KEY_F1,  [0x3C] = DOOM_KEY_F2,  [0x3D] = DOOM_KEY_F3,
    [0x3E] = DOOM_KEY_F4,  [0x3F] = DOOM_KEY_F5,  [0x40] = DOOM_KEY_F6,
    [0x41] = DOOM_KEY_F7,  [0x42] = DOOM_KEY_F8,  [0x43] = DOOM_KEY_F9,
    [0x44] = DOOM_KEY_F10,
    [0x15] = 'y',
    [0x31] = 'n',
};

/* Keyboard IRQ callback runs with IRQs disabled; push only, no process */
static void on_kbd(uint8_t sc, bool pressed, void *ctx) {
    (void)ctx;
    /* Ignore scancodes we don't map */
    if (sc >= 128u) { return; }
    uint32_t next = (s_kbd_head + 1u) % INPUT_RING;
    if (next == s_kbd_tail) { return; } /* drop on overflow */
    s_kbd_ring[s_kbd_head].scancode = sc;
    s_kbd_ring[s_kbd_head].pressed  = pressed ? 1u : 0u;
    s_kbd_head = next;
}

/* DG_Init */

void DG_Init(void) {
    s_kbd_head = 0;
    s_kbd_tail = 0;
    keyboard_subscribe(on_kbd, (void *)0);
    /* Best-effort: ensure /home/doom exists for savegames. */
    vfs_mkdir("/home/doom");
    serial_write_string("[doom] DG_Init: platform shim ready\n");
}

/* DG_DrawFrame */

/* DG_ScreenBuffer aliases the 640x400 body slice of the VGA back buffer
 * (set up at startup in doom_main). doomgeneric writes its frame
 * directly into back_buffer at y_off, so DG_DrawFrame does not need to
 * memcpy. vga_flip's existing back->LFB copy is the only blit per frame.
 * That removes a full 1 MB cached-RAM copy from every game frame, which
 * is the biggest single CPU win for sustained framerate (and the
 * stability win for music: less time in the main thread blocked on
 * memory bandwidth = more headroom for the AC97 IRQ + cup_music_pump).*/
void DG_DrawFrame(void) {
    if (!DG_ScreenBuffer) { return; }
    vga_mark_dirty_full();
    vga_flip();
}

/* DG_SleepMs / DG_GetTicksMs */

/* USB host controllers expose interrupt URBs only via cooperative polling
 * from kernel_check_reschedule().  DOOM owns the kernel main thread and
 * never yields, so we drain pending USB events ourselves on each timer
 * query / sleep call. Without this, USB-attached keyboards never reach
 * DOOM.*/
extern void ehci_poll_interrupts(void);
extern void uhci_poll_interrupts(void);
extern void uhci_poll_ports(void);
extern void usb_process_pending(void);

/* Producer for the music ring buffer (defined in i_sound_cupidos.c).
 * Synthesises OPL3 audio ahead of the AC97 IRQ on the main thread so
 * the IRQ becomes a pure memcpy.*/
extern void cup_music_pump(void);

extern uint64_t get_cpu_freq(void);

static inline uint64_t cup_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* DOOM's TryRunTics busy-loops on DG_GetTicksMs at thousands of Hz.
 * Polling the USB host stack on every call burns CPU we'd rather give
 * to OPL synthesis (cup_music_pump) and the renderer. USB device
 * interrupts only need ~1 ms granularity for input. Sub-ms polling
 * provides no user-visible benefit.*/
static uint32_t s_last_usb_pump_ms = 0u;

static void cup_pump_usb_throttled(uint32_t now_ms) {
    if (now_ms - s_last_usb_pump_ms < 1u) return;
    s_last_usb_pump_ms = now_ms;
    ehci_poll_interrupts();
    uhci_poll_ports();
    uhci_poll_interrupts();
    usb_process_pending();
}

/* TSC-based clock for DOOM. The kernel PIT runs at 100 Hz, so
 * timer_get_uptime_ms() only advances in 10 ms steps. DOOM's I_GetTime
 * computes `ms * 35 / 1000` from this. At 10 ms granularity the
 * derived game tic advances in big jumps and jitters by up to one
 * full tic (28 ms) relative to wall-clock. TryRunTics then either
 * runs no tic (waiting for the timer to step) or runs a backlog all
 * at once. Input feels laggy because a key press at t=15 ms isn't
 * processed until the timer steps to 20 ms and then DOOM thinks we're
 * still at game tic 0. RDTSC gives cycle-level precision so
 * DG_GetTicksMs increments smoothly per call.*/
static uint64_t s_tsc_origin    = 0;
static uint64_t s_tsc_per_ms    = 0;

static void cup_clock_init(void) {
    if (s_tsc_per_ms != 0) return;
    s_tsc_per_ms = get_cpu_freq() / 1000u;
    if (s_tsc_per_ms == 0) s_tsc_per_ms = 1;
    s_tsc_origin = cup_rdtsc();
}

static uint32_t cup_get_ticks_ms(void) {
    cup_clock_init();
    uint64_t now = cup_rdtsc() - s_tsc_origin;
    return (uint32_t)(now / s_tsc_per_ms);
}

void DG_SleepMs(uint32_t ms) {
    /* Force IF=1. DOOM busy-waits on this in TryRunTics, and the shell
     * command path can leave us with IF=0 (BKL save/restore cycle).
     * Without IF=1 the PIT IRQ never fires and the loop spins forever.*/
    __asm__ volatile("sti");

    /* Busy-wait against the TSC clock instead of timer_sleep_ms()'s
     * HLT loop. timer_sleep_ms quantises to 10 ms PIT ticks and the
     * `hlt` parks the CPU until the next IRQ. Fine for power but it
     * means I_Sleep(1) actually sleeps 0 or 10 ms (never 1 ms). DOOM
     * calls I_Sleep(1) inside TryRunTics specifically to give the next
     * tic time to become ready; we want that wait short and accurate.
     *
     * While spinning we keep the music ring topped up and drain any
     * USB events. The CPU stays warm. Fine, we're a game, not a
     * mail server.*/
    cup_clock_init();
    uint64_t target = cup_rdtsc() + (uint64_t)ms * s_tsc_per_ms;
    cup_pump_usb_throttled(cup_get_ticks_ms());
    cup_music_pump();
    while (cup_rdtsc() < target) {
        cup_music_pump();
        cup_pump_usb_throttled(cup_get_ticks_ms());
        __asm__ volatile("pause");
    }
}

uint32_t DG_GetTicksMs(void) {
    __asm__ volatile("sti");
    uint32_t now = cup_get_ticks_ms();
    cup_pump_usb_throttled(now);
    cup_music_pump();
    return now;
}

/* DG_GetKey */

int DG_GetKey(int *pressed, unsigned char *doomkey) {
    kbd_evt_t e;
    unsigned char dk;

    if (s_kbd_tail == s_kbd_head) { return 0; }
    e = s_kbd_ring[s_kbd_tail];
    s_kbd_tail = (s_kbd_tail + 1u) % INPUT_RING;

    if (e.scancode >= 128u) { return 0; }
    dk = s_scan_to_doom[e.scancode];
    if (dk == 0) { return 0; }

    *pressed = e.pressed ? 1 : 0;
    *doomkey  = dk;
    return 1;
}

/* DG_SetWindowTitle */

void DG_SetWindowTitle(const char *t) { (void)t; }

/* doom_main entry: wraps D_DoomMain in a dg_setjmp envelope so
 * dg_exit / dg_abort can longjmp back to the shell cleanly.*/

extern void  D_DoomMain(void);
extern void  M_FindResponseFile(void);
extern int   myargc;
extern char **myargv;

extern int strcmp(const char *a, const char *b);

static int file_exists(const char *path) {
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) { return 0; }
    vfs_close(fd);
    return 1;
}

static const char *find_default_iwad(void) {
    /* FAT16 truncates long filenames to 8.3 form, so we probe both the
     * friendly names (homefs preserves them) and the 8.3 short names
     * that the raw fat16 VFS at /disk exposes.*/
    static const char *candidates[] = {
        "/disk/wads/freedoom1.wad",  /* long name (if LFN supported) */
        "/disk/wads/freedo~1.wad",   /* 8.3 short name generated by mtools */
        "/disk/wads/freedoom2.wad",
        "/disk/wads/freedo~2.wad",
        "/disk/wads/doom.wad",
        "/disk/wads/doom2.wad",
        "/disk/wads/doom~1.wad",
        "/disk/wads/doom~2.wad",
        "/home/doom/freedoom1.wad",
        "/home/doom/freedoom2.wad",
        "/home/doom/doom.wad",
        "/home/doom/doom2.wad",
        0
    };
    int i;
    for (i = 0; candidates[i]; i++) {
        if (file_exists(candidates[i])) {
            return candidates[i];
        }
    }
    return 0;
}

static dg_jmp_buf s_doom_env;

int doom_main(int argc, char **argv) {
    int has_iwad;
    int i;
    static char *new_argv[16];
    int new_argc;

    myargc = argc;
    myargv = argv;

    /* If user didn't pass -iwad, auto-discover and inject */
    has_iwad = 0;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-iwad") == 0) {
            has_iwad = 1;
            break;
        }
    }

    new_argc = argc;
    if (!has_iwad) {
        const char *def = find_default_iwad();
        if (!def) {
            dg_printf("doom: no WAD found in /disk/wads/ or /home/doom/.\n");
            dg_printf("       try: doom -iwad /path/to/your.wad\n");
            return 1;
        }
        if (argc + 2 > 16) {
            dg_printf("doom: too many args\n");
            return 1;
        }
        for (i = 0; i < argc; i++) { new_argv[i] = argv[i]; }
        new_argv[argc]     = (char *)"-iwad";
        new_argv[argc + 1] = (char *)def;
        new_argc = argc + 2;
        myargv = new_argv;
        myargc = new_argc;
        dg_printf("doom: using IWAD %s\n", def);
    }

    if (dg_setjmp(s_doom_env) != 0) {
        /* Arrived here via dg_longjmp from dg_exit/dg_abort */
        keyboard_unsubscribe();
        serial_write_string("[doom] returned to shell\n");
        return 0;
    }

    /* Arm the exit trap so dg_exit will longjmp here */
    dg_arm_exit(s_doom_env);

    /* Force IF=1.  Shell command path can leave us with interrupts disabled
     * (BKL save/restore in process_yield).  DOOM busy-waits on PIT-driven
     * timer in TryRunTics, so without IF=1 the wait spins forever.*/
    __asm__ volatile("sti");

    /* Wipe the framebuffer so leftover terminal/desktop pixels don't
     * leak through during DOOM frames where some columns (border, sky,
     * etc.) skip writes.*/
    {
        uint32_t *fb = vga_get_framebuffer();
        if (fb) {
            uint32_t i;
            for (i = 0; i < (uint32_t)(VGA_GFX_WIDTH * VGA_GFX_HEIGHT); i++) {
                fb[i] = 0u;
            }
            vga_mark_dirty_full();
            vga_flip();
        }
    }

    /* doomgeneric expects DG_ScreenBuffer pre-allocated. We point it at
     * the 640x400 body slice of the VGA back buffer (centred vertically
     * with a 40-row letterbox top + bottom). This means doomgeneric's
     * frame writes land directly in cached back-buffer RAM at the
     * displayed offset, so DG_DrawFrame is just a vga_flip. The
     * previous heap-alloc + per-frame ARGB copy is gone. The 80 rows
     * of black letterbox bars are written once here and never touched
     * again, since DG_ScreenBuffer never points outside its 400-row
     * window.*/
    if (!DG_ScreenBuffer) {
        uint32_t *fb = vga_get_framebuffer();
        if (!fb) {
            serial_write_string("[doom] no back buffer for DG_ScreenBuffer\n");
            return 1;
        }
        int y_off = (VGA_GFX_HEIGHT - DG_RESY) / 2;
        if (y_off < 0) { y_off = 0; }
        DG_ScreenBuffer = &fb[(uint32_t)y_off * (uint32_t)VGA_GFX_WIDTH];
    }

    DG_Init();
    M_FindResponseFile();
    D_DoomMain();

    /* D_DoomMain returns after one tic in this port; drive the game loop
     * here. sti each iteration in case BKL save/restore left IF=0.*/
    for (;;) {
        __asm__ volatile("sti");
        doomgeneric_Tick();
    }

    /* Unreachable, but keep cleanup in case the loop ever exits. */
    keyboard_unsubscribe();
    serial_write_string("[doom] D_DoomMain returned\n");
    return 0;
}
