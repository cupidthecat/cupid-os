#ifndef CUPID_TOOLCHAIN_PE32_H
#define CUPID_TOOLCHAIN_PE32_H

#include "ctool.h"

/* Bounded reader for the deterministic static i386 PE32 profile emitted by
 * CupidLD. It does not model general Windows executables or dynamic linking. */

#define CTOOL_PE32_IMAGE_BASE 0x00400000u
#define CTOOL_PE32_SECTION_ALIGNMENT 0x00001000u
#define CTOOL_PE32_FILE_ALIGNMENT 0x00000200u
#define CTOOL_PE32_IMAGE_SIZE_LIMIT 0x80000000u

#define CTOOL_PE32_SCN_CODE 0x00000020u
#define CTOOL_PE32_SCN_INITIALIZED_DATA 0x00000040u
#define CTOOL_PE32_SCN_UNINITIALIZED_DATA 0x00000080u
#define CTOOL_PE32_SCN_EXECUTE 0x20000000u
#define CTOOL_PE32_SCN_READ 0x40000000u
#define CTOOL_PE32_SCN_WRITE 0x80000000u

typedef enum {
  CTOOL_PE32_SECTION_TEXT = 0,
  CTOOL_PE32_SECTION_RODATA,
  CTOOL_PE32_SECTION_DATA,
  CTOOL_PE32_SECTION_BSS,
  CTOOL_PE32_SECTION_IDATA
} ctool_pe32_section_kind_t;

typedef struct {
  ctool_u32 file_index;
  ctool_string_t name;
  ctool_pe32_section_kind_t kind;
  ctool_u32 virtual_address;
  ctool_u32 virtual_size;
  ctool_u32 file_offset;
  ctool_u32 file_size;
  ctool_u32 characteristics;
  ctool_bytes_t contents;
} ctool_pe32_section_t;

typedef struct {
  ctool_u32 file_index;
  ctool_string_t name;
  ctool_u32 lookup_rva;
  ctool_u32 iat_rva;
  ctool_u32 import_first;
  ctool_u32 import_count;
} ctool_pe32_import_library_t;

typedef struct {
  ctool_u32 file_index;
  ctool_u32 library_file_index;
  ctool_string_t library_name;
  ctool_string_t procedure_name;
  ctool_u32 hint_name_rva;
  ctool_u32 lookup_rva;
  ctool_u32 iat_rva;
} ctool_pe32_import_t;

typedef struct {
  ctool_bytes_t image;
  ctool_u32 entry_rva;
  ctool_u32 entry_point;
  ctool_u32 image_base;
  ctool_u32 base_of_code;
  ctool_u32 base_of_data;
  ctool_u32 code_size;
  ctool_u32 initialized_data_size;
  ctool_u32 uninitialized_data_size;
  ctool_u32 image_size;
  ctool_u32 headers_size;
  ctool_u32 section_alignment;
  ctool_u32 file_alignment;
  ctool_u32 subsystem;
  ctool_u32 dll_characteristics;
  ctool_u32 import_directory_rva;
  ctool_u32 import_directory_size;
  ctool_u32 iat_directory_rva;
  ctool_u32 iat_directory_size;
  const ctool_pe32_section_t *sections;
  ctool_u32 section_count;
  const ctool_pe32_import_library_t *import_libraries;
  ctool_u32 import_library_count;
  const ctool_pe32_import_t *imports;
  ctool_u32 import_count;
} ctool_pe32_image_t;

typedef enum {
  CTOOL_PE32_DIAG_BAD_DOS_HEADER = 0x0e000001u,
  CTOOL_PE32_DIAG_BAD_SIGNATURE = 0x0e000002u,
  CTOOL_PE32_DIAG_UNSUPPORTED_COFF = 0x0e000003u,
  CTOOL_PE32_DIAG_BAD_OPTIONAL_HEADER = 0x0e000004u,
  CTOOL_PE32_DIAG_BAD_DIRECTORY = 0x0e000005u,
  CTOOL_PE32_DIAG_BAD_SECTION = 0x0e000006u,
  CTOOL_PE32_DIAG_BAD_ENTRY = 0x0e000007u,
  CTOOL_PE32_DIAG_BAD_IMPORT = 0x0e000008u,
  CTOOL_PE32_DIAG_LIMIT = 0x0e000009u
} ctool_pe32_diag_code_t;

ctool_status_t ctool_pe32_read(ctool_job_t *job,
                               const ctool_source_t *source,
                               ctool_pe32_image_t *image_out);

/* Metadata is arena-owned. Names, section contents, and image bytes borrow
 * source->contents. A failed read rewinds all metadata and clears image_out. */

#endif
