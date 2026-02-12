/**
 * exec.c — Program loader for CupidOS
 *
 * Supports two binary formats:
 *   1. ELF32 — Standard i386 ELF executables (compiled with GCC/Clang)
 *   2. CUPD  — CupidOS flat binary format (20-byte header + code + data)
 *
 * Format detection is automatic: the first 4 bytes determine which
 * loader is used.  ELF programs receive a pointer to the kernel
 * syscall table as their first argument to _start().
 */

#include "exec.h"
#include "kernel.h"
#include "vfs.h"
#include "process.h"
#include "shell.h"
#include "memory.h"
#include "string.h"
#include "syscall.h"
#include "../drivers/serial.h"

/* Maximum executable size (1 MB) */
#define EXEC_MAX_SIZE (1024u * 1024u)
/* Self-hosted compiler binaries need more stack than tiny shell apps. */
#define EXEC_STACK_SIZE (DEFAULT_STACK_SIZE * 4u)

/* Maximum number of ELF program headers we support */
#define ELF_MAX_PHDRS 16

/* ══════════════════════════════════════════════════════════════════════
 *  ELF32 Loader
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * elf_validate_header — Check that an ELF header is valid for CupidOS.
 * Returns 0 on success, VFS_EINVAL on failure.
 */
static int elf_validate_header(const elf32_ehdr_t *hdr) {
    /* Check magic bytes */
    if (hdr->e_ident[0] != ELF_MAGIC_0 ||
        hdr->e_ident[1] != ELF_MAGIC_1 ||
        hdr->e_ident[2] != ELF_MAGIC_2 ||
        hdr->e_ident[3] != ELF_MAGIC_3) {
        serial_printf("[elf] Invalid ELF magic\n");
        return VFS_EINVAL;
    }

    /* Must be 32-bit */
    if (hdr->e_ident[4] != ELF_CLASS_32) {
        serial_printf("[elf] Not ELF32 (class=%u)\n",
                      (uint32_t)hdr->e_ident[4]);
        return VFS_EINVAL;
    }

    /* Must be little-endian */
    if (hdr->e_ident[5] != ELF_DATA_LSB) {
        serial_printf("[elf] Not little-endian (data=%u)\n",
                      (uint32_t)hdr->e_ident[5]);
        return VFS_EINVAL;
    }

    /* Must be executable */
    if (hdr->e_type != ELF_TYPE_EXEC) {
        serial_printf("[elf] Not ET_EXEC (type=%u)\n",
                      (uint32_t)hdr->e_type);
        return VFS_EINVAL;
    }

    /* Must be i386 */
    if (hdr->e_machine != ELF_MACHINE_386) {
        serial_printf("[elf] Not i386 (machine=%u)\n",
                      (uint32_t)hdr->e_machine);
        return VFS_EINVAL;
    }

    /* Must have program headers */
    if (hdr->e_phnum == 0) {
        serial_printf("[elf] No program headers\n");
        return VFS_EINVAL;
    }

    if (hdr->e_phnum > ELF_MAX_PHDRS) {
        serial_printf("[elf] Too many program headers (%u)\n",
                      (uint32_t)hdr->e_phnum);
        return VFS_EINVAL;
    }

    return 0;
}

