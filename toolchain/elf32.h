#ifndef CUPID_TOOLCHAIN_ELF32_H
#define CUPID_TOOLCHAIN_ELF32_H

#include "ctool.h"

/* Semantic i386 ELF32 ET_REL interface.  Callers describe object content;
 * the module owns all file offsets, indices, derived tables, and padding. */

#define CTOOL_ELF32_NO_SECTION 0xffffffffu

typedef enum {
  CTOOL_ELF32_SHT_PROGBITS = 1,
  CTOOL_ELF32_SHT_NOBITS = 8
} ctool_elf32_section_type_t;

#define CTOOL_ELF32_SHF_WRITE 0x00000001u
#define CTOOL_ELF32_SHF_ALLOC 0x00000002u
#define CTOOL_ELF32_SHF_EXECINSTR 0x00000004u
#define CTOOL_ELF32_SHF_MERGE 0x00000010u
#define CTOOL_ELF32_SHF_STRINGS 0x00000020u
#define CTOOL_ELF32_SHF_TLS 0x00000400u
#define CTOOL_ELF32_SHF_EXCLUDE 0x80000000u

typedef struct {
  ctool_string_t name;
  ctool_elf32_section_type_t type;
  ctool_u32 flags;
  ctool_u32 alignment;
  ctool_u32 entry_size;
  ctool_u32 size;
  ctool_bytes_t contents;
} ctool_elf32_section_spec_t;

typedef enum {
  CTOOL_ELF32_BIND_LOCAL = 0,
  CTOOL_ELF32_BIND_GLOBAL = 1,
  CTOOL_ELF32_BIND_WEAK = 2
} ctool_elf32_symbol_binding_t;

typedef enum {
  CTOOL_ELF32_SYMBOL_NOTYPE = 0,
  CTOOL_ELF32_SYMBOL_OBJECT = 1,
  CTOOL_ELF32_SYMBOL_FUNCTION = 2,
  CTOOL_ELF32_SYMBOL_SECTION = 3,
  CTOOL_ELF32_SYMBOL_FILE = 4,
  CTOOL_ELF32_SYMBOL_COMMON = 5
} ctool_elf32_symbol_type_t;

typedef enum {
  CTOOL_ELF32_VIS_DEFAULT = 0,
  CTOOL_ELF32_VIS_INTERNAL = 1,
  CTOOL_ELF32_VIS_HIDDEN = 2,
  CTOOL_ELF32_VIS_PROTECTED = 3
} ctool_elf32_symbol_visibility_t;

typedef enum {
  CTOOL_ELF32_SYMBOL_UNDEFINED = 0,
  CTOOL_ELF32_SYMBOL_DEFINED,
  CTOOL_ELF32_SYMBOL_ABSOLUTE,
  CTOOL_ELF32_SYMBOL_COMMON_STORAGE,
  CTOOL_ELF32_SYMBOL_RESERVED
} ctool_elf32_symbol_placement_t;

typedef struct {
  ctool_string_t name;
  ctool_elf32_symbol_binding_t binding;
  ctool_elf32_symbol_type_t type;
  ctool_elf32_symbol_visibility_t visibility;
  ctool_elf32_symbol_placement_t placement;
  ctool_u32 section; /* CTOOL_ELF32_SYMBOL_DEFINED: spec.sections index. */
  ctool_u32 value;
  ctool_u32 size;
  ctool_u32 alignment;
} ctool_elf32_symbol_spec_t;

typedef enum {
  CTOOL_ELF32_R_386_32 = 1,
  CTOOL_ELF32_R_386_PC32 = 2
} ctool_elf32_relocation_type_t;

typedef struct {
  ctool_u32 target_section; /* spec.sections index. */
  ctool_u32 offset;
  ctool_u32 symbol; /* spec.symbols index. */
  ctool_elf32_relocation_type_t type;
  ctool_i32 addend;
} ctool_elf32_relocation_spec_t;

