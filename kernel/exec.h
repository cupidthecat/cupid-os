/**
 * exec.h — Program loader for CupidOS
 *
 * Loads and runs CUPD flat binaries and ELF32 executables from the VFS.
 * Format detection is automatic based on magic bytes:
 *   - 0x7F 'E' 'L' 'F'  →  ELF32 loader
 *   - 0x43555044 (CUPD)  →  CUPD flat binary loader
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

/* ── ELF32 constants ──────────────────────────────────────────────── */
#define ELF_MAGIC_0     0x7F
#define ELF_MAGIC_1     'E'
#define ELF_MAGIC_2     'L'
#define ELF_MAGIC_3     'F'
#define ELF_CLASS_32    1
#define ELF_DATA_LSB    1
#define ELF_TYPE_EXEC   2
#define ELF_MACHINE_386 3
#define ELF_PT_LOAD     1

/* ── ELF32 header (52 bytes) ─────────────────────────────────────── */
typedef struct {
    uint8_t  e_ident[16];       /* Magic: 0x7F 'E' 'L' 'F' + class/endian */
    uint16_t e_type;            /* ET_EXEC (2) for executables      */
    uint16_t e_machine;         /* EM_386 (3) for i386              */
    uint32_t e_version;         /* Version (1)                      */
    uint32_t e_entry;           /* Entry point virtual address      */
    uint32_t e_phoff;           /* Program header table offset      */
    uint32_t e_shoff;           /* Section header table offset      */
    uint32_t e_flags;           /* Processor flags                  */
    uint16_t e_ehsize;          /* ELF header size (52)             */
    uint16_t e_phentsize;       /* Program header entry size (32)   */
    uint16_t e_phnum;           /* Number of program headers        */
    uint16_t e_shentsize;       /* Section header entry size        */
    uint16_t e_shnum;           /* Number of section headers        */
    uint16_t e_shstrndx;        /* String table section index       */
} __attribute__((packed)) elf32_ehdr_t;

/* ── ELF32 program header (32 bytes) ─────────────────────────────── */
typedef struct {
    uint32_t p_type;            /* PT_LOAD (1) for loadable segment */
    uint32_t p_offset;          /* Offset in file                   */
    uint32_t p_vaddr;           /* Virtual address to load at       */
    uint32_t p_paddr;           /* Physical address (ignored)       */
    uint32_t p_filesz;          /* Size in file                     */
    uint32_t p_memsz;           /* Size in memory (>= filesz)      */
    uint32_t p_flags;           /* PF_R | PF_W | PF_X              */
    uint32_t p_align;           /* Alignment (power of 2)           */
} __attribute__((packed)) elf32_phdr_t;

/* ── ELF32 section header (40 bytes) ─────────────────────────────── */
typedef struct {
    uint32_t sh_name;       /* Section name (index into string table)  */
    uint32_t sh_type;       /* Section type                             */
    uint32_t sh_flags;      /* Section flags                            */
    uint32_t sh_addr;       /* Section virtual address                  */
    uint32_t sh_offset;     /* Offset in file                           */
    uint32_t sh_size;       /* Section size in bytes                    */
    uint32_t sh_link;       /* Link to another section                  */
    uint32_t sh_info;       /* Additional information                   */
    uint32_t sh_addralign;  /* Address alignment                        */
    uint32_t sh_entsize;    /* Entry size (for tables)                  */
} __attribute__((packed)) elf32_shdr_t;

#define ELF_SHT_NULL    0   /* Null section                             */
#define ELF_SHT_SYMTAB  2   /* Symbol table section type                */
#define ELF_SHT_STRTAB  3   /* String table section type                */
#define ELF_STB_GLOBAL  1   /* Global binding (in st_info high nibble)  */
#define ELF_STT_FUNC    2   /* Function type (in st_info low nibble)    */

/* ── ELF32 symbol table entry (16 bytes) ─────────────────────────── */
typedef struct {
    uint32_t st_name;   /* Symbol name (index into string table)      */
    uint32_t st_value;  /* Symbol value (address for functions)        */
    uint32_t st_size;   /* Symbol size in bytes                        */
    uint8_t  st_info;   /* Symbol type and binding                     */
    uint8_t  st_other;  /* Symbol visibility                           */
    uint16_t st_shndx;  /* Section index                               */
} __attribute__((packed)) elf32_sym_t;

/**
 * exec — Load and execute a binary from the VFS.
 *
 * Detects format (ELF or CUPD), validates, allocates memory,
 * loads segments, and creates a new process.  For ELF binaries,
 * the kernel syscall table is passed to _start() as an argument.
 *
 * @param path   VFS path to the executable (e.g. "/home/hello")
 * @param name   Human-readable name for the process
 *
 * @return  New process PID on success, or negative VFS error code.
 */
int exec(const char *path, const char *name);

/**
 * elf_exec — Load and execute an ELF32 binary (internal).
 *
 * @param path       VFS path to ELF executable
 * @param proc_name  Process name for debugging
 *
 * @return  PID on success, negative VFS error on failure.
 */
int elf_exec(const char *path, const char *proc_name);

/**
 * cupd_exec — Load and execute a CUPD flat binary (internal).
 *
 * @param path       VFS path to CUPD executable
 * @param proc_name  Process name for debugging
 *
 * @return  PID on success, negative VFS error on failure.
 */
int cupd_exec(const char *path, const char *proc_name);

#endif /* EXEC_H */