int elf_exec(const char *path, const char *proc_name) {
    if (!path) return VFS_EINVAL;

    /* Open the file */
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        serial_printf("[elf] Cannot open %s (err=%d)\n", path, fd);
        return fd;
    }

    /* Read the ELF header */
    elf32_ehdr_t ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    int r = vfs_read(fd, &ehdr, sizeof(ehdr));
    if (r < (int)sizeof(ehdr)) {
        vfs_close(fd);
        serial_printf("[elf] Failed to read ELF header from %s\n", path);
        return VFS_EIO;
    }

    /* Validate */
    if (elf_validate_header(&ehdr) != 0) {
        vfs_close(fd);
        return VFS_EINVAL;
    }

    /* Read all program headers */
    elf32_phdr_t phdrs[ELF_MAX_PHDRS];
    memset(phdrs, 0, sizeof(phdrs));

    vfs_seek(fd, (int32_t)ehdr.e_phoff, SEEK_SET);
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        r = vfs_read(fd, &phdrs[i], sizeof(elf32_phdr_t));
        if (r < (int)sizeof(elf32_phdr_t)) {
            vfs_close(fd);
            serial_printf("[elf] Failed to read phdr %u\n", (uint32_t)i);
            return VFS_EIO;
        }
    }

    /* Calculate total memory needed (scan all PT_LOAD segments) */
    uint32_t min_vaddr = 0xFFFFFFFF;
    uint32_t max_vaddr = 0;
    int load_count = 0;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != ELF_PT_LOAD) continue;
        if (phdrs[i].p_memsz == 0) continue;

        load_count++;

        if (phdrs[i].p_vaddr < min_vaddr) {
            min_vaddr = phdrs[i].p_vaddr;
        }
        uint32_t seg_end = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        if (seg_end > max_vaddr) {
            max_vaddr = seg_end;
        }
    }

    if (load_count == 0) {
        vfs_close(fd);
        serial_printf("[elf] No PT_LOAD segments in %s\n", path);
        return VFS_EINVAL;
    }

    uint32_t total_size = max_vaddr - min_vaddr;
    if (total_size == 0 || total_size > EXEC_MAX_SIZE) {
        vfs_close(fd);
        serial_printf("[elf] Image too large (%u bytes) in %s\n",
                      total_size, path);
        return VFS_EINVAL;
    }

    serial_printf("[elf] %s: %d PT_LOAD segments, vaddr 0x%x-0x%x (%u bytes)\n",
                  path, load_count, min_vaddr, max_vaddr, total_size);

    /* Sanity check: the vaddr range must be within identity-mapped memory
     * and must not overlap with the kernel or critical regions.
     * We require vaddr >= 0x00400000 (4MB) to stay above kernel+heap+bss,
     * and the entire range must fit within the 32MB identity map.
     * (Kernel occupies 0x10000-~0x37000; user programs must be above that.) */
    if (min_vaddr < 0x00400000) {
        vfs_close(fd);
        serial_printf("[elf] Load address too low (0x%x) in %s — "
                      "relink with -Ttext=0x00400000\n",
                      min_vaddr, path);
        return VFS_EINVAL;
    }
    if (max_vaddr > IDENTITY_MAP_SIZE) {
        vfs_close(fd);
        serial_printf("[elf] Load address too high (0x%x > 0x%x) in %s\n",
                      max_vaddr, IDENTITY_MAP_SIZE, path);
        return VFS_EINVAL;
    }

    /* Page-align the region for PMM reservation */
    uint32_t page_base = min_vaddr & ~0xFFFu;
    uint32_t page_end  = (max_vaddr + 0xFFFu) & ~0xFFFu;
    uint32_t page_size = page_end - page_base;

    /* Reserve these physical pages so nothing else uses them */
    pmm_reserve_region(page_base, page_size);

    /* Zero the entire region first (covers BSS and alignment gaps) */
    memset((void *)page_base, 0, page_size);

    /* Load each PT_LOAD segment directly to its virtual address */
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != ELF_PT_LOAD) continue;
        if (phdrs[i].p_memsz == 0) continue;

        uint8_t *dest = (uint8_t *)phdrs[i].p_vaddr;

        /* Read file data for this segment */
        if (phdrs[i].p_filesz > 0) {
            vfs_seek(fd, (int32_t)phdrs[i].p_offset, SEEK_SET);

            uint32_t remaining = phdrs[i].p_filesz;
            uint32_t written = 0;
            while (remaining > 0) {
                uint32_t chunk = remaining;
                if (chunk > 512) chunk = 512;
                r = vfs_read(fd, dest + written, chunk);
                if (r <= 0) break;
                written += (uint32_t)r;
                remaining -= (uint32_t)r;
            }
        }

        /* BSS: the gap between filesz and memsz is already zeroed
         * because we memset the whole region to 0 above. */
    }

    vfs_close(fd);

    /* Verify load integrity by checking a few bytes at various offsets */
    {
        serial_printf("[elf] Verifying loaded code integrity for %s\n", path);
        uint8_t *check_addr = (uint8_t *)min_vaddr;
        serial_printf("[elf]   @0x%x: ", (uint32_t)check_addr);
        for (int i = 0; i < 8; i++) {
            serial_printf("%x ", check_addr[i]);
        }
        serial_printf("\n");
        if (total_size > 0x10000) {
            check_addr = (uint8_t *)(min_vaddr + 0x10000);
            serial_printf("[elf]   @0x%x: ", (uint32_t)check_addr);
            for (int i = 0; i < 8; i++) {
                serial_printf("%x ", check_addr[i]);
            }
            serial_printf("\n");
        }
        if (total_size > 0x2c89c) {
            check_addr = (uint8_t *)(min_vaddr + 0x2c89c); /* Crash offset */
            serial_printf("[elf]   @0x%x (crash site): ", (uint32_t)check_addr);
            for (int i = 0; i < 8; i++) {
                serial_printf("%x ", check_addr[i]);
            }
            serial_printf("\n");
        }
    }

    /* The entry point is the ELF's e_entry — since we loaded at the
     * exact virtual addresses the ELF expects, we use it directly. */
    uint32_t entry_addr = ehdr.e_entry;

    /* Create the entry function pointer */
    void (*entry_fn)(void);
    memcpy(&entry_fn, &entry_addr, sizeof(entry_fn));

    /* Use provided name, or fallback to path */
    const char *pname = proc_name ? proc_name : path;

    /* Create process with syscall table argument on the stack.
     * We use process_create_with_arg to push the arg before entry. */
    uint32_t pid = process_create_with_arg(
        entry_fn, pname, EXEC_STACK_SIZE,
        (uint32_t)syscall_get_table()
    );

    if (pid == 0) {
        pmm_release_region(page_base, page_size);
        serial_printf("[elf] Failed to create process for %s\n", path);
        return VFS_EIO;
    }

    /* Associate the image memory with the process so it gets
     * freed when the process exits. */
    process_set_image(pid, page_base, page_size);
    process_set_program_args(pid, shell_get_program_args());

    serial_printf("[elf] Loaded %s as PID %u (ELF32, %u bytes at 0x%x)\n",
                  path, pid, total_size, min_vaddr);

    /* Yield immediately so the new process gets a time slice
     * without waiting for the next timer tick. */
    process_yield();

    return (int)pid;
}