typedef struct {
  const ctool_elf32_section_spec_t *sections;
  ctool_u32 section_count;
  const ctool_elf32_symbol_spec_t *symbols;
  ctool_u32 symbol_count;
  const ctool_elf32_relocation_spec_t *relocations;
  ctool_u32 relocation_count;
} ctool_elf32_object_spec_t;

typedef struct {
  ctool_u32 file_index;
  ctool_string_t name;
  ctool_u32 type;
  ctool_u32 flags;
  ctool_u32 address;
  ctool_u32 file_offset;
  ctool_u32 size;
  ctool_u32 link; /* Raw sh_link; a file index when this type defines one. */
  ctool_u32 info; /* Raw sh_info; a target file index for SHT_REL. */
  ctool_u32 alignment;
  ctool_u32 entry_size;
  ctool_bytes_t contents;
  ctool_u32 relocation_first;
  ctool_u32 relocation_count;
} ctool_elf32_section_t;

typedef struct {
  ctool_u32 file_index;
  ctool_string_t name;
  ctool_u32 binding;
  ctool_u32 type;
  ctool_u32 visibility;
  ctool_elf32_symbol_placement_t placement;
  ctool_u32 section_file_index;
  ctool_u32 value;
  ctool_u32 size;
  ctool_u32 alignment;
} ctool_elf32_symbol_t;

typedef struct {
  ctool_u32 relocation_section_file_index;
  ctool_u32 entry_index;
  ctool_u32 target_section_file_index;
  ctool_u32 offset;
  ctool_u32 symbol_file_index;
  ctool_u32 type;
  ctool_bool addend_known;
  ctool_i32 addend;
} ctool_elf32_relocation_t;

typedef struct {
  ctool_bytes_t image;
  /* Read arrays retain serialized order.  Section and symbol counts include
   * their null entry and every generated metadata entry. */
  const ctool_elf32_section_t *sections;
  ctool_u32 section_count;
  const ctool_elf32_symbol_t *symbols;
  ctool_u32 symbol_count;
  const ctool_elf32_relocation_t *relocations;
  ctool_u32 relocation_count;
  ctool_u32 symbol_table_section_file_index;
} ctool_elf32_object_t;

typedef enum {
  CTOOL_ELF32_DIAG_INVALID_SPEC = 0x03000001u,
  CTOOL_ELF32_DIAG_BAD_MAGIC = 0x03000002u,
  CTOOL_ELF32_DIAG_UNSUPPORTED_DOMAIN = 0x03000003u,
  CTOOL_ELF32_DIAG_BAD_HEADER = 0x03000004u,
  CTOOL_ELF32_DIAG_BAD_SECTION = 0x03000005u,
  CTOOL_ELF32_DIAG_BAD_STRING = 0x03000006u,
  CTOOL_ELF32_DIAG_BAD_SYMBOL = 0x03000007u,
  CTOOL_ELF32_DIAG_BAD_RELOCATION = 0x03000008u,
  CTOOL_ELF32_DIAG_UNSUPPORTED_FEATURE = 0x03000009u,
  CTOOL_ELF32_DIAG_LIMIT = 0x0300000au
} ctool_elf32_diag_code_t;

ctool_status_t ctool_elf32_write(ctool_job_t *job,
                                  const ctool_elf32_object_spec_t *object,
                                  ctool_buffer_t *output);
ctool_status_t ctool_elf32_read(ctool_job_t *job,
                                 const ctool_source_t *source,
                                 ctool_elf32_object_t *object_out);

/* Writer descriptions and source images are borrowed for the call.  Reader
 * metadata is arena-owned; its names and contents borrow source->contents.
 * Writer output must be empty and is rewound to empty after any failure.
 * Every read-side field ending in file_index, plus type-defined raw link/info,
 * uses the serialized ELF table domain, including null/generated entries. */

#endif
