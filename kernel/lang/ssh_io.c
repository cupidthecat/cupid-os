/* Terminal I/O bridge for the cupidc SSH client. */

#include "ssh_io.h"
#include "shell.h"
#include "process.h"
#include "kernel.h"
#include "terminal_app.h"
#include "p256.h"
#include "ecdsa.h"

/* Same scancodes as drivers/keyboard.c. */
#define KSC_UP      0x48
#define KSC_DOWN    0x50
#define KSC_LEFT    0x4B
#define KSC_RIGHT   0x4D
#define KSC_HOME    0x47
#define KSC_END     0x4F
#define KSC_PGUP    0x49
#define KSC_PGDN    0x51
#define KSC_INS     0x52
#define KSC_DEL     0x53
#define KSC_F1      0x3B
#define KSC_F10     0x44
#define KSC_F11     0x57
#define KSC_F12     0x58

static void put_one(char c) {
    shell_gui_putchar_ext(c);
}

static int do_read_line(char *buf, uint32_t cap, int echo) {
    uint32_t n = 0;
    key_event_t ev;

    if (cap == 0) return 0;

    while (1) {
        char ch;
        if (shell_get_output_mode() == SHELL_OUTPUT_GUI) {
            ch = shell_jit_program_getchar();
            if (ch == 0) {
                put_one('\n');
                buf[0] = 0;
                return -1;
            }
        } else {
            if (!keyboard_read_event(&ev)) { schedule(); continue; }
            if (!ev.pressed) continue;
            ch = ev.character;
        }
        if (ch == 0) continue;
        if (ch == '\n' || ch == '\r') {
            put_one('\n');
            buf[n] = 0;
            return (int)n;
        }
        if (ch == 8 || ch == 127) {
            if (n > 0) {
                n--;
                if (echo) { put_one(8); put_one(' '); put_one(8); }
            }
            continue;
        }
        if (ch == 3) { /* Ctrl-C */
            put_one('\n');
            if (cap > 0) buf[0] = 0;
            return -1;
        }
        if (ch < 32) continue; /* drop other control chars */
        if (n + 1 < cap) {
            buf[n++] = ch;
            if (echo == 1) put_one(ch);
            else if (echo == 2) put_one('*');
        }
    }
}

int ssh_read_line(char *buf, uint32_t cap) {
    return do_read_line(buf, cap, 1);
}

int ssh_read_password(char *buf, uint32_t cap) {
    /* echo=0 to avoid leaking length via asterisks. OpenSSH-style. */
    return do_read_line(buf, cap, 0);
}

int ssh_key_event_to_vt(const key_event_t *ev, char out[8]) {
    if (!ev || !ev->pressed) return 0;
    char ch = ev->character;
    if (ch != 0) {
        /* A Linux PTY treats LF as the canonical line delimiter. Sending LF
         * avoids depending on the remote ICRNL mode being applied correctly. */
        if (ch == '\n') { out[0] = '\n'; return 1; }
        out[0] = ch;
        return 1;
    }
    switch (ev->scancode) {
        case KSC_UP:    out[0]=0x1b; out[1]='['; out[2]='A'; return 3;
        case KSC_DOWN:  out[0]=0x1b; out[1]='['; out[2]='B'; return 3;
        case KSC_RIGHT: out[0]=0x1b; out[1]='['; out[2]='C'; return 3;
        case KSC_LEFT:  out[0]=0x1b; out[1]='['; out[2]='D'; return 3;
        case KSC_HOME:  out[0]=0x1b; out[1]='['; out[2]='H'; return 3;
        case KSC_END:   out[0]=0x1b; out[1]='['; out[2]='F'; return 3;
        case KSC_PGUP:  out[0]=0x1b; out[1]='['; out[2]='5'; out[3]='~'; return 4;
        case KSC_PGDN:  out[0]=0x1b; out[1]='['; out[2]='6'; out[3]='~'; return 4;
        case KSC_INS:   out[0]=0x1b; out[1]='['; out[2]='2'; out[3]='~'; return 4;
        case KSC_DEL:   out[0]=0x1b; out[1]='['; out[2]='3'; out[3]='~'; return 4;
        case KSC_F1:    out[0]=0x1b; out[1]='O'; out[2]='P'; return 3;
        case KSC_F1+1:  out[0]=0x1b; out[1]='O'; out[2]='Q'; return 3;
        case KSC_F1+2:  out[0]=0x1b; out[1]='O'; out[2]='R'; return 3;
        case KSC_F1+3:  out[0]=0x1b; out[1]='O'; out[2]='S'; return 3;
        case KSC_F1+4:  out[0]=0x1b; out[1]='['; out[2]='1'; out[3]='5'; out[4]='~'; return 5;
        case KSC_F1+5:  out[0]=0x1b; out[1]='['; out[2]='1'; out[3]='7'; out[4]='~'; return 5;
        case KSC_F1+6:  out[0]=0x1b; out[1]='['; out[2]='1'; out[3]='8'; out[4]='~'; return 5;
        case KSC_F1+7:  out[0]=0x1b; out[1]='['; out[2]='1'; out[3]='9'; out[4]='~'; return 5;
        case KSC_F1+8:  out[0]=0x1b; out[1]='['; out[2]='2'; out[3]='0'; out[4]='~'; return 5;
        case KSC_F10:   out[0]=0x1b; out[1]='['; out[2]='2'; out[3]='1'; out[4]='~'; return 5;
        case KSC_F11:   out[0]=0x1b; out[1]='['; out[2]='2'; out[3]='3'; out[4]='~'; return 5;
        case KSC_F12:   out[0]=0x1b; out[1]='['; out[2]='2'; out[3]='4'; out[4]='~'; return 5;
        default: return 0;
    }
}

void ssh_print_n(const char *buf, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) shell_gui_putchar_ext(buf[i]);
}

void ssh_get_screen_size(int *cols, int *rows) {
    if (shell_get_output_mode() == SHELL_OUTPUT_GUI) {
        terminal_get_size(cols, rows);
        return;
    }
    if (cols) *cols = VGA_WIDTH;
    if (rows) *rows = VGA_HEIGHT;
}

int ssh_poll_key_vt(char out[8]) {
    if (shell_get_output_mode() == SHELL_OUTPUT_GUI) {
        char ch = shell_jit_program_pollchar();
        if (ch == 0) return 0;
        if (ch == '\n') { out[0] = '\n'; return 1; }
        out[0] = ch;
        return 1;
    }

    key_event_t ev;
    if (!keyboard_read_event(&ev)) return 0;
    return ssh_key_event_to_vt(&ev, out);
}

int ssh_ecdsa_p256_verify_blob(const uint8_t pub65[65],
                               const uint8_t *hash, uint32_t hash_len,
                               const uint8_t *r_be, uint32_t r_len,
                               const uint8_t *s_be, uint32_t s_len) {
    p256_aff_t pub;
    if (p256_pub_from_uncompressed(&pub, pub65, 65) != 0) return -1;
    return ecdsa_p256_verify(&pub, hash, hash_len, r_be, r_len, s_be, s_len);
}
