#ifndef CUPID_TOOLCHAIN_CUPIDC_TYPE_H
#define CUPID_TOOLCHAIN_CUPIDC_TYPE_H

#include "cupidc_pp.h"

#define CTOOL_C_TYPE_NONE 0xffffffffu

#define CTOOL_C_QUAL_CONST 0x01u
#define CTOOL_C_QUAL_VOLATILE 0x02u
#define CTOOL_C_QUAL_RESTRICT 0x04u
#define CTOOL_C_QUAL_ATOMIC 0x08u
#define CTOOL_C_QUAL_ALL                                                     \
  (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_VOLATILE | CTOOL_C_QUAL_RESTRICT |     \
   CTOOL_C_QUAL_ATOMIC)

/* The scalar entries are distinct semantic C types even where the i386
 * object representation is identical.  Derived types refer to another entry
 * in the same immutable request table by index. */
typedef enum {
  CTOOL_C_TYPE_VOID = 1,
  CTOOL_C_TYPE_BOOL,
  CTOOL_C_TYPE_CHAR,
  CTOOL_C_TYPE_SIGNED_CHAR,
  CTOOL_C_TYPE_UNSIGNED_CHAR,
  CTOOL_C_TYPE_SIGNED_SHORT,
  CTOOL_C_TYPE_UNSIGNED_SHORT,
  CTOOL_C_TYPE_SIGNED_INT,
  CTOOL_C_TYPE_UNSIGNED_INT,
  CTOOL_C_TYPE_SIGNED_LONG,
  CTOOL_C_TYPE_UNSIGNED_LONG,
  CTOOL_C_TYPE_SIGNED_LONG_LONG,
  CTOOL_C_TYPE_UNSIGNED_LONG_LONG,
  CTOOL_C_TYPE_FLOAT,
  CTOOL_C_TYPE_DOUBLE,
  CTOOL_C_TYPE_LONG_DOUBLE,
  CTOOL_C_TYPE_FUNCTION,
  CTOOL_C_TYPE_POINTER,
  CTOOL_C_TYPE_ARRAY,
  CTOOL_C_TYPE_ENUM,
  CTOOL_C_TYPE_VECTOR,
  CTOOL_C_TYPE_RECORD,
  /* Alignment-carrying typedef/type attribute wrapper. */
  CTOOL_C_TYPE_ALIGNED,
  /* Layout-preserving qualification of an existing semantic type. */
  CTOOL_C_TYPE_QUALIFIED
} ctool_c_type_kind_t;

typedef enum {
  CTOOL_C_RECORD_STRUCT = 1,
  CTOOL_C_RECORD_UNION,
  CTOOL_C_RECORD_CLASS
} ctool_c_record_kind_t;

typedef enum {
  CTOOL_C_ARRAY_FIXED = 1,
  CTOOL_C_ARRAY_UNSPECIFIED,
  /* Reserved for a later evaluated-bound/variably-modified type slice. */
  CTOOL_C_ARRAY_VARIABLE
} ctool_c_array_bound_kind_t;

typedef struct {
  ctool_c_type_kind_t kind;
  /* Presumed location is the primary downstream diagnostic location. */
  ctool_c_pp_location_t location;
  /* Immutable phase-three provenance retained for semantic notes/tools. */
  ctool_c_pp_location_t physical_location;
  /* C qualifiers are part of semantic identity. Const, volatile, and
   * restrict do not change layout; atomic representation follows the fixed
   * i386 rules implemented by the layout operation. */
  ctool_u32 qualifiers;

  /* POINTER: pointee; ARRAY/VECTOR: element; ENUM: compatible integer type;
   * FUNCTION: result type; ALIGNED/QUALIFIED: wrapped type. */
  ctool_u32 referenced_type;
  /* Fixed ARRAY element count or VECTOR lane count. */
  ctool_u32 element_count;
  ctool_c_array_bound_kind_t array_bound_kind;

  /* FUNCTION parameter references select the request's flat parameter table.
   * has_prototype distinguishes `int()` from `int(void)`; the latter has an
   * empty slice with has_prototype true. */
  ctool_u32 first_parameter;
  ctool_u32 parameter_count;
  ctool_bool has_prototype;
  ctool_bool variadic;

  ctool_c_record_kind_t record_kind;
  ctool_bool record_complete;
  ctool_bool record_packed;
  /* RECORD: zero selects computed alignment. ALIGNED: exact effective
   * typedef/type-attribute alignment, which may raise or lower the wrapped
   * type's natural alignment. */
  ctool_u32 explicit_alignment;
  ctool_u32 first_member;
  ctool_u32 member_count;
} ctool_c_type_node_t;

