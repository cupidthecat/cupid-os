/**
 * clipboard.c - Global clipboard for cupid-os
 *
 * System-wide clipboard singleton that persists across application
 * lifetime.  Shared between Notepad, Terminal, and future apps.
 */

#include "clipboard.h"
#include "string.h"
#include "../drivers/serial.h"

static clipboard_t clipboard;

void clipboard_init(void) {
    memset(&clipboard, 0, sizeof(clipboard));
    clipboard.has_data = false;
    clipboard.length = 0;
    KINFO("Clipboard initialized (%d bytes max)", CLIPBOARD_MAX_SIZE);
}

void clipboard_copy(const char *data, int length) {
    if (!data || length <= 0) return;
    if (length > CLIPBOARD_MAX_SIZE - 1) {
        length = CLIPBOARD_MAX_SIZE - 1;
    }
    memcpy(clipboard.data, data, (size_t)length);
    clipboard.data[length] = '\0';
    clipboard.length = length;
    clipboard.has_data = true;
}

const char *clipboard_get_data(void) {
    if (!clipboard.has_data) return NULL;
    return clipboard.data;
}

int clipboard_get_length(void) {
    return clipboard.length;
}

bool clipboard_has_data(void) {
    return clipboard.has_data;
}

void clipboard_clear(void) {
    clipboard.has_data = false;
    clipboard.length = 0;
    clipboard.data[0] = '\0';
}
