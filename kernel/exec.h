/**
 * exec.h — Program loader for CupidOS
 *
 * Loads and runs CUPD flat binary executables from the VFS.
 * Binary format: 20-byte header + code section + data section.
 */

#ifndef EXEC_H
#define EXEC_H

#include "types.h"

/* ── CUPD binary magic number ─────────────────────────────────────── */
#define CUPD_MAGIC 0x43555044  /* "CUPD" in little-endian */

/* ── CUPD executable header (20 bytes) ────────────────────────────── */
typedef struct {
    uint32_t magic;        /* Must be CUPD_MAGIC              */
    uint32_t entry_offset; /* Entry point offset from code start */
    uint32_t code_size;    /* Size of code section in bytes    */
    uint32_t data_size;    /* Size of data section in bytes    */
    uint32_t bss_size;     /* Size of BSS (zeroed) in bytes    */
} __attribute__((packed)) cupd_header_t;

/**
 * exec - Load and execute a CUPD binary from the VFS.
 *
 * Opens the file at `path`, reads the header, validates it,
 * allocates memory, loads code + data, zeroes BSS, and creates
 * a new process to run the binary.
 *
 * @param path   VFS path to the executable (e.g. "/bin/hello")
 * @param name   Human-readable name for the process
 *
 * @return  New process PID on success, or negative VFS error code.
 */
int exec(const char *path, const char *name);

#endif /* EXEC_H */
