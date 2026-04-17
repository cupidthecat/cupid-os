/* kernel/swap_disk.c - Swap backing file I/O. */

#include "swap_disk.h"
#include "vfs.h"
#include "../drivers/serial.h"

const uint32_t swap_disk_slot_bytes[SWAP_DISK_NUM_CLASSES] = {
    1024u, 4096u, 16384u, 65536u
};
const uint32_t swap_disk_slot_count[SWAP_DISK_NUM_CLASSES] = {
    4096u, 1024u, 256u, 64u
};
const uint32_t swap_disk_region_offset[SWAP_DISK_NUM_CLASSES] = {
    0x000000u, 0x400000u, 0x800000u, 0xC00000u
};

static int swap_fd = -1;

int swap_disk_open(const char *vfs_path) {
    if (!vfs_path) return VFS_EINVAL;
    if (swap_fd >= 0) return VFS_EACCES;  /* already open */

    int fd = vfs_open(vfs_path, O_RDWR | O_CREAT);
    if (fd < 0) {
        KERROR("swap_disk: vfs_open('%s') failed: %d", vfs_path, fd);
        return fd;
    }

    vfs_stat_t st;
    if (vfs_stat(vfs_path, &st) < 0) {
        vfs_close(fd);
        return VFS_EIO;
    }

    /* Zero-extend to SWAP_DISK_FILE_SIZE if shorter. */
    if (st.size < SWAP_DISK_FILE_SIZE) {
        uint8_t zeros[2048];
        for (uint32_t i = 0; i < sizeof(zeros); i++) zeros[i] = 0;

        if (vfs_seek(fd, (int32_t)st.size, SEEK_SET) < 0) {
            vfs_close(fd);
            return VFS_EIO;
        }
        uint32_t remaining = SWAP_DISK_FILE_SIZE - st.size;
        while (remaining > 0) {
            uint32_t chunk = remaining > sizeof(zeros) ? sizeof(zeros) : remaining;
            int n = vfs_write(fd, zeros, chunk);
            if (n != (int)chunk) {
                vfs_close(fd);
                KERROR("swap_disk: zero-extend write short: %d/%u", n, chunk);
                return VFS_ENOSPC;
            }
            remaining -= chunk;
        }
        KINFO("swap_disk: extended '%s' from %u -> %u bytes",
              vfs_path, st.size, SWAP_DISK_FILE_SIZE);
    }

    swap_fd = fd;
    KINFO("swap_disk: opened '%s' (fd=%d, 16 MB)", vfs_path, fd);
    return 0;
}

void swap_disk_close(void) {
    if (swap_fd >= 0) {
        vfs_close(swap_fd);
        swap_fd = -1;
    }
}

int swap_disk_read(uint8_t class_idx, uint32_t slot, void *buf) {
    if (class_idx >= SWAP_DISK_NUM_CLASSES) return VFS_EINVAL;
    if (slot >= swap_disk_slot_count[class_idx]) return VFS_EINVAL;
    if (!buf) return VFS_EINVAL;
    if (swap_fd < 0) return VFS_EIO;

    uint32_t off = swap_disk_region_offset[class_idx] +
                   slot * swap_disk_slot_bytes[class_idx];
    if (vfs_seek(swap_fd, (int32_t)off, SEEK_SET) < 0) return VFS_EIO;
    int n = vfs_read(swap_fd, buf, swap_disk_slot_bytes[class_idx]);
    return (n == (int)swap_disk_slot_bytes[class_idx]) ? 0 : VFS_EIO;
}

int swap_disk_write(uint8_t class_idx, uint32_t slot, const void *buf) {
    if (class_idx >= SWAP_DISK_NUM_CLASSES) return VFS_EINVAL;
    if (slot >= swap_disk_slot_count[class_idx]) return VFS_EINVAL;
    if (!buf) return VFS_EINVAL;
    if (swap_fd < 0) return VFS_EIO;

    uint32_t off = swap_disk_region_offset[class_idx] +
                   slot * swap_disk_slot_bytes[class_idx];
    if (vfs_seek(swap_fd, (int32_t)off, SEEK_SET) < 0) return VFS_EIO;
    int n = vfs_write(swap_fd, buf, swap_disk_slot_bytes[class_idx]);
    return (n == (int)swap_disk_slot_bytes[class_idx]) ? 0 : VFS_EIO;
}