typedef struct {
  ctool_string_t name;
  ctool_u32 type;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
  /* The preprocessing cap sampled from the first declaration-specifier token
   * of this member declaration and shared by every declarator in it. Zero
   * selects natural alignment. */
  ctool_u32 pack_alignment;
  ctool_bool is_bit_field;
  ctool_u32 bit_width;
  ctool_bool anonymous;
  ctool_bool member_packed;
  /* Zero selects the type alignment. */
  ctool_u32 explicit_alignment;
} ctool_c_record_member_t;

typedef struct {
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
  const ctool_c_type_node_t *types;
  ctool_u32 type_count;
  const ctool_c_record_member_t *members;
  ctool_u32 member_count;
  const ctool_u32 *parameter_types;
  ctool_u32 parameter_type_count;
} ctool_c_layout_request_t;

typedef struct {
  ctool_u32 size;
  ctool_u32 alignment;
  ctool_bool is_complete_object;
  ctool_bool is_object;
  ctool_bool is_integer;
  ctool_bool is_signed;
} ctool_c_type_layout_t;

typedef struct {
  ctool_u32 byte_offset;
  /* For a bit-field, the least-significant bit relative to byte_offset. */
  ctool_u32 bit_offset;
  ctool_u32 bit_width;
  /* Ordinary member object size or the declared bit-field storage size. */
  ctool_u32 size;
  /* Effective member alignment after packed/explicit/pragma policy. */
  ctool_u32 alignment;
} ctool_c_member_layout_t;

typedef struct {
  const ctool_c_type_layout_t *types;
  ctool_u32 type_count;
  const ctool_c_member_layout_t *members;
  ctool_u32 member_count;
} ctool_c_layout_result_t;

typedef enum {
  CTOOL_C_TYPE_DIAG_INVALID_REQUEST = 0x0a000001u,
  CTOOL_C_TYPE_DIAG_INVALID_REFERENCE = 0x0a000002u,
  CTOOL_C_TYPE_DIAG_MEMBER_SLICE = 0x0a000003u,
  CTOOL_C_TYPE_DIAG_MEMBER_OVERLAP = 0x0a000004u,
  CTOOL_C_TYPE_DIAG_ARRAY = 0x0a000005u,
  CTOOL_C_TYPE_DIAG_ALIGNMENT = 0x0a000006u,
  CTOOL_C_TYPE_DIAG_BIT_FIELD = 0x0a000007u,
  CTOOL_C_TYPE_DIAG_FLEXIBLE_ARRAY = 0x0a000008u,
  CTOOL_C_TYPE_DIAG_CYCLE = 0x0a000009u,
  CTOOL_C_TYPE_DIAG_INCOMPLETE = 0x0a00000au,
  CTOOL_C_TYPE_DIAG_OVERFLOW = 0x0a00000bu,
  CTOOL_C_TYPE_DIAG_LIMIT = 0x0a00000cu,
  CTOOL_C_TYPE_DIAG_INVALID_TYPE = 0x0a00000du,
  CTOOL_C_TYPE_DIAG_RECORD = 0x0a00000eu,
  CTOOL_C_TYPE_DIAG_INTERNAL = 0x0a00000fu
} ctool_c_type_diag_code_t;

ctool_status_t ctool_c_layout_types(ctool_job_t *job,
                                     const ctool_c_layout_request_t *request,
                                     ctool_c_layout_result_t *result_out);

/* Request arrays, names, and locations are borrowed for the call.  Success
 * publishes parallel job-arena-owned type/member arrays in request order.
 * A failed operation zeros its result and rewinds every operation allocation;
 * its structured diagnostic remains job-owned.  The operation implements the
 * fixed little-endian i386 SysV ILP32 object-layout contract and does not infer
 * target facts from the compiler hosting the bootstrap. Scratch traversal
 * storage is reclaimed before success without invalidating published arrays.
 *
 * This operation validates graph ownership, references, completion needed for
 * layout, and target representation. The declaration frontend remains
 * responsible for C namespace and declarator legality, including function
 * return/parameter constraints, qualifier placement, duplicate names, and
 * anonymous-member syntax. It also owns flexible-array named-member
 * eligibility, including names promoted through anonymous records; layout
 * enforces only the required final structure position. A qualified wrapper
 * preserves nominal identity without duplicating a record's member slice. An
 * aligned wrapper represents the exact effective alignment carried by a
 * typedef/type attribute without changing the wrapped record definition.
 * Introducing atomic qualification applies the fixed target minimum; a later
 * aligned wrapper still applies its exact effective alignment, and later
 * non-atomic qualification preserves that result. Atomic scalar, pointer, and
 * enum layouts are represented; atomic aggregate layout remains an explicit
 * unsupported boundary. */

#endif
