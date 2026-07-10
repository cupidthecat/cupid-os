/**
 * exec.c - Program loader for CupidOS
 *
 * Supports two binary formats:
 *   1. ELF32 - Static fixed-address i386 executables from CupidLD or the
 *              CupidC/CupidASM AOT writers
 *   2. CUPD  - CupidOS flat binary format (20-byte header + code + data)
 *
 * Format detection is automatic: the first 4 bytes determine which
 * loader is used.  ELF programs receive a pointer to the kernel
 * syscall table as their first argument to _start().
*/

#include "exec.h"
#include "kernel.h"
#include "vfs.h"
#include "process.h"
#include "memory.h"
#include "string.h"
#include "syscall.h"
#include "serial.h"

/* Maximum CUPD payload size. */
#define EXEC_MAX_SIZE (256u * 1024u)

/* Maximum number of ELF program headers we support */
#define ELF_MAX_PHDRS 16
#define ELF_VERSION_CURRENT 1u
#define ELF_PT_NULL 0u
#define ELF_PT_GNU_STACK 0x6474E551u
#define ELF_PF_X 0x1u
#define ELF_PF_W 0x2u
#define ELF_PF_R 0x4u
#define ELF_PF_KNOWN (ELF_PF_X | ELF_PF_W | ELF_PF_R)

typedef enum {
    ELF_IMAGE_EXTERNAL = 0,
    ELF_IMAGE_CUPIDC,
    ELF_IMAGE_CUPIDASM
} elf_image_kind_t;

typedef struct {
    uint32_t start;
    uint32_t end;
    elf_image_kind_t kind;
    const char *name;
} elf_image_region_t;

typedef struct {
    const elf_image_region_t *region;
    uint32_t min_vaddr;
    uint32_t max_vaddr;
    uint32_t page_base;
    uint32_t page_size;
    uint16_t load_count;
} elf_load_plan_t;

_Static_assert((EXTERNAL_EXEC_ARENA_START & (PAGE_SIZE - 1u)) == 0u &&
               (EXTERNAL_EXEC_ARENA_END & (PAGE_SIZE - 1u)) == 0u,
               "external executable arena must be page aligned");
_Static_assert((CUPIDC_EXEC_ARENA_START & (PAGE_SIZE - 1u)) == 0u &&
               (CUPIDC_EXEC_ARENA_END & (PAGE_SIZE - 1u)) == 0u,
               "CupidC executable arena must be page aligned");
_Static_assert((CUPIDASM_EXEC_ARENA_START & (PAGE_SIZE - 1u)) == 0u &&
               (CUPIDASM_EXEC_ARENA_END & (PAGE_SIZE - 1u)) == 0u,
               "CupidASM executable arena must be page aligned");

static const elf_image_region_t elf_image_regions[] = {
    {EXTERNAL_EXEC_ARENA_START, EXTERNAL_EXEC_ARENA_END,
     ELF_IMAGE_EXTERNAL, "external"},
    {CUPIDC_EXEC_ARENA_START, CUPIDC_EXEC_ARENA_END,
     ELF_IMAGE_CUPIDC, "CupidC"},
    {CUPIDASM_EXEC_ARENA_START, CUPIDASM_EXEC_ARENA_END,
     ELF_IMAGE_CUPIDASM, "CupidASM"}
};

static const elf_image_region_t *elf_image_region(uint32_t image_start,
                                                   uint32_t image_end,
                                                   uint32_t entry) {
    uint32_t count = (uint32_t)(sizeof(elf_image_regions) /
                                sizeof(elf_image_regions[0]));
    for (uint32_t i = 0; i < count; i++) {
        const elf_image_region_t *region = &elf_image_regions[i];
        if (image_start >= region->start && image_end <= region->end &&
            entry >= region->start && entry < region->end) {
            return region;
        }
    }
    return NULL;
}

static bool elf_range_within_file(uint32_t offset, uint32_t size,
                                  uint32_t file_size) {
    return offset <= file_size && size <= file_size - offset;
}

static bool elf_alignment_valid(uint32_t alignment) {
    return alignment == 0u || alignment == 1u ||
           (alignment & (alignment - 1u)) == 0u;
}

static bool elf_program_type_supported(uint32_t type) {
    return type == ELF_PT_NULL || type == ELF_PT_LOAD ||
           type == ELF_PT_GNU_STACK;
}

