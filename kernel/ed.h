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

#endif
