#ifndef CUPID_TOOLCHAIN_CUPIDC_IR_H
#define CUPID_TOOLCHAIN_CUPIDC_IR_H

#include "cupidc_frontend.h"

typedef enum {
  CTOOL_C_IR_INSTRUCTION_INTEGER = 1,
  CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_LOAD,
  CTOOL_C_IR_INSTRUCTION_CONVERT,
  CTOOL_C_IR_INSTRUCTION_BINARY,
  CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
  CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
  CTOOL_C_IR_INSTRUCTION_JUMP,
  CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
  CTOOL_C_IR_INSTRUCTION_RETURN_VOID,
  CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_STORE,
  CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_STORE_VALUE,
  CTOOL_C_IR_INSTRUCTION_DISCARD,
  CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD,
  CTOOL_C_IR_INSTRUCTION_UNARY
} ctool_c_ir_instruction_kind_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  /* Value-producing instructions use type for their result. Address
   * instructions and STORE use the referenced destination object type.
   * STORE_VALUE uses the assignment result type. Control instructions and
   * DISCARD use CTOOL_C_TYPE_NONE, except RETURN_VALUE, which retains the
   * result type. */
  ctool_u32 type;
  /* LOAD and CONVERT retain their source type. MEMBER_ADDRESS and
   * BIT_FIELD_LOAD retain their record operand type. STORE and STORE_VALUE
   * retain the stored value type. DISCARD retains its consumed value type.
   * UNARY and BINARY retain their operand type. CALL_DIRECT retains the
   * function type. BRANCH_ZERO retains its consumed condition type. */
  ctool_u32 input_type;
  ctool_c_expression_operator_t operation;
  /* CONVERT uses NONE for an explicit cast and the exact conversion kind for
   * an implicit conversion. */
  ctool_c_conversion_kind_t conversion;
  /* PARAMETER_ADDRESS uses an absolute frontend parameter index.
   * LOCAL_ADDRESS uses an absolute frontend block-binding index. FILE_ADDRESS
   * and CALL_DIRECT use an absolute file-binding index. MEMBER_ADDRESS and
   * BIT_FIELD_LOAD use an absolute graph-member index. Branches use a
   * function-relative instruction index. Other instructions use
   * CTOOL_C_AST_NONE. */
  ctool_u32 reference;
  ctool_u64 integer_bits;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_ir_instruction_t;

typedef struct {
  ctool_u32 binding;
  ctool_u32 declared_type;
  ctool_u32 first_instruction;
  ctool_u32 instruction_count;
  ctool_u32 maximum_stack_depth;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_ir_function_t;

typedef struct {
  const ctool_c_ir_function_t *functions;
  ctool_u32 function_count;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 instruction_count;
} ctool_c_ir_unit_t;

typedef enum {
  CTOOL_C_IR_DIAG_INVALID_REQUEST = 0x0d000001u,
  CTOOL_C_IR_DIAG_INVALID_UNIT = 0x0d000002u,
  CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE = 0x0d000003u,
  CTOOL_C_IR_DIAG_UNSUPPORTED_STATEMENT = 0x0d000004u,
  CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION = 0x0d000005u,
  CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION = 0x0d000006u,
  CTOOL_C_IR_DIAG_ABI = 0x0d000007u,
  CTOOL_C_IR_DIAG_LIMIT = 0x0d000008u,
  CTOOL_C_IR_DIAG_INTERNAL = 0x0d000009u,
  CTOOL_C_IR_DIAG_EXTERNAL_INLINE = 0x0d00000au
} ctool_c_ir_diag_code_t;

ctool_status_t ctool_c_lower_ir(ctool_job_t *job,
                                const ctool_c_translation_unit_t *unit,
                                ctool_c_ir_unit_t *result_out);

/* The typed translation unit is borrowed and remains unchanged. Success
 * publishes immutable function and instruction arrays in the job arena.
 * Each function owns a contiguous instruction slice and a typed abstract
 * stack that begins and ends empty. Branch targets are relative to that
 * slice, and every join has the same address/value stack shape on each
 * incoming path. A pre-test while loop uses BRANCH_ZERO for its forward exit
 * and JUMP for its backward edge. A for loop evaluates its optional expression
 * initializer once, then its optional condition, body, and optional iteration
 * expression in C source order. An omitted condition has no false exit.
 * Break and continue use JUMP to the nearest loop's exit and continuation
 * point. A do continuation reaches its condition, while a for continuation
 * reaches its iteration expression when one is present and its condition
 * otherwise.
 * LOCAL_ADDRESS and FILE_ADDRESS push object addresses.
 * MEMBER_ADDRESS consumes a record address and pushes the selected complete,
 * direct, non-bit-field member address. BIT_FIELD_LOAD consumes a record
 * address and pushes the selected field's extracted integer value. STORE
 * consumes the value on top of the stack and the destination address below
 * it, without producing a result.
 * STORE_VALUE consumes the same pair and pushes the stored assignment result.
 * DISCARD consumes one value.
 * CALL_DIRECT consumes its fixed arguments after they have been evaluated in
 * source order. Argument zero is deepest on the stack and the final argument
 * is on top. The call pushes one result unless its result type is void.
 * Failure zeros the result, rewinds allocations made during the operation,
 * and keeps its structured diagnostic in the job. */

#endif