static int elf_read_exact(int fd, void *destination, uint32_t size) {
    uint8_t *bytes = (uint8_t *)destination;
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t chunk = size - offset;
        if (chunk > 512u) chunk = 512u;
        int read_count = vfs_read(fd, bytes + offset, chunk);
        if (read_count <= 0 || (uint32_t)read_count > chunk) {
            return VFS_EIO;
        }
        offset += (uint32_t)read_count;
    }
    return VFS_OK;
}

static int elf_plan_load(const char *path, const elf32_ehdr_t *header,
                         const elf32_phdr_t *programs, uint32_t file_size,
                         elf_load_plan_t *plan) {
    uint32_t min_vaddr = 0xFFFFFFFFu;
    uint32_t max_vaddr = 0u;
    uint16_t load_count = 0u;
    bool entry_executable = false;

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const elf32_phdr_t *program = &programs[i];
        if (!elf_program_type_supported(program->p_type)) {
            serial_printf("[elf] Unsupported phdr type 0x%x at %u in %s\n",
                          program->p_type, (uint32_t)i, path);
            return VFS_EINVAL;
        }
        if ((program->p_flags & ~ELF_PF_KNOWN) != 0u ||
            !elf_alignment_valid(program->p_align)) {
            serial_printf("[elf] Invalid phdr flags/alignment at %u in %s\n",
                          (uint32_t)i, path);
            return VFS_EINVAL;
        }
        if (program->p_type != ELF_PT_LOAD) {
            if (program->p_filesz != 0u || program->p_memsz != 0u) {
                serial_printf("[elf] Non-load phdr %u has payload in %s\n",
                              (uint32_t)i, path);
                return VFS_EINVAL;
            }
            continue;
        }
        if (program->p_align > 1u &&
            (program->p_offset & (program->p_align - 1u)) !=
                (program->p_vaddr & (program->p_align - 1u))) {
            serial_printf("[elf] PT_LOAD %u alignment is incongruent in %s\n",
                          (uint32_t)i, path);
            return VFS_EINVAL;
        }
        if (program->p_filesz > program->p_memsz ||
            program->p_vaddr > 0xFFFFFFFFu - program->p_memsz ||
            !elf_range_within_file(program->p_offset, program->p_filesz,
                                   file_size) ||
            (program->p_filesz > 0u && program->p_offset > 0x7FFFFFFFu)) {
            serial_printf("[elf] Invalid PT_LOAD %u ranges in %s\n",
                          (uint32_t)i, path);
            return VFS_EINVAL;
        }
        if (program->p_memsz == 0u) {
            continue;
        }

        uint32_t program_end = program->p_vaddr + program->p_memsz;
        for (uint16_t j = 0; j < i; j++) {
            const elf32_phdr_t *previous = &programs[j];
            if (previous->p_type != ELF_PT_LOAD || previous->p_memsz == 0u) {
                continue;
            }
            uint32_t previous_end = previous->p_vaddr + previous->p_memsz;
            if (program->p_vaddr < previous_end &&
                previous->p_vaddr < program_end) {
                serial_printf("[elf] PT_LOAD %u overlaps PT_LOAD %u in %s\n",
                              (uint32_t)i, (uint32_t)j, path);
                return VFS_EINVAL;
            }
        }

        load_count++;
        if (program->p_vaddr < min_vaddr) min_vaddr = program->p_vaddr;
        if (program_end > max_vaddr) max_vaddr = program_end;
        if ((program->p_flags & ELF_PF_X) != 0u &&
            header->e_entry >= program->p_vaddr &&
            header->e_entry - program->p_vaddr < program->p_filesz) {
            entry_executable = true;
        }
    }

    if (load_count == 0u || !entry_executable) {
        serial_printf("[elf] Missing loadable image or executable entry in %s\n",
                      path);
        return VFS_EINVAL;
    }

    const elf_image_region_t *region =
        elf_image_region(min_vaddr, max_vaddr, header->e_entry);
    if (!region) {
        serial_printf("[elf] Image 0x%x-0x%x entry=0x%x is outside an "
                      "executable arena in %s\n",
                      min_vaddr, max_vaddr, header->e_entry, path);
        return VFS_EINVAL;
    }

    uint32_t page_base = min_vaddr & ~(PAGE_SIZE - 1u);
    uint32_t page_end = (max_vaddr + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    uint32_t page_size = page_end - page_base;
    if (page_size == 0u || page_size > region->end - region->start) {
        serial_printf("[elf] Invalid staged image size in %s\n", path);
        return VFS_EINVAL;
    }

    plan->region = region;
    plan->min_vaddr = min_vaddr;
    plan->max_vaddr = max_vaddr;
    plan->page_base = page_base;
    plan->page_size = page_size;
    plan->load_count = load_count;
    return VFS_OK;
}

