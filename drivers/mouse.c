/**
 * mouse.c - PS/2 mouse driver for cupid-os
 *
 * Implements:
 *  - PS/2 auxiliary device initialization
 *  - IRQ12 handler for 3-byte mouse packets
 *  - Cursor rendering on the VGA back buffer
 */

#include "mouse.h"
#include "../kernel/ports.h"
#include "../kernel/isr.h"
#include "../kernel/pic.h"
#include "../drivers/vga.h"
#include "../kernel/graphics.h"
#include "../drivers/serial.h"

/* ── Global mouse state ───────────────────────────────────────────── */
mouse_state_t mouse = { 320, 240, 0, 0, 0, false };
static bool has_scroll_wheel = false;

/* ── Cursor bitmap (8x10 arrow) ───────────────────────────────────── */
#define CURSOR_W 8
#define CURSOR_H 10

static const uint8_t cursor_bitmap[CURSOR_H] = {
    0x80, /* X....... */
    0xC0, /* XX...... */
    0xE0, /* XXX..... */
    0xF0, /* XXXX.... */
    0xF8, /* XXXXX... */
    0xFC, /* XXXXXX.. */
    0xFE, /* XXXXXXX. */
    0xF0, /* XXXX.... */
    0xD8, /* XX.XX... */
    0x18  /* ...XX... */
};

/* Outline mask for visibility */
static const uint8_t cursor_outline[CURSOR_H] = {
    0xC0, /* XX...... */
    0xE0, /* XXX..... */
    0xF0, /* XXXX.... */
    0xF8, /* XXXXX... */
    0xFC, /* XXXXXX.. */
    0xFE, /* XXXXXXX. */
    0xFF, /* XXXXXXXX */
    0xF8, /* XXXXX... */
    0xFC, /* XXXXXX.. */
    0x3C  /* ..XXXX.. */
};

/* Save-under buffer (32bpp pixels) */
static uint32_t under_cursor[CURSOR_W * CURSOR_H];
static int16_t saved_x = -1, saved_y = -1;

/* ── PS/2 controller helpers ──────────────────────────────────────── */

static void mouse_wait_write(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(inb(0x64) & 0x02)) return;
    }
}

static void mouse_wait_read(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(0x64) & 0x01) return;
    }
}

static void mouse_cmd(uint8_t cmd) {
    mouse_wait_write();
    outb(0x64, 0xD4);       /* Tell controller: next byte goes to mouse */
    mouse_wait_write();
    outb(0x60, cmd);
    mouse_wait_read();
    (void)inb(0x60);        /* Read ACK (0xFA) */
}

/* ── IRQ12 handler ────────────────────────────────────────────────── */

void mouse_irq_handler(struct registers *r) {
    (void)r;
    static uint8_t packet[4];
    static int pkt_idx = 0;
    int pkt_size = has_scroll_wheel ? 4 : 3;

    uint8_t status = inb(0x64);
    if (!(status & 0x20)) return; /* Not from aux device */

    uint8_t data = inb(0x60);

    /* Byte 0 must have bit 3 set (always-1 bit in PS/2 protocol) */
    if (pkt_idx == 0 && !(data & 0x08)) return;

    packet[pkt_idx++] = data;

    if (pkt_idx == pkt_size) {
        pkt_idx = 0;

        /* Discard packets with overflow bits */
        if (packet[0] & 0xC0) return;

        mouse.prev_buttons = mouse.buttons;
        mouse.buttons = packet[0] & 0x07;

        int16_t dx = (int16_t)packet[1];
        int16_t dy = (int16_t)packet[2];

        /* Sign-extend using bits 4 and 5 of byte 0 */
        if (packet[0] & 0x10) dx = (int16_t)(dx | (int16_t)0xFF00);
        if (packet[0] & 0x20) dy = (int16_t)(dy | (int16_t)0xFF00);

        mouse.x = (int16_t)CLAMP(mouse.x + dx, 0, VGA_GFX_WIDTH - 1);
        mouse.y = (int16_t)CLAMP(mouse.y - dy, 0, VGA_GFX_HEIGHT - 1);

        /* Accumulate scroll wheel delta from 4th byte (Intellimouse).
         * We add rather than overwrite so multiple scroll ticks
         * between desktop-loop iterations are not lost. */
        if (has_scroll_wheel && packet[3] != 0) {
            mouse.scroll_z += (int8_t)packet[3];
        }

        mouse.updated = true;
    }
}

/* ── Initialization ───────────────────────────────────────────────── */

static void mouse_set_sample_rate(uint8_t rate) {
    mouse_cmd(0xF3);           /* Set sample rate command */
    mouse_cmd(rate);           /* Desired rate */
}

static void mouse_enable_scroll_wheel(void) {
    /* Intellimouse magic sequence: set sample rate 200, 100, 80 */
    mouse_set_sample_rate(200);
    mouse_set_sample_rate(100);
    mouse_set_sample_rate(80);

    /* Read device ID — if it's 3, scroll wheel is active */
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, 0xF2);          /* Get device ID */
    mouse_wait_read();
    (void)inb(0x60);           /* ACK */
    mouse_wait_read();
    uint8_t id = inb(0x60);

    if (id == 3) {
        has_scroll_wheel = true;
    }
}

