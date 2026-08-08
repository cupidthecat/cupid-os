#ifndef CUPID_TOOLCHAIN_CUPIDOBJ_H
#define CUPID_TOOLCHAIN_CUPIDOBJ_H

#include "ctool.h"
#include "elf32.h"

#define CTOOL_OBJ_PROFILE_MAGIC_BYTES 8u
#define CTOOL_OBJ_PROFILE_SCHEMA_BYTES 127u
#define CTOOL_OBJ_PROFILE_NAME_BYTES 63u
#define CTOOL_OBJ_PROFILE_PATH_BYTES 1024u
#define CTOOL_OBJ_PROFILE_LIMIT 16u
#define CTOOL_OBJ_PROFILE_INPUT_LIMIT 512u
#define CTOOL_OBJ_PROFILE_REFERENCE_LIMIT 2048u

typedef enum {
  CTOOL_OBJ_WRAP_BINARY = 1,
  CTOOL_OBJ_WRAP_TEXT,
  CTOOL_OBJ_EXTRACT_FLAT,
  CTOOL_OBJ_GENERATE_INSTALL_SOURCE,
  CTOOL_OBJ_GENERATE_KSYMS_SOURCE,
  CTOOL_OBJ_WRAP_JPEG,
  CTOOL_OBJ_BUILD_DISK_TEMPLATE,
  CTOOL_OBJ_BUILD_ISO_FIXTURE,
  CTOOL_OBJ_GENERATE_PROFILE_MANIFEST
} ctool_obj_operation_t;

typedef enum {
  CTOOL_OBJ_INSTALL_DEMOS = 1,
  CTOOL_OBJ_INSTALL_BIN,
  CTOOL_OBJ_INSTALL_DOCS
} ctool_obj_install_source_kind_t;

typedef struct {
  ctool_string_t section_name;
  ctool_u32 section_flags;
  ctool_u32 section_alignment;
  ctool_string_t start_symbol;
  ctool_string_t end_symbol;
  ctool_string_t size_symbol;
} ctool_obj_wrap_binary_request_t;

typedef struct {
  ctool_obj_install_source_kind_t kind;
  const ctool_string_t *bin_paths;
  ctool_u32 bin_count;
  const ctool_string_t *header_paths;
  ctool_u32 header_count;
  const ctool_string_t *browser_paths;
  ctool_u32 browser_count;
  const ctool_string_t *ctxt_paths;
  ctool_u32 ctxt_count;
  const ctool_string_t *doc_asset_paths;
  ctool_u32 doc_asset_count;
  const ctool_string_t *home_asset_paths;
  ctool_u32 home_asset_count;
  const ctool_string_t *demo_paths;
  ctool_u32 demo_count;
} ctool_obj_install_source_request_t;

typedef struct {
  const ctool_source_t *kernel;
  ctool_u32 image_sectors;
  ctool_u32 fat_start_lba;
} ctool_obj_disk_template_request_t;

typedef enum {
  CTOOL_OBJ_ISO_FIXTURE_DIRECTORY = 1,
  CTOOL_OBJ_ISO_FIXTURE_FILE
} ctool_obj_iso_fixture_kind_t;

typedef struct {
  ctool_string_t path;
  ctool_obj_iso_fixture_kind_t kind;
  const ctool_source_t *source;
} ctool_obj_iso_fixture_entry_t;

typedef struct {
  const ctool_obj_iso_fixture_entry_t *entries;
  ctool_u32 entry_count;
} ctool_obj_iso_fixture_request_t;

typedef struct {
  ctool_obj_operation_t operation;
  const ctool_source_t *input;
  union {
    ctool_obj_wrap_binary_request_t wrap_binary;
    ctool_obj_install_source_request_t install_source;
    ctool_obj_disk_template_request_t disk_template;
    ctool_obj_iso_fixture_request_t iso_fixture;
  } as;
} ctool_obj_request_t;

typedef struct {
  ctool_bytes_t bytes;
  ctool_u32 base_address;
  ctool_u32 end_address;
} ctool_obj_result_t;

typedef enum {
  CTOOL_OBJ_DIAG_INVALID_REQUEST = 0x08000001u,
  CTOOL_OBJ_DIAG_INVALID_INPUT = 0x08000002u,
  CTOOL_OBJ_DIAG_INVALID_SECTION = 0x08000003u,
  CTOOL_OBJ_DIAG_INVALID_SYMBOL = 0x08000004u,
  CTOOL_OBJ_DIAG_SYMBOL_COLLISION = 0x08000005u,
  CTOOL_OBJ_DIAG_NO_LOAD = 0x08000006u,
  CTOOL_OBJ_DIAG_OVERLAP = 0x08000007u,
  CTOOL_OBJ_DIAG_ADDRESS_OVERFLOW = 0x08000008u,
  CTOOL_OBJ_DIAG_LIMIT = 0x08000009u,
  CTOOL_OBJ_DIAG_OUTPUT = 0x0800000au,
  CTOOL_OBJ_DIAG_UNSUPPORTED = 0x0800000bu
} ctool_obj_diag_code_t;

ctool_status_t ctool_obj_transform(ctool_job_t *job,
                                    const ctool_obj_request_t *request,
                                    ctool_buffer_t *output,
                                    ctool_obj_result_t *result_out);

