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
  CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
  CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT,
  CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_FUNCTION_TO_POINTER,
  CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT,
  CTOOL_C_IR_INSTRUCTION_ELEMENT_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_COPY_OBJECT,
  CTOOL_C_IR_INSTRUCTION_STRING_LITERAL_ADDRESS,
  CTOOL_C_IR_INSTRUCTION_COPY_STRING,
  CTOOL_C_IR_INSTRUCTION_VARIADIC_START,
  CTOOL_C_IR_INSTRUCTION_VARIADIC_ARGUMENT,
  CTOOL_C_IR_INSTRUCTION_VARIADIC_END
} ctool_c_ir_instruction_kind_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  /* Value-producing instructions use type for their result. Object-address
   * instructions, aggregate LOAD, STORE, and ZERO_OBJECT use the object type.
   * FUNCTION_ADDRESS retains its function-designator type.
   * STRING_LITERAL_ADDRESS retains its character-array type.
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
   * operand type. ADDRESS_OF retains its object or function operand type.
   * FUNCTION_TO_POINTER retains its function operand type. ZERO_OBJECT and
   * COPY_OBJECT retain aggregate address operand types. COPY_STRING retains
   * its character-array address operand type. ELEMENT_ADDRESS retains its
   * fixed-array operand type.
   * VARIADIC_START/VARIADIC_ARGUMENT/VARIADIC_END retain the consumed
   * cursor-object type. VARIADIC_ARGUMENT uses type for the loaded result.
   * CALL_DIRECT retains the function type, while
   * CALL_INDIRECT retains the function pointer type. BRANCH_ZERO retains its
   * consumed condition type. */
  ctool_u32 input_type;
  ctool_c_expression_operator_t operation;
  /* CONVERT uses NONE for an explicit cast and the exact conversion kind for
   * an implicit conversion. */
  ctool_c_conversion_kind_t conversion;
  /* CALL_DIRECT and CALL_INDIRECT retain the actual argument count after
   * default argument promotions. Other instructions keep zero. */
  ctool_u32 argument_count;
  /* PARAMETER_ADDRESS uses an absolute frontend parameter index.
   * LOCAL_ADDRESS uses an absolute frontend block-binding index for a
   * represented scalar or a complete fixed array or record.
   * COMPOUND_LITERAL_ADDRESS and COMPOUND_LITERAL_STAGING_ADDRESS use the
   * absolute frontend expression index of the unnamed automatic object. The
   * staging address is private storage used to finish aggregate initialization
   * before replacing the persistent object. Object offsets and structure
   * snapshot storage stay private to the emitter. STRING_LITERAL_ADDRESS uses
   * an absolute frontend expression index. COPY_STRING uses an absolute
   * semantic initializer index. FILE_ADDRESS,
   * CALL_DIRECT, and
   * FUNCTION_ADDRESS use an absolute file-binding index.
   * MEMBER_ADDRESS and BIT_FIELD_LOAD use an absolute graph-member index.
   * ELEMENT_ADDRESS uses a direct fixed-array element index.
   * POINTER_BINARY uses an absolute graph-type index for its right operand.
   * VARIADIC_START uses the absolute final named parameter index.
   * Branches use a function-relative instruction index. Other instructions
   * use CTOOL_C_AST_NONE. */
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
 * four-byte object, void, or function pointer values. Top-level pointer-object
 * qualifiers do not belong to the value after lvalue conversion. Referent
 * qualifiers remain significant. The query compares qualified, aligned,
 * pointer, array, vector, function, enumeration, and scalar nodes with a
 * memoized worklist. It returns all scratch storage to the job arena before
 * returning. Function types are compared structurally. Top-level const,
 * volatile, and restrict parameter qualification is ignored, while atomic
 * and referent qualification remains significant. Distinct record nodes
 * remain compatible only when the frontend has canonicalized them to the same
 * graph entry. A malformed graph or out-of-range type returns CTOOL_OK with a
 * false result. Missing job or output arguments return
 * CTOOL_ERR_INVALID_ARGUMENT. */