void mouse_init(void) {
    uint8_t status_byte;

    /* 1. Enable auxiliary PS/2 device */
    mouse_wait_write();
    outb(0x64, 0xA8);

    /* 2. Enable IRQ12 in the controller configuration byte */
    mouse_wait_write();
    outb(0x64, 0x20);         /* Read controller config */
    mouse_wait_read();
    status_byte = inb(0x60);
    status_byte |= 0x02;      /* Set bit 1 (aux interrupt enable) */
    status_byte &= (uint8_t)~0x20; /* Clear bit 5 (disable aux clock = 0) */
    mouse_wait_write();
    outb(0x64, 0x60);         /* Write controller config */
    mouse_wait_write();
    outb(0x60, status_byte);

    /* 3. Reset and configure mouse */
    mouse_cmd(0xFF);           /* Reset */
    mouse_wait_read();
    (void)inb(0x60);           /* Read extra bytes from reset */
    mouse_wait_read();
    (void)inb(0x60);

    mouse_cmd(0xF6);           /* Set defaults */

    /* Try to enable Intellimouse scroll wheel (4-byte packets) */
    mouse_enable_scroll_wheel();

    mouse_cmd(0xF4);           /* Enable data reporting */

    /* 4. Install IRQ12 handler and unmask */
    irq_install_handler(12, mouse_irq_handler);
    pic_clear_mask(12);

    KINFO("PS/2 mouse initialized");
}

/* ── Cursor drawing ───────────────────────────────────────────────── */

void mouse_save_under_cursor(void) {
    uint32_t *framebuf = vga_get_framebuffer();
    saved_x = mouse.x;
    saved_y = mouse.y;

    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            int16_t px = (int16_t)(saved_x + (int16_t)col);
            int16_t py = (int16_t)(saved_y + (int16_t)row);
            if (px >= 0 && px < VGA_GFX_WIDTH && py >= 0 && py < VGA_GFX_HEIGHT) {
                under_cursor[row * CURSOR_W + col] =
                    framebuf[(int32_t)py * VGA_GFX_WIDTH + (int32_t)px];
            }
        }
    }
}

void mouse_restore_under_cursor(void) {
    if (saved_x < 0) return;
    uint32_t *framebuf = vga_get_framebuffer();

    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            int16_t px = (int16_t)(saved_x + (int16_t)col);
            int16_t py = (int16_t)(saved_y + (int16_t)row);
            if (px >= 0 && px < VGA_GFX_WIDTH && py >= 0 && py < VGA_GFX_HEIGHT) {
                framebuf[(int32_t)py * VGA_GFX_WIDTH + (int32_t)px] =
                    under_cursor[row * CURSOR_W + col];
            }
        }
    }
}

void mouse_draw_cursor(void) {
    uint32_t *framebuf = vga_get_framebuffer();

    for (int row = 0; row < CURSOR_H; row++) {
        uint8_t outline = cursor_outline[row];
        uint8_t fill    = cursor_bitmap[row];
        for (int col = 0; col < CURSOR_W; col++) {
            int16_t px = (int16_t)(mouse.x + (int16_t)col);
            int16_t py = (int16_t)(mouse.y + (int16_t)row);
            if (px >= 0 && px < VGA_GFX_WIDTH && py >= 0 && py < VGA_GFX_HEIGHT) {
                uint8_t mask = (uint8_t)(0x80U >> (unsigned)col);
                if (fill & mask) {
                    framebuf[(int32_t)py * VGA_GFX_WIDTH + (int32_t)px] =
                        COLOR_CURSOR;
                } else if (outline & mask) {
                    framebuf[(int32_t)py * VGA_GFX_WIDTH + (int32_t)px] =
                        COLOR_BLACK;
                }
            }
        }
    }
}

void mouse_update_cursor_direct(void) {
    /* Skip if no cursor was ever saved (before first full render) */
    if (saved_x < 0) return;

    uint32_t *disp = vga_get_display_buffer();
    if (!disp) return;

    /* Erase old cursor by restoring saved pixels onto the display buffer */
    {
        int row, col;
        for (row = 0; row < CURSOR_H; row++) {
            for (col = 0; col < CURSOR_W; col++) {
                int16_t px = (int16_t)(saved_x + (int16_t)col);
                int16_t py = (int16_t)(saved_y + (int16_t)row);
                if (px >= 0 && px < VGA_GFX_WIDTH && py >= 0 && py < VGA_GFX_HEIGHT) {
                    disp[(int32_t)py * VGA_GFX_WIDTH + (int32_t)px] =
                        under_cursor[row * CURSOR_W + col];
                }
            }
        }
    }

    /* Save new cursor area and draw cursor at new position */
    saved_x = mouse.x;
    saved_y = mouse.y;
    {
        int row, col;
        for (row = 0; row < CURSOR_H; row++) {
            uint8_t outline = cursor_outline[row];
            uint8_t fill    = cursor_bitmap[row];
            for (col = 0; col < CURSOR_W; col++) {
                int16_t px = (int16_t)(saved_x + (int16_t)col);
                int16_t py = (int16_t)(saved_y + (int16_t)row);
                if (px >= 0 && px < VGA_GFX_WIDTH && py >= 0 && py < VGA_GFX_HEIGHT) {
                    uint8_t mask = (uint8_t)(0x80U >> (unsigned)col);
                    under_cursor[row * CURSOR_W + col] =
                        disp[(int32_t)py * VGA_GFX_WIDTH + (int32_t)px];
                    if (fill & mask) {
                        disp[(int32_t)py * VGA_GFX_WIDTH + (int32_t)px] = COLOR_CURSOR;
                    } else if (outline & mask) {
                        disp[(int32_t)py * VGA_GFX_WIDTH + (int32_t)px] = COLOR_BLACK;
                    }
                }
            }
        }
    }
}
