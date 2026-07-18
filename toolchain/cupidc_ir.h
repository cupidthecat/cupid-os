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
  CTOOL_C_IR_INSTRUCTION_UNARY,
  CTOOL_C_IR_INSTRUCTION_DUPLICATE_VALUE,
  CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_DEREFERENCE,
  CTOOL_C_IR_INSTRUCTION_ADDRESS_OF,
  CTOOL_C_IR_INSTRUCTION_POINTER_BINARY,
  CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER
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
   * UNARY and BINARY retain their operand type. POINTER_BINARY retains its
   * left operand type. ARRAY_TO_POINTER retains its array operand type.
   * DUPLICATE_VALUE retains the duplicated value type. DUPLICATE_ADDRESS
   * retains the duplicated object's type. DEREFERENCE retains its pointer
   * operand type. ADDRESS_OF retains its object operand type. CALL_DIRECT
   * retains the function type. BRANCH_ZERO retains its consumed condition
   * type. */
  ctool_u32 input_type;
  ctool_c_expression_operator_t operation;
  /* CONVERT uses NONE for an explicit cast and the exact conversion kind for
   * an implicit conversion. */
  ctool_c_conversion_kind_t conversion;
  /* PARAMETER_ADDRESS uses an absolute frontend parameter index.
   * LOCAL_ADDRESS uses an absolute frontend block-binding index. FILE_ADDRESS
   * and CALL_DIRECT use an absolute file-binding index. MEMBER_ADDRESS and
   * BIT_FIELD_LOAD use an absolute graph-member index. POINTER_BINARY uses
   * an absolute graph-type index for its right operand. Branches use a
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

/* Reports whether two published semantic types carry compatible represented
 * four-byte object or void pointer values. Top-level pointer-object
 * qualifiers do not belong to the value after lvalue conversion. Referent
 * qualifiers remain significant. The query walks qualified, aligned,
 * pointer, array, vector, enumeration, and scalar nodes without allocating.
 * Distinct function and record nodes remain compatible only when the
 * frontend has already canonicalized them to the same graph entry. */
ctool_bool ctool_c_ir_pointer_value_types_compatible(
    const ctool_c_translation_unit_t *unit, ctool_u32 left,
    ctool_u32 right);

/* Reports whether two represented object pointers may participate in pointer
 * subtraction. The pointed-to types must be compatible complete objects.
 * Immediate pointed-to qualification, including atomic qualification, does
 * not change compatibility for this operation. */
ctool_bool ctool_c_ir_pointer_arithmetic_types_compatible(
    const ctool_c_translation_unit_t *unit, ctool_u32 left,
    ctool_u32 right);

/* Reports whether a complete array object may decay to a represented pointer
 * to its first element. Array qualification follows the C rule that moves it
 * to the element type. */
ctool_bool ctool_c_ir_array_decay_types_compatible(
    const ctool_c_translation_unit_t *unit, ctool_u32 array_type,
    ctool_u32 pointer_type);

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
 * or declaration initializer once, then its optional condition, body, and
 * optional iteration expression in C source order. Declarations in supported
 * compound statements use the same source-ordered block bindings as outer
 * declarations. An omitted condition has no false exit.
 * Break uses JUMP to the nearest loop or switch exit. Continue uses JUMP to
 * the nearest loop's continuation point. A do continuation reaches its
 * condition, while a for continuation reaches its iteration expression when
 * one is present and its condition otherwise. Identifier labels select
 * zero-width targets inside one function.
 * Direct goto statements use JUMP with a function-relative target. Forward
 * targets are resolved before the immutable result is published.
 * A switch evaluates its promoted integer condition once. DUPLICATE_VALUE
 * preserves that value while equality tests select resolved case targets.
 * LOCAL_ADDRESS and FILE_ADDRESS push object addresses. DUPLICATE_ADDRESS
 * preserves an address while a represented scalar compound assignment or
 * update loads and stores the object. This evaluates the destination once.
 * Integer compound assignments retain integer-promotion,
 * usual-arithmetic, and assignment conversions. Pointer compound
 * assignments and updates use POINTER_BINARY with a complete-object stride.
 * Prefix updates produce the stored value. Postfix updates produce the value
 * from before the store.
 * MEMBER_ADDRESS consumes a record address and pushes the selected complete,
 * direct, non-bit-field member address. BIT_FIELD_LOAD consumes a record
 * address and pushes the selected field's extracted integer value.
 * DEREFERENCE consumes a pointer value and pushes the referenced object
 * address. It emits no target instruction because both forms occupy one
 * 32-bit machine word, while the public IR keeps their meanings distinct.
 * ADDRESS_OF performs the inverse transition for an object designator.
 * ARRAY_TO_POINTER consumes an array address and produces the address of its
 * first element without emitting a target instruction. POINTER_BINARY scales
 * an integer operand by the pointed-to object size. Pointer subtraction
 * divides the byte difference by that size and produces signed int.
 * STORE consumes the value on top of the stack and the destination address
 * below it, without producing a result.
 * STORE_VALUE consumes the same pair and pushes the stored assignment result.
 * DISCARD consumes one value.
 * CALL_DIRECT consumes its fixed arguments after they have been evaluated in
 * source order. Argument zero is deepest on the stack and the final argument
 * is on top. The call pushes one result unless its result type is void.
 * Failure zeros the result, rewinds allocations made during the operation,
 * and keeps its structured diagnostic in the job. */

#endif
