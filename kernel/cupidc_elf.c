/**
 * cupidc_elf.c — ELF32 binary writer for the CupidC compiler
 *
 * Writes compiled CupidC code as a standard ELF32 executable that
 * can be loaded by the existing CupidOS ELF loader.
 *
 * The output ELF has:
 *   - ELF header (52 bytes)
 *   - Two program headers (PT_LOAD for code, PT_LOAD for data)
 *   - Code section loaded at 0x00400000
 *   - Data section loaded after code
 */

#include "cupidc.h"
#include "exec.h"
#include "vfs.h"
#include "string.h"
#include "kernel.h"
#include "../drivers/serial.h"

/* ELF load address — must be >= 0x00400000 per exec.c */
#define ELF_LOAD_ADDR    CC_AOT_CODE_BASE
#define ELF_DATA_ADDR    CC_AOT_DATA_BASE

/* Page alignment */
#define ELF_PAGE_ALIGN   0x1000u

int cc_write_elf(cc_state_t *cc, const char *path) {
    if (!path || cc->code_pos == 0) return -1;

    /*
     * Layout:
     *   Offset 0x00: ELF header (52 bytes)
     *   Offset 0x34: Program header 1 — code (32 bytes)
     *   Offset 0x54: Program header 2 — data (32 bytes)
     *   Offset 0x74: padding to 0x80
     *   Offset 0x80: code section
     *   Offset 0x80 + code_size (aligned): data section
     */

    uint32_t headers_size = 52 + 32 * 2;  /* ehdr + 2 phdrs = 116 */
    uint32_t code_offset = 0x80;           /* file offset of code */
    uint32_t code_size = cc->code_pos;
    uint32_t data_offset = code_offset + code_size;
    /* Align data offset to 4 bytes */
    data_offset = (data_offset + 3) & ~3u;
    uint32_t data_size = cc->data_pos;
    uint32_t total_file_size = data_offset + data_size;

    /* Virtual addresses */
    uint32_t code_vaddr = ELF_LOAD_ADDR;
    uint32_t data_vaddr = ELF_DATA_ADDR;

    /* Entry point virtual address */
    uint32_t entry_vaddr = code_vaddr + cc->entry_offset;

    /* Build ELF header */
    elf32_ehdr_t ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELF_MAGIC_0;
    ehdr.e_ident[1] = ELF_MAGIC_1;
    ehdr.e_ident[2] = ELF_MAGIC_2;
    ehdr.e_ident[3] = ELF_MAGIC_3;
    ehdr.e_ident[4] = ELF_CLASS_32;      /* 32-bit */
    ehdr.e_ident[5] = ELF_DATA_LSB;      /* little-endian */
    ehdr.e_ident[6] = 1;                 /* EV_CURRENT */
    ehdr.e_type      = ELF_TYPE_EXEC;
    ehdr.e_machine   = ELF_MACHINE_386;
    ehdr.e_version   = 1;
    ehdr.e_entry     = entry_vaddr;
    ehdr.e_phoff     = 52;               /* immediately after ehdr */
    ehdr.e_ehsize    = 52;
    ehdr.e_phentsize = 32;
    ehdr.e_phnum     = (data_size > 0) ? 2 : 1;

    /* Build program headers */
    elf32_phdr_t phdr_code;
    memset(&phdr_code, 0, sizeof(phdr_code));
    phdr_code.p_type   = ELF_PT_LOAD;
    phdr_code.p_offset = code_offset;
    phdr_code.p_vaddr  = code_vaddr;
    phdr_code.p_paddr  = code_vaddr;
    phdr_code.p_filesz = code_size;
    phdr_code.p_memsz  = code_size;
    phdr_code.p_flags  = 0x5;            /* PF_R | PF_X */
    phdr_code.p_align  = 0x4;

    elf32_phdr_t phdr_data;
    memset(&phdr_data, 0, sizeof(phdr_data));
    phdr_data.p_type   = ELF_PT_LOAD;
    phdr_data.p_offset = data_offset;
    phdr_data.p_vaddr  = data_vaddr;
    phdr_data.p_paddr  = data_vaddr;
    phdr_data.p_filesz = data_size;
    phdr_data.p_memsz  = data_size;
    phdr_data.p_flags  = 0x6;            /* PF_R | PF_W */
    phdr_data.p_align  = 0x4;

    /* Write the file */
    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        serial_printf("[cupidc] Cannot create output file: %s (err=%d)\n",
                      path, fd);
        return fd;
    }

    /* Write ELF header */
    vfs_write(fd, &ehdr, sizeof(ehdr));

    /* Write program headers */
    vfs_write(fd, &phdr_code, sizeof(phdr_code));
    if (data_size > 0) {
        vfs_write(fd, &phdr_data, sizeof(phdr_data));
    }

    /* Pad to code_offset */
    uint32_t current = headers_size;
    while (current < code_offset) {
        uint8_t zero = 0;
        vfs_write(fd, &zero, 1);
        current++;
    }

    /* Write code section */
    vfs_write(fd, cc->code, code_size);
    current += code_size;

    /* Pad to data_offset */
    while (current < data_offset) {
        uint8_t zero = 0;
        vfs_write(fd, &zero, 1);
        current++;
    }

    /* Write data section */
    if (data_size > 0) {
        vfs_write(fd, cc->data, data_size);
    }

    vfs_close(fd);

    serial_printf("[cupidc] Wrote ELF: %s (%u bytes code, %u bytes data, "
                  "entry=0x%x, total=%u bytes)\n",
                  path, code_size, data_size, entry_vaddr, total_file_size);

    return 0;
}