/* ══════════════════════════════════════════════════════════════════════
 *  CUPD Flat Binary Loader (original format)
 * ══════════════════════════════════════════════════════════════════════ */

int cupd_exec(const char *path, const char *proc_name) {
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
        serial_printf("[cupd] Failed to read header from %s\n", path);
        return VFS_EIO;
    }

    /* Validate magic */
    if (hdr.magic != CUPD_MAGIC) {
        vfs_close(fd);
        serial_printf("[cupd] Bad magic in %s\n", path);
        return VFS_EINVAL;
    }

    /* Validate sizes */
    uint32_t total = hdr.code_size + hdr.data_size + hdr.bss_size;
    if (total == 0 || total > EXEC_MAX_SIZE) {
        vfs_close(fd);
        serial_printf("[cupd] Invalid sizes in %s\n", path);
        return VFS_EINVAL;
    }

    if (hdr.entry_offset >= hdr.code_size) {
        vfs_close(fd);
        serial_printf("[cupd] Entry offset out of range in %s\n", path);
        return VFS_EINVAL;
    }

    /* Allocate memory for the entire image (code + data + bss) */
    uint8_t *image = kmalloc(total);
    if (!image) {
        vfs_close(fd);
        serial_printf("[cupd] Out of memory for %s\n", path);
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

    /* Compute entry point address */
    void (*entry)(void);
    {
        uint32_t addr = (uint32_t)(void *)image + hdr.entry_offset;
        memcpy(&entry, &addr, sizeof(entry));
    }

    /* Use provided name, or fallback to path */
    const char *pname = proc_name ? proc_name : path;

    /* Create the process */
    uint32_t pid = process_create(entry, pname, EXEC_STACK_SIZE);
    if (pid == 0) {
        kfree(image);
        serial_printf("[cupd] Failed to create process for %s\n", path);
        return VFS_EIO;
    }
    process_set_program_args(pid, shell_get_program_args());

    serial_printf("[cupd] Loaded %s as PID %u (CUPD, %u bytes)\n",
                  path, pid, total);

    return (int)pid;
}

/* ══════════════════════════════════════════════════════════════════════
 *  exec() — Auto-detecting loader (ELF or CUPD)
 * ══════════════════════════════════════════════════════════════════════ */

int exec(const char *path, const char *name) {
    if (!path) return VFS_EINVAL;

    /* Open the file and read the first 4 bytes to detect format */
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        serial_printf("[exec] Cannot open %s (err=%d)\n", path, fd);
        return fd;
    }

    uint8_t magic[4];
    memset(magic, 0, sizeof(magic));
    int r = vfs_read(fd, magic, 4);
    vfs_close(fd);

    if (r < 4) {
        serial_printf("[exec] File too small: %s\n", path);
        return VFS_EINVAL;
    }

    /* Check for ELF magic: 0x7F 'E' 'L' 'F' */
    if (magic[0] == ELF_MAGIC_0 &&
        magic[1] == ELF_MAGIC_1 &&
        magic[2] == ELF_MAGIC_2 &&
        magic[3] == ELF_MAGIC_3) {
        serial_printf("[exec] Detected ELF format: %s\n", path);
        return elf_exec(path, name);
    }

    /* Check for CUPD magic (little-endian 0x43555044) */
    uint32_t cupd_magic;
    memcpy(&cupd_magic, magic, 4);
    if (cupd_magic == CUPD_MAGIC) {
        serial_printf("[exec] Detected CUPD format: %s\n", path);
        return cupd_exec(path, name);
    }

    serial_printf("[exec] Unknown binary format in %s "
                  "(magic: %02x %02x %02x %02x)\n",
                  path, magic[0], magic[1], magic[2], magic[3]);
    return VFS_EINVAL;
}
