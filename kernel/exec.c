/**
 * exec.c — Program loader for CupidOS
 *
 * Loads CUPD flat binary executables from the VFS filesystem.
 * Format:  cupd_header_t (20 bytes) + code + data
 * At runtime: code, data, BSS (zeroed) are laid out contiguously in
 * a single kmalloc'd block.  A kernel process is then created with
 * the entry point set to header.entry_offset within the code region.
 */

#include "exec.h"
#include "vfs.h"
#include "process.h"
#include "memory.h"
#include "string.h"
#include "../drivers/serial.h"

/* Maximum executable size (256 KB) */
#define EXEC_MAX_SIZE (256u * 1024u)

int exec(const char *path, const char *name) {
    if (!path) return VFS_EINVAL;

    /* Open the file */
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return fd;

    /* Read the header */
    cupd_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    int r = vfs_read(fd, &hdr, sizeof(hdr));
    if (r < (int)sizeof(hdr)) {
        vfs_close(fd);
        serial_printf("[exec] Failed to read header from %s\n", path);
        return VFS_EIO;
    }

    /* Validate magic */
    if (hdr.magic != CUPD_MAGIC) {
        vfs_close(fd);
        serial_printf("[exec] Bad magic in %s\n", path);
        return VFS_EINVAL;
    }

    /* Validate sizes */
    uint32_t total = hdr.code_size + hdr.data_size + hdr.bss_size;
    if (total == 0 || total > EXEC_MAX_SIZE) {
        vfs_close(fd);
        serial_printf("[exec] Invalid sizes in %s\n", path);
        return VFS_EINVAL;
    }

    if (hdr.entry_offset >= hdr.code_size) {
        vfs_close(fd);
        serial_printf("[exec] Entry offset out of range in %s\n", path);
        return VFS_EINVAL;
    }

    /* Allocate memory for the entire image (code + data + bss) */
    uint8_t *image = kmalloc(total);
    if (!image) {
        vfs_close(fd);
        serial_printf("[exec] Out of memory for %s\n", path);
        return VFS_ENOSPC;
    }

    /* Read code section */
    uint32_t bytes_read = 0;
    while (bytes_read < hdr.code_size) {
        uint32_t chunk = hdr.code_size - bytes_read;
        if (chunk > 512) chunk = 512;
        r = vfs_read(fd, image + bytes_read, chunk);
        if (r <= 0) break;
        bytes_read += (uint32_t)r;
    }

    /* Read data section */
    uint32_t data_read = 0;
    while (data_read < hdr.data_size) {
        uint32_t chunk = hdr.data_size - data_read;
        if (chunk > 512) chunk = 512;
        r = vfs_read(fd, image + hdr.code_size + data_read, chunk);
        if (r <= 0) break;
        data_read += (uint32_t)r;
    }

    vfs_close(fd);

    /* Zero out BSS section */
    if (hdr.bss_size > 0) {
        memset(image + hdr.code_size + hdr.data_size, 0, hdr.bss_size);
    }

    /* Compute entry point address (use memcpy to avoid
     * ISO C object-to-function-pointer cast warning) */
    void (*entry)(void);
    {
        uint32_t addr = (uint32_t)(void *)image + hdr.entry_offset;
        memcpy(&entry, &addr, sizeof(entry));
    }

    /* Use provided name, or fallback to path */
    const char *proc_name = name ? name : path;

    /* Create the process */
    uint32_t pid = process_create(entry, proc_name, DEFAULT_STACK_SIZE);
    if (pid == 0) {
        kfree(image);
        serial_printf("[exec] Failed to create process for %s\n", path);
        return VFS_EIO;
    }

    serial_printf("[exec] Loaded %s as PID %u\n", path, pid);

    return (int)pid;
}
