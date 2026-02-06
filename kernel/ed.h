/**
 * ed.h - Ed line editor for cupid-os
 *
 * A faithful implementation of the classic Unix ed(1) line editor.
 * Operates on an in-memory text buffer with standard ed commands.
 */

#ifndef ED_H
#define ED_H

#include "types.h"

/* Launch ed, optionally opening a file from the in-memory filesystem.
 * Pass NULL or "" for a new empty buffer. */
void ed_run(const char *filename);

/* Set output functions for ed (for GUI mode support) */
void ed_set_output(void (*print_fn)(const char*), void (*putchar_fn)(char), void (*print_int_fn)(uint32_t));

#endif
