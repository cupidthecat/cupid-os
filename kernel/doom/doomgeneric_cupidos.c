/* doomgeneric_cupidos.c
 * Task 13: Real CupidOS platform shim for doomgeneric.
 *
 * Replaces the Task 12 stubs with:
 *   DG_DrawFrame  — blits 640x400 ARGB into VBE backbuffer (y=40 letterbox)
 *   DG_GetKey     — drains ring buffer fed by keyboard_subscribe callback
 *   DG_SleepMs    — timer_sleep_ms wrapper
 *   DG_GetTicksMs — timer_get_uptime_ms wrapper
 *   DG_Init       — registers kbd subscriber, ensures /home/doom exists
 *   doom_main     — entry with dg_setjmp envelope so dg_exit returns to shell
 *
 * Built with CFLAGS_DOOM (no -include dglibc_compat.h).
 */

#include "doomgeneric_cupidos.h"
#include "dglibc.h"
#include "../../drivers/vga.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/serial.h"
#include "../../drivers/timer.h"
#include "../vfs.h"
#include "../types.h"

extern void process_yield(void);

/* doomgeneric global framebuffer — 640*400 ARGB.
 * Defined inside kernel/doom/src/doomgeneric.c. */
extern uint32_t *DG_ScreenBuffer;

#define DG_RESX      640
#define DG_RESY      400
#define INPUT_RING   256

/* ------------------------------------------------------------------ *
 * Keyboard ring buffer                                                 *
 * ------------------------------------------------------------------ */

typedef struct { uint8_t scancode; uint8_t pressed; } kbd_evt_t;
static kbd_evt_t s_kbd_ring[INPUT_RING];
static volatile uint32_t s_kbd_head = 0;
static volatile uint32_t s_kbd_tail = 0;

/* DOOM key codes — match chocolate-doom doomkeys.h */
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

/* Keyboard IRQ callback — runs with IRQs disabled; push only, no process */
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

/* ------------------------------------------------------------------ *
 * DG_Init                                                             *
 * ------------------------------------------------------------------ */

void DG_Init(void) {
    s_kbd_head = 0;
    s_kbd_tail = 0;
    keyboard_subscribe(on_kbd, (void *)0);
    /* Best-effort: ensure /home/doom exists for savegames. */
    vfs_mkdir("/home/doom");
    serial_write_string("[doom] DG_Init: platform shim ready\n");
}

/* ------------------------------------------------------------------ *
 * DG_DrawFrame                                                         *
 * ------------------------------------------------------------------ */

void DG_DrawFrame(void) {
    uint32_t *fb;
    int y_off;
    int x;
    int y;

    if (!DG_ScreenBuffer) { return; }

    fb = vga_get_framebuffer();
    if (!fb) { return; }

    /* VBE is 640x480; DOOM renders 640x400.
     * Centre vertically: y_off = (480 - 400) / 2 = 40. */
    y_off = (VGA_GFX_HEIGHT - DG_RESY) / 2;
    if (y_off < 0) { y_off = 0; }

    /* Clear top letterbox bar */
    for (y = 0; y < y_off; y++) {
        for (x = 0; x < VGA_GFX_WIDTH; x++) {
            fb[y * VGA_GFX_WIDTH + x] = 0u;
        }
    }

    /* Blit DG_ScreenBuffer (ARGB) into back buffer.
     * doomgeneric writes 0xAARRGGBB; VBE 32bpp is XRGB (0x00RRGGBB).
     * The alpha byte is already ignored by the display so a direct copy
     * works without channel-swapping. */
    for (y = 0; y < DG_RESY; y++) {
        uint32_t *src = &DG_ScreenBuffer[y * DG_RESX];
        uint32_t *dst = &fb[(y + y_off) * VGA_GFX_WIDTH];
        for (x = 0; x < DG_RESX; x++) {
            dst[x] = src[x];
        }
    }

    /* Clear bottom letterbox bar */
    for (y = y_off + DG_RESY; y < VGA_GFX_HEIGHT; y++) {
        for (x = 0; x < VGA_GFX_WIDTH; x++) {
            fb[y * VGA_GFX_WIDTH + x] = 0u;
        }
    }

    vga_mark_dirty_full();
    vga_flip();
}

/* ------------------------------------------------------------------ *
 * DG_SleepMs / DG_GetTicksMs                                          *
 * ------------------------------------------------------------------ */

void DG_SleepMs(uint32_t ms) {
    timer_sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    return timer_get_uptime_ms();
}

/* ------------------------------------------------------------------ *
 * DG_GetKey                                                           *
 * ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ *
 * DG_SetWindowTitle                                                    *
 * ------------------------------------------------------------------ */

void DG_SetWindowTitle(const char *t) { (void)t; }

/* ------------------------------------------------------------------ *
 * doom_main entry — wraps D_DoomMain in a dg_setjmp envelope so      *
 * dg_exit / dg_abort can longjmp back to the shell cleanly.          *
 * ------------------------------------------------------------------ */

extern void  D_DoomMain(void);
extern void  M_FindResponseFile(void);
extern int   myargc;
extern char **myargv;

static dg_jmp_buf s_doom_env;

int doom_main(int argc, char **argv) {
    myargc = argc;
    myargv = argv;

    if (dg_setjmp(s_doom_env) != 0) {
        /* Arrived here via dg_longjmp from dg_exit/dg_abort */
        keyboard_unsubscribe();
        serial_write_string("[doom] returned to shell\n");
        return 0;
    }

    /* Arm the exit trap so dg_exit will longjmp here */
    dg_arm_exit(s_doom_env);

    DG_Init();
    M_FindResponseFile();
    D_DoomMain();

    /* D_DoomMain normally never returns (enters game loop).
     * If it does return (no WAD, etc.) clean up gracefully. */
    keyboard_unsubscribe();
    serial_write_string("[doom] D_DoomMain returned\n");
    return 0;
}