/* Request/source and installation path views are borrowed for the call.
 * WRAP_BINARY emits one
 * canonical ELF32 ET_REL PROGBITS section with the exact requested bytes and
 * global start, end, and absolute size symbols.  WRAP_TEXT has the same
 * object model but canonicalizes CRLF pairs to LF; lone carriage returns are
 * retained.  WRAP_JPEG first validates one baseline or extended sequential
 * SOF0/SOF1 frame, at least one scan, and a terminal EOI marker, then wraps
 * the exact input bytes with the WRAP_BINARY object model.  Progressive and
 * other unsupported frame types are rejected.  EXTRACT_FLAT lays out
 * initialized PT_LOAD bytes by physical address, with a checked
 * allocated-section fallback for executables without load segments; BSS is
 * excluded.  WRAP_JPEG validation and emission are transactional at this
 * transform boundary: failures retain no temporary arena storage, and the
 * same job can be reused.
 *
 * Output must be empty.  Every failure preserves its pre-call bytes and fully
 * zeros result_out.  On success result bytes borrow output; extraction
 * addresses describe the half-open initialized range [base_address,
 * end_address).  Equal requests and inputs produce byte-identical output.
 *
 * GENERATE_INSTALL_SOURCE emits one of the bin, docs, or demos installation
 * tables from a typed path inventory.  Paths use repository-relative forward
 * slash spelling and their category's exact extension.  A request may contain
 * at most 512 paths across all categories, and that total is checked without
 * overflowing before a list is traversed.  Output keeps caller order within
 * each typed list.  Category mixing, malformed paths, duplicate paths,
 * distinct paths that map to the same complete wrapped binary symbol, and
 * output exhaustion fail before publication.  One exact BMP path may appear
 * once in both the documentation and home lists because both entries use the
 * same wrapped object.
 *
 * GENERATE_KSYMS_SOURCE consumes canonical CupidDis nm text.  It retains
 * defined text and weak-text symbols, orders them by address while preserving
 * input order at equal addresses, keeps the first symbol at each address, and
 * emits the kernel's word-packed .ksyms C source.  Undefined and non-text
 * rows are ignored.  Malformed rows, addresses outside i386, an empty text
 * symbol set, allocation limits, and output limits fail before publication. */

/* BUILD_DISK_TEMPLATE consumes the five-sector Cupid boot image as input and
 * one separately loaded raw kernel source.  The request supplies the complete
 * image size and FAT16 partition start in 512-byte sectors.  The operation
 * patches one active type-0x06 MBR entry, copies stage two, places the kernel
 * at LBA 5, and zero-fills the remaining pre-partition reserve.  It then emits
 * the canonical CUPIDOS FAT16 boot sector, two pristine FATs, and the empty
 * 512-entry root directory.  Output ends immediately before FAT cluster 2;
 * the caller extends the full image and owns later filesystem mutations.
 *
 * The boot image must contain at least five sectors.  The FAT partition must
 * begin after LBA 5 and within the image, the kernel must end at or before the
 * partition boundary, and the partition must admit a FAT16 cluster count.
 * All byte and sector calculations are checked before narrowing to i386.
 * BUILD_DISK_TEMPLATE leaves result addresses zero because its offsets are
 * disk positions rather than load addresses.  Its source views are borrowed
 * for the call, equal inputs produce byte-identical output, and every failure
 * follows the transform-wide output and result rollback contract above. */

/* BUILD_ISO_FIXTURE consumes an ASCII manifest as input and a borrowed flat
 * inventory of logical directories and loaded files.  Every manifest path
 * must have one exact entry, every parent directory must be represented, and
 * names use the repository fixture's portable ASCII spelling.  The operation
 * emits the complete deterministic ECMA-119 image with fixed RRIP_1991A SP,
 * PX, TF, NM, CE, and ER records.  Both path-table byte orders, breadth-first
 * directory numbering, identifier collision suffixes, block boundaries,
 * fixed UTC dates, and contiguous file extents are part of the byte contract.
 * The root continuation follows every directory stream, and ST terminators
 * are intentionally absent.
 *
 * At most 512 entries are accepted.  Directory entries borrow only their
 * logical path; file entries also borrow a loaded source.  Native path safety,
 * freezing, parity checks, and publication remain caller responsibilities.
 * Equal logical inventories and file bytes produce byte-identical output.
 * Result addresses remain zero, and failures follow the transform-wide
 * output, result, and arena rollback contract. */

/* GENERATE_PROFILE_MANIFEST consumes one bounded binary snapshot.  The
 * little-endian CUPROF1 envelope carries a schema name, named profiles with
 * header and source membership, and the exact bytes of every captured header
 * input.  CupidObj validates safe repository-relative ASCII paths, exact
 * header-to-input membership, unique names, portable case identity, bounded
 * counts, and complete consumption of the envelope.  It then emits canonical
 * indented JSON, sorts every named set, and records each input's size and
 * SHA-256 digest.
 *
 * The wire grammar is:
 *
 *   "CUPROF1\0", bytes(schema), u32(profile_count),
 *   repeated profile_count times {
 *     bytes(name), u32(header_count), repeated bytes(header),
 *     u32(source_count), repeated bytes(source)
 *   },
 *   u32(input_count), repeated input_count times {
 *     bytes(path), bytes(contents)
 *   }
 *
 * `u32` is little-endian.  `bytes(value)` is a u32 byte count followed by the
 * exact bytes.  The public PROFILE constants cap the schema, names, paths,
 * profiles, each membership list, captured inputs, and the combined header
 * plus source references.  Source rows carry membership only; input rows hold
 * the header bytes that CupidObj hashes.
 *
 * The snapshot is the complete deterministic input.  Native filesystem
 * discovery, freezing, parity checks, and publication remain caller
 * responsibilities.  Equivalent typed snapshots produce byte-identical
 * output regardless of profile, membership, or input order.  Result addresses
 * remain zero, and failures follow the transform-wide output, result, and
 * arena rollback contract. */

#endif
