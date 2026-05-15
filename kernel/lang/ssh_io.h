#ifndef CUPID_SSH_IO_H
#define CUPID_SSH_IO_H

#include "types.h"
#include "keyboard.h"

/* Helpers exposed to cupidc programs for terminal I/O during SSH
 * sessions. Live in kernel/lang because they bridge keyboard/VGA into
 * a single-call surface that cupidc bindings can target. */

/* Read a line from the keyboard, echoing each byte to the screen.
 * Stops on Enter, returns chars written (excl. NUL). On Ctrl-C
 * returns -1 and leaves buf empty (with a trailing newline echoed). */
int ssh_read_line(char *buf, uint32_t cap);

/* Same as ssh_read_line but echoes '*' instead of the typed char. */
int ssh_read_password(char *buf, uint32_t cap);

/* Translate a key_event_t (one returned by keyboard_read_event) into
 * the byte sequence a VT100/xterm terminal would emit. Writes at most
 * 8 bytes to out, returns the count. Returns 0 if the event has no
 * VT mapping (e.g. modifier keys, release events). */
int ssh_key_event_to_vt(const key_event_t *ev, char out[8]);

/* Non-blocking: if a keyboard event is queued, translate it to its
 * VT100/xterm byte sequence and write up to 8 bytes into out. Returns
 * the count written (1..8) or 0 if no key is queued / no VT mapping.
 * Combines keyboard_read_event + ssh_key_event_to_vt so cupidc clients
 * don't need to declare the key_event_t struct. */
int ssh_poll_key_vt(char out[8]);

/* Bulk byte sink — pushes len bytes through shell_gui_putchar_ext. */
void ssh_print_n(const char *buf, uint32_t len);

/* Current text-mode screen geometry. */
void ssh_get_screen_size(int *cols, int *rows);

/* Wrapper around ecdsa_p256_verify that takes the SEC1 uncompressed
 * public-key blob (0x04 || X || Y, 65 bytes) instead of a parsed
 * p256_aff_t — easier to use from cupidc, which lacks struct
 * initializers for kernel types. Returns 0 on valid, -1 otherwise. */
int ssh_ecdsa_p256_verify_blob(const uint8_t pub65[65],
                               const uint8_t *hash, uint32_t hash_len,
                               const uint8_t *r_be, uint32_t r_len,
                               const uint8_t *s_be, uint32_t s_len);

#endif
