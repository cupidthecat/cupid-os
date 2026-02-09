#ifndef VFS_HELPERS_H
#define VFS_HELPERS_H

#include "types.h"

/* ══════════════════════════════════════════════════════════════════════
 *  vfs_helpers.h — High-level VFS convenience functions
 *
 *  Provides simple read/write-all operations so callers don't need
 *  to manually open/read-loop/close.  All functions return >= 0 on
 *  success or a negative VFS error code on failure.
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Read entire file into buffer.
 * @return bytes read on success, negative VFS error on failure
 */
int vfs_read_all(const char *path, void *buffer, uint32_t max_size);

/**
 * Write entire buffer to file (creates or truncates).
 * @return bytes written on success, negative VFS error on failure
 */
int vfs_write_all(const char *path, const void *buffer, uint32_t size);

/**
 * Read text file as null-terminated string (adds null terminator).
 * @return string length (excluding null) on success, negative on error
 */
int vfs_read_text(const char *path, char *buffer, uint32_t max_size);

/**
 * Write null-terminated string to file (creates or truncates).
 * @return bytes written (excluding null) on success, negative on error
 */
int vfs_write_text(const char *path, const char *text);

/**
 * Copy a file from src to dest.
 * @return bytes copied on success, negative VFS error on failure
 */
int vfs_copy_file(const char *src, const char *dest);

#endif /* VFS_HELPERS_H */