/* ELF32 Loader */

/**
 * elf_validate_header - Check that an ELF header is valid for CupidOS.
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

    if (hdr->e_ident[6] != ELF_VERSION_CURRENT ||
        hdr->e_version != ELF_VERSION_CURRENT) {
        serial_printf("[elf] Unsupported ELF version (%u/%u)\n",
                      (uint32_t)hdr->e_ident[6], hdr->e_version);
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

    if (hdr->e_ehsize != (uint16_t)sizeof(elf32_ehdr_t) ||
        hdr->e_phentsize != (uint16_t)sizeof(elf32_phdr_t)) {
        serial_printf("[elf] Unsupported ELF header sizes (%u/%u)\n",
                      (uint32_t)hdr->e_ehsize,
                      (uint32_t)hdr->e_phentsize);
        return VFS_EINVAL;
    }

    return 0;
}

int elf_exec(const char *path, const char *proc_name) {
    if (!path) return VFS_EINVAL;

    process_image_t image;
    memset(&image, 0, sizeof(image));

    vfs_stat_t file_info;
    int status = vfs_stat(path, &file_info);
    if (status < 0) {
        serial_printf("[elf] Cannot stat %s (err=%d)\n", path, status);
        return status;
    }
    if (file_info.type != VFS_TYPE_FILE ||
        file_info.size < (uint32_t)sizeof(elf32_ehdr_t)) {
        serial_printf("[elf] Invalid executable file size for %s\n", path);
        return VFS_EINVAL;
    }

    /* Open the file */
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        serial_printf("[elf] Cannot open %s (err=%d)\n", path, fd);
        return fd;
    }

    /* Read the ELF header */
    elf32_ehdr_t ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    status = elf_read_exact(fd, &ehdr, (uint32_t)sizeof(ehdr));
    if (status != VFS_OK) {
        vfs_close(fd);
        serial_printf("[elf] Failed to read ELF header from %s\n", path);
        return status;
    }

    /* Validate */
    if (elf_validate_header(&ehdr) != 0) {
        vfs_close(fd);
        return VFS_EINVAL;
    }

    /* Read all program headers */
    elf32_phdr_t phdrs[ELF_MAX_PHDRS];
    memset(phdrs, 0, sizeof(phdrs));

    uint32_t phdr_bytes = (uint32_t)ehdr.e_phnum *
                          (uint32_t)sizeof(elf32_phdr_t);
    if (ehdr.e_phoff < (uint32_t)sizeof(elf32_ehdr_t) ||
        ehdr.e_phoff > 0x7FFFFFFFu ||
        !elf_range_within_file(ehdr.e_phoff, phdr_bytes, file_info.size) ||
        vfs_seek(fd, (int32_t)ehdr.e_phoff, SEEK_SET) < 0) {
        vfs_close(fd);
        serial_printf("[elf] Invalid program-header offset in %s\n", path);
        return VFS_EINVAL;
    }
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        status = elf_read_exact(fd, &phdrs[i],
                                (uint32_t)sizeof(elf32_phdr_t));
        if (status != VFS_OK) {
            vfs_close(fd);
            serial_printf("[elf] Failed to read phdr %u\n", (uint32_t)i);
            return status;
        }
    }

    elf_load_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    status = elf_plan_load(path, &ehdr, phdrs, file_info.size, &plan);
    if (status != VFS_OK) {
        vfs_close(fd);
        return status;
    }

    uint32_t total_size = plan.max_vaddr - plan.min_vaddr;
    serial_printf("[elf] %s: %u PT_LOAD segments, %s arena "
                  "0x%x-0x%x (%u bytes)\n",
                  path, (uint32_t)plan.load_count, plan.region->name,
                  plan.min_vaddr, plan.max_vaddr, total_size);

    /* Stage the complete page span before touching a permanent executable
     * arena.  Exact reads remain authoritative if path metadata races. */
    uint8_t *staging = (uint8_t *)kmalloc(plan.page_size);
    if (!staging) {
        vfs_close(fd);
        serial_printf("[elf] Cannot stage %u bytes for %s\n",
                      plan.page_size, path);
        return VFS_ENOSPC;
    }
    memset(staging, 0, plan.page_size);

    int load_error = VFS_OK;
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != ELF_PT_LOAD) continue;
        if (phdrs[i].p_memsz == 0) continue;

        if (phdrs[i].p_filesz > 0u) {
            if (vfs_seek(fd, (int32_t)phdrs[i].p_offset, SEEK_SET) < 0) {
                load_error = VFS_EIO;
                serial_printf("[elf] Failed to seek to phdr %u in %s\n",
                              (uint32_t)i, path);
                break;
            }
            uint32_t stage_offset = phdrs[i].p_vaddr - plan.page_base;
            if (stage_offset > plan.page_size ||
                phdrs[i].p_filesz > plan.page_size - stage_offset) {
                load_error = VFS_EINVAL;
                serial_printf("[elf] PT_LOAD %u exceeds staging in %s\n",
                              (uint32_t)i, path);
                break;
            }
            load_error = elf_read_exact(fd, staging + stage_offset,
                                        phdrs[i].p_filesz);
            if (load_error != VFS_OK) {
                serial_printf("[elf] Short read in phdr %u from %s\n",
                              (uint32_t)i, path);
                break;
            }
        }
    }

    int close_status = vfs_close(fd);
    if (load_error != VFS_OK) {
        kfree(staging);
        return load_error;
    }
    if (close_status < 0) {
        kfree(staging);
        return close_status;
    }

    if (plan.region->kind == ELF_IMAGE_EXTERNAL) {
        if (!process_external_image_claim(&image)) {
            kfree(staging);
            serial_printf("[elf] External executable arena is busy for %s\n",
                          path);
            return VFS_ENOSPC;
        }
    } else {
        image.base = plan.page_base;
        image.size = plan.page_size;
        image.ownership = PROCESS_IMAGE_PERMANENT;
        image.lease_generation = 0u;
    }

    memcpy((void *)plan.page_base, staging, plan.page_size);
    kfree(staging);

    /* The entry point is the ELF's e_entry - since we loaded at the
     * exact virtual addresses the ELF expects, we use it directly.*/
    uint32_t entry_addr = ehdr.e_entry;

    /* Create the entry function pointer */
    void (*entry_fn)(void);
    memcpy(&entry_fn, &entry_addr, sizeof(entry_fn));

    /* Use provided name, or fallback to path */
    const char *pname = proc_name ? proc_name : path;

    /* Create the process and atomically transfer image ownership while
     * publishing the syscall-table argument on its initial stack. */
    uint32_t pid = process_create_with_arg_image_ex(
        entry_fn, pname, DEFAULT_STACK_SIZE,
        (uint32_t)syscall_get_table(), PROCESS_DOMAIN_EXTERNAL, &image
    );

    if (pid == 0) {
        process_image_discard(&image);
        serial_printf("[elf] Failed to create process for %s\n", path);
        return VFS_EIO;
    }

    serial_printf("[elf] Loaded %s as PID %u (ELF32, %u bytes at 0x%x)\n",
                  path, pid, total_size, plan.min_vaddr);

    /* Yield immediately so the new process gets a time slice
     * without waiting for the next timer tick.*/
    process_yield();

    return (int)pid;
}

/* CUPD Flat Binary Loader (original format) */

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

    /* Validate sizes - bound each field first so the sum below cannot wrap u32. */
    if (hdr.code_size > EXEC_MAX_SIZE ||
        hdr.data_size > EXEC_MAX_SIZE ||
        hdr.bss_size  > EXEC_MAX_SIZE) {
        vfs_close(fd);
        serial_printf("[cupd] Section size out of range in %s\n", path);
        return VFS_EINVAL;
    }
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
    uint32_t pid = process_create_ex(entry, pname, DEFAULT_STACK_SIZE,
                                     PROCESS_DOMAIN_EXTERNAL);
    if (pid == 0) {
        kfree(image);
        serial_printf("[cupd] Failed to create process for %s\n", path);
        return VFS_EIO;
    }

    serial_printf("[cupd] Loaded %s as PID %u (CUPD, %u bytes)\n",
                  path, pid, total);

    return (int)pid;
}

/* exec() - Auto-detecting loader (ELF or CUPD) */

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