ctool_status_t ctool_c_ir_pointer_value_types_compatible(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 left, ctool_u32 right, ctool_bool *compatible_out);

/* Applies the same structural relation to pointer comparison operands.
 * Requiring object referents rejects function and void pointers, as needed
 * for relational comparison. */
ctool_status_t ctool_c_ir_pointer_comparison_types_compatible(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 left, ctool_u32 right, ctool_bool require_object_referents,
    ctool_bool *compatible_out);

/* Checks one implicit pointer conversion against the IR lowerer's
 * qualification and object-to-void rules. */
ctool_status_t ctool_c_ir_pointer_conversion_is_valid(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 source_type, ctool_u32 target_type,
    ctool_c_conversion_kind_t conversion, ctool_bool *valid_out);

/* Reports whether two represented object pointers may participate in pointer
 * subtraction. The pointed-to types must be compatible complete objects.
 * Immediate pointed-to qualification, including atomic qualification, does
 * not change compatibility for this operation. */
ctool_status_t ctool_c_ir_pointer_arithmetic_types_compatible(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 left, ctool_u32 right, ctool_bool *compatible_out);

/* Reports whether a complete array object may decay to a represented pointer
 * to its first element. Array qualification follows the C rule that moves it
 * to the element type. */
ctool_status_t ctool_c_ir_array_decay_types_compatible(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 array_type, ctool_u32 pointer_type,
    ctool_bool *compatible_out);

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
 * stack that begins and ends empty. Represented one-byte and two-byte integer
 * values occupy canonical 32-bit stack words after signed or unsigned
 * extension. A supported structure value also occupies one abstract stack
 * entry. The i386 emitter stores its copied bytes in private frame storage,
 * so temporary addresses do not become part of the public IR. Branch targets
 * are relative to that slice, and every join has the same address/value stack
 * shape on each incoming path. A pre-test while loop uses BRANCH_ZERO for its
 * forward exit
 * and JUMP for its backward edge. A for loop evaluates its optional expression
 * or declaration initializer once, then its optional condition, body, and
 * optional iteration expression in C source order. Declarations in supported
 * compound statements use the same source-ordered block bindings as outer
 * declarations. A block extern object advances lexical visibility without
 * emitting runtime work or reserving local storage. Its uses publish
 * FILE_ADDRESS against the canonical linked binding. An omitted condition has
 * no false exit.
 * Break uses JUMP to the nearest loop or switch exit. Continue uses JUMP to
 * the nearest loop's continuation point. A do continuation reaches its
 * condition, while a for continuation reaches its iteration expression when
 * one is present and its condition otherwise. Identifier labels select
 * zero-width targets inside one function.
 * Direct goto statements use JUMP with a function-relative target. Forward
 * targets are resolved before the immutable result is published.
 * A switch evaluates its promoted integer condition once. DUPLICATE_VALUE
 * preserves that value while equality tests select resolved case targets.
 * LOCAL_ADDRESS, COMPOUND_LITERAL_ADDRESS,
 * COMPOUND_LITERAL_STAGING_ADDRESS, STRING_LITERAL_ADDRESS, and FILE_ADDRESS
 * push object addresses.
 * A referenced automatic scalar receives one target-sized slot.
 * A referenced fixed array or record receives its target size and alignment,
 * up to four-byte alignment. Each compound-literal source site keeps one
 * automatic slot and lowers its initializer at every evaluation before
 * producing that slot's address. Aggregate compound literals build a separate
 * staging object, then COPY_OBJECT replaces the persistent object's complete
 * representation after every explicit initializer expression has run.
 * ZERO_OBJECT consumes an aggregate address of input_type and performs
 * semantic zero initialization for the complete object named by type.
 * COPY_STRING consumes a character-array address and copies the exact bytes
 * retained by its semantic initializer. The enclosing automatic initializer
 * first zeroes the complete destination, so array elements beyond the copied
 * bytes retain C's implicit zero initialization.
 * ELEMENT_ADDRESS consumes a fixed-array address and produces one direct
 * element address. COPY_OBJECT consumes destination and source aggregate
 * addresses. Automatic array and structure
 * initializer lists zero the complete object once, then evaluate represented
 * scalar or supported structure expression leaves in source order and store
 * them through ELEMENT_ADDRESS and MEMBER_ADDRESS paths. DUPLICATE_ADDRESS
 * preserves an address while a supported integer or pointer compound
 * assignment or update loads and stores the object. This evaluates the
 * destination once. Integer mutation supports
 * non-Boolean scalar objects that occupy one, two, or four bytes. Narrow
 * values are promoted for the 32-bit computation and converted back before
 * an exact-width store. Compound assignments retain integer-promotion, usual
 * arithmetic, and assignment conversions. Pointer compound assignments and
 * updates use POINTER_BINARY with a complete-object stride.
 * Prefix updates produce the stored value. Postfix updates produce the value
 * from before the store.
 * MEMBER_ADDRESS consumes a record address and pushes the selected complete,
 * direct, non-bit-field member address. BIT_FIELD_LOAD consumes a record
 * address and pushes the selected field's extracted integer value.
 * DEREFERENCE consumes a pointer value and pushes the referenced object
 * address or function designator. It emits no target instruction because
 * both forms occupy one 32-bit machine word, while the public IR keeps their
 * meanings distinct. ADDRESS_OF performs the inverse transition for an
 * object or function designator. FUNCTION_ADDRESS publishes a linked function
 * designator, and FUNCTION_TO_POINTER records its value conversion.
 * ARRAY_TO_POINTER consumes an array address and produces the address of its
 * first element without emitting a target instruction. POINTER_BINARY scales
 * an integer operand by the pointed-to object size. Pointer subtraction
 * divides the byte difference by that size and produces signed int.
 * LOAD consumes an object address. A scalar load pushes the loaded value. A
 * structure load snapshots the complete object and pushes one handle to that
 * snapshot. STORE consumes the value on top of the stack and the destination
 * address below it, without producing a result. Structure STORE copies the
 * complete object. STORE_VALUE consumes the same pair and pushes the stored
 * scalar value or preserved structure snapshot. DISCARD consumes one value.
 * An explicit cast to void evaluates its operand once and produces no result.
 * A represented integer, object pointer, function pointer, or supported
 * structure operand emits DISCARD. A void operand leaves the abstract stack at
 * its incoming depth and emits no extra instruction.
 * CALL_DIRECT consumes its arguments after they have been evaluated in source
 * order. CALL_INDIRECT also consumes the function pointer below those
 * arguments. The instruction retains the actual argument count, including
 * variadic and unprototyped arguments after the frontend applies the
 * represented default promotions. Argument zero is deepest among the
 * arguments, and the final argument is on top. Each argument occupies one
 * abstract stack entry. The i386 emitter gives scalar arguments one four-byte
 * slot and copies named structure arguments inline, rounded up to four bytes.
 * Arguments without declared parameter types are limited to represented
 * four-byte scalar values. A structure result uses a hidden pointer before the
 * explicit arguments. The callee copies into that
 * storage, returns its address in EAX, and removes the hidden pointer with
 * RET 4. Either call pushes one result unless its result type is void. Narrow
 * caller and callee results are normalized from the declared AL or AX lane.
 * Supported structure values are complete, nonvolatile, nonatomic structures
 * whose alignment does not exceed four bytes. Structure RETURN_VALUE copies
 * into the caller-provided result object.
 * Failure zeros the result, rewinds allocations made during the operation,
 * and keeps its structured diagnostic in the job. */

#endif
