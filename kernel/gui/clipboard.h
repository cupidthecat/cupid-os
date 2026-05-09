/**
 * clipboard.h - Global clipboard for cupid-os
 *
 * Provides a system-wide clipboard for copy/cut/paste operations
 * shared between all applications (Notepad, Terminal, etc.).
 */

#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include "types.h"

#define CLIPBOARD_MAX_SIZE 8192

typedef struct {
    char data[CLIPBOARD_MAX_SIZE];
    int length;
    bool has_data;
} clipboard_t;

/* Initialize the global clipboard */
void clipboard_init(void);

/* Copy data into the clipboard */
void clipboard_copy(const char *data, int length);

/* Get clipboard contents (returns NULL if empty) */
const char *clipboard_get_data(void);

/* Get clipboard data length */
int clipboard_get_length(void);

/* Check if clipboard has data */
bool clipboard_has_data(void);

/* Clear clipboard contents */
void clipboard_clear(void);

#endif
