#ifndef CUPID_TOOLCHAIN_CUPIDC_FRONTEND_H
#define CUPID_TOOLCHAIN_CUPIDC_FRONTEND_H

#include "cupidc_type.h"

/* Deterministic translation limit for recursively nested declaration syntax.
 * It exceeds the C11 minimum translation limits and converts adversarial
 * nesting into a transactional diagnostic instead of a host-stack failure. */
#define CTOOL_C_PARSE_NESTING_LIMIT 256u

typedef struct {
  ctool_c_pp_mode_t mode;
  ctool_bool gnu_extensions;
} ctool_c_parse_request_t;

typedef enum {
  CTOOL_C_BINDING_TYPEDEF = 1,
  CTOOL_C_BINDING_OBJECT,
  CTOOL_C_BINDING_FUNCTION,
  CTOOL_C_BINDING_ENUMERATOR
} ctool_c_binding_kind_t;

typedef enum {
  CTOOL_C_STORAGE_NONE = 0,
  CTOOL_C_STORAGE_TYPEDEF,
  CTOOL_C_STORAGE_EXTERN,
  CTOOL_C_STORAGE_STATIC,
  CTOOL_C_STORAGE_AUTO,
  CTOOL_C_STORAGE_REGISTER
} ctool_c_storage_class_t;

typedef enum {
  CTOOL_C_LINKAGE_NONE = 0,
  CTOOL_C_LINKAGE_INTERNAL,
  CTOOL_C_LINKAGE_EXTERNAL
} ctool_c_linkage_t;

#define CTOOL_C_DECL_ATTR_NORETURN 0x00000001u
#define CTOOL_C_DECL_ATTR_ALL CTOOL_C_DECL_ATTR_NORETURN

#define CTOOL_C_FUNCTION_DECL_INLINE 0x00000001u
#define CTOOL_C_FUNCTION_DECL_ALL CTOOL_C_FUNCTION_DECL_INLINE

typedef struct {
  ctool_string_t name;
  ctool_c_binding_kind_t kind;
  /* Canonical bindings retain the first declaration's storage spelling and
   * dual location. Linkage and type describe the merged entity; a composite
   * type is symbol-local and never mutates a shared typedef graph. */
  ctool_c_storage_class_t storage;
  ctool_c_linkage_t linkage;
  /* Semantically retained declaration attributes. Noreturn belongs to the
   * canonical function entity and merges across compatible declarations. */
  ctool_u32 attributes;
  /* OR-summary of standard function-declaration specifiers seen across
   * compatible declarations. Definition-local inline/storage spelling must
   * be retained separately when function definitions are represented. */
  ctool_u32 function_declaration_flags;
  /* Minimum object/function entity alignment. Zero selects the declared
   * type's alignment; exact typedef/type alignment uses an ALIGNED node. */
  ctool_u32 minimum_alignment;
  ctool_u32 type;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
  /* ENUMERATOR only: target integer bit pattern and evaluated unsignedness. */
  ctool_u64 integer_bits;
  ctool_bool integer_unsigned;
} ctool_c_binding_t;

typedef struct {
  ctool_string_t name;
  ctool_u32 type;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_tag_t;

typedef struct {
  ctool_string_t name;
  /* Definition parameters retain their source storage spelling. */
  ctool_c_storage_class_t storage;
  /* The adjusted parameter object type retains definition-local top-level
   * qualification. The parallel graph.parameter_types entry is the
   * unqualified type used by function-type compatibility. */
  ctool_u32 type;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_parameter_t;

typedef struct {
  ctool_string_t name;
  ctool_c_binding_kind_t kind;
  /* Block-scope declarations retain their source storage spelling. */
  ctool_c_storage_class_t storage;
  ctool_u32 type;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_block_binding_t;

#define CTOOL_C_AST_NONE 0xffffffffu

typedef enum {
  CTOOL_C_STATEMENT_COMPOUND = 1,
  CTOOL_C_STATEMENT_EXPRESSION,
  CTOOL_C_STATEMENT_DECLARATION,
  CTOOL_C_STATEMENT_RETURN
} ctool_c_statement_kind_t;

typedef struct {
  ctool_c_statement_kind_t kind;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;

  /* COMPOUND: ordered slice of translation_unit.statement_children. */
  ctool_u32 first_child;
  ctool_u32 child_count;
  /* EXPRESSION: index into translation_unit.expressions.
   * RETURN: returned expression, or AST_NONE for `return;`. */
  ctool_u32 expression;
  /* DECLARATION: ordered slice of translation_unit.block_bindings. */
  ctool_u32 first_block_binding;
  ctool_u32 block_binding_count;
} ctool_c_statement_t;

typedef enum {
  CTOOL_C_EXPRESSION_IDENTIFIER = 1,
  CTOOL_C_EXPRESSION_PARAMETER,
  CTOOL_C_EXPRESSION_STRING,
  CTOOL_C_EXPRESSION_CALL,
  CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION,
  CTOOL_C_EXPRESSION_BLOCK_BINDING,
  CTOOL_C_EXPRESSION_INTEGER_CONSTANT,
  CTOOL_C_EXPRESSION_UNARY,
  CTOOL_C_EXPRESSION_BINARY,
  CTOOL_C_EXPRESSION_ASSIGNMENT,
  CTOOL_C_EXPRESSION_CAST,
  CTOOL_C_EXPRESSION_MEMBER
} ctool_c_expression_kind_t;

typedef enum {
  CTOOL_C_EXPRESSION_OPERATOR_NONE = 0,
  CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS,
  CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE,
  CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT,
  CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT,
  CTOOL_C_EXPRESSION_OPERATOR_ADDRESS,
  CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE,
  CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY,
  CTOOL_C_EXPRESSION_OPERATOR_DIVIDE,
  CTOOL_C_EXPRESSION_OPERATOR_REMAINDER,
  CTOOL_C_EXPRESSION_OPERATOR_ADD,
  CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
  CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT,
  CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT,
  CTOOL_C_EXPRESSION_OPERATOR_LESS,
  CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL,
  CTOOL_C_EXPRESSION_OPERATOR_GREATER,
  CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL,
  CTOOL_C_EXPRESSION_OPERATOR_EQUAL,
  CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL,
  CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND,
  CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR,
  CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR,
  CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_AND,
  CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_OR,
  CTOOL_C_EXPRESSION_OPERATOR_ASSIGN
} ctool_c_expression_operator_t;

typedef enum {
  CTOOL_C_CONVERSION_NONE = 0,
  CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
  CTOOL_C_CONVERSION_ARRAY_TO_POINTER,
  CTOOL_C_CONVERSION_FUNCTION_TO_POINTER,
  CTOOL_C_CONVERSION_QUALIFICATION,
  CTOOL_C_CONVERSION_INTEGER_PROMOTION,
  CTOOL_C_CONVERSION_USUAL_ARITHMETIC,
  CTOOL_C_CONVERSION_ASSIGNMENT
} ctool_c_conversion_kind_t;

typedef struct {
  ctool_c_expression_kind_t kind;
  /* Type produced by this node. A conversion node carries the result type;
   * its child retains the source-semantic type. */
  ctool_u32 type;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;

  /* IDENTIFIER: file-binding index. PARAMETER: parameter index.
   * BLOCK_BINDING: block-binding index. MEMBER: direct graph-member index. */
  ctool_u32 reference;
  /* CALL: ordered slice of expression_children, callee first.
   * IMPLICIT_CONVERSION/CAST/MEMBER/UNARY: one source-expression child. */
  ctool_u32 first_child;
  ctool_u32 child_count;
  /* IMPLICIT_CONVERSION: exact semantic conversion applied to one child. */
  ctool_c_conversion_kind_t conversion;
  /* UNARY/BINARY/ASSIGNMENT: represented source operator. */
  ctool_c_expression_operator_t operation;
  /* ASSIGNMENT: arithmetic computation type; plain `=` uses result type.
   * Other expression kinds use CTOOL_C_TYPE_NONE. */
  ctool_u32 computation_type;
  /* INTEGER_CONSTANT: target-width constant bit pattern; type carries
   * rank/sign. This includes target-folded non-VLA layout queries. */
  ctool_u64 integer_bits;
  /* STRING: decoded target bytes including the trailing null byte. */
  ctool_bytes_t string_bytes;
} ctool_c_expression_t;

typedef struct {
  /* Canonical function entity in translation_unit.bindings. */
  ctool_u32 binding;
  /* Exact definition declarator type and definition-local source spelling. */
  ctool_u32 declared_type;
  ctool_c_storage_class_t storage;
  ctool_u32 function_declaration_flags;
  /* COMPOUND statement index. */
  ctool_u32 body;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_function_definition_t;

typedef struct {
  ctool_c_layout_request_t graph;
  ctool_c_layout_result_t layout;
  const ctool_c_binding_t *bindings;
  ctool_u32 binding_count;
  const ctool_c_tag_t *tags;
  ctool_u32 tag_count;
  /* Parallel to graph.parameter_types. Function slices index both arrays;
   * parameter metadata also retains the definition-local object type. */
  const ctool_c_parameter_t *parameters;
  ctool_u32 parameter_count;
  /* Source-ordered block bindings survive after their lexical scopes close. */
  const ctool_c_block_binding_t *block_bindings;
  ctool_u32 block_binding_count;
  /* Function bodies are immutable postorder tables. Child indices precede
   * their parents, and every child slice preserves source order. */
  const ctool_c_function_definition_t *function_definitions;
  ctool_u32 function_definition_count;
  const ctool_c_statement_t *statements;
  ctool_u32 statement_count;
  const ctool_u32 *statement_children;
  ctool_u32 statement_child_count;
  const ctool_c_expression_t *expressions;
  ctool_u32 expression_count;
  const ctool_u32 *expression_children;
  ctool_u32 expression_child_count;
} ctool_c_translation_unit_t;

typedef enum {
  CTOOL_C_PARSE_DIAG_INVALID_REQUEST = 0x0b000001u,
  CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN = 0x0b000002u,
  CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS = 0x0b000003u,
  CTOOL_C_PARSE_DIAG_DECLARATOR = 0x0b000004u,
  CTOOL_C_PARSE_DIAG_TYPE_NAME = 0x0b000005u,
  CTOOL_C_PARSE_DIAG_REDEFINITION = 0x0b000006u,
  CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION = 0x0b000007u,
  CTOOL_C_PARSE_DIAG_UNSUPPORTED = 0x0b000008u,
  CTOOL_C_PARSE_DIAG_LIMIT = 0x0b000009u,
  CTOOL_C_PARSE_DIAG_OVERFLOW = 0x0b00000au,
  CTOOL_C_PARSE_DIAG_INTERNAL = 0x0b00000bu,
  CTOOL_C_PARSE_DIAG_ATTRIBUTE = 0x0b00000cu,
  CTOOL_C_PARSE_DIAG_STATIC_ASSERT = 0x0b00000du,
  CTOOL_C_PARSE_DIAG_FUNCTION_DEFINITION = 0x0b00000eu,
  CTOOL_C_PARSE_DIAG_STATEMENT = 0x0b00000fu,
  CTOOL_C_PARSE_DIAG_EXPRESSION = 0x0b000010u
} ctool_c_parse_diag_code_t;

ctool_status_t ctool_c_parse(ctool_job_t *job,
                             const ctool_c_pp_result_t *tape,
                             const ctool_c_parse_request_t *request,
                             ctool_c_translation_unit_t *unit_out);

/* The tape and request are borrowed only for the call. Success publishes one
 * immutable, job-owned semantic graph plus layouts, ordinary bindings, named
 * tags, parameter metadata, function definitions, and typed body AST tables.
 * Presumed and physical locations, decoded string bytes, and every counted
 * name/path are copied. Failure zeros the unit and rewinds all parser
 * allocations while preserving structured diagnostics.
 *
 * The request language settings must match those used to produce the tape;
 * preprocessing provenance does not yet carry that configuration itself.
 * This frontend slice owns the contracted scalar/typedef/storage subset,
 * namespaces, declarators, record/enum definitions, fixed or incomplete
 * arrays, prototypes, compatible file-scope redeclarations, composite array
 * and function types, C linkage, layout, and normalized GNU packed, aligned,
 * and noreturn attributes at their contracted placements. Other attributes
 * fail closed instead of being skipped. Type compatibility uses checked
 * iterative graph walks; the public nesting limit applies to recursive source
 * syntax, not derived-type graph depth. File- and record-scope C11 static
 * assertions validate the shared integer-constant grammar, including target
 * relational/equality conversions, target `sizeof` type/expression queries,
 * standard/GNU alignment queries, and GNU `__builtin_offsetof` member paths
 * for complete objects at that declaration point. Assertions publish no
 * entity, member, or expression node;
 * semantic types constructed by their type names remain in the immutable
 * graph. The initial body AST retains definition-local storage, `inline`, and
 * parameter storage, and represents compound, expression, declaration, and
 * scalar return statements; automatic/register block-object bindings;
 * file/block/parameter references, target-typed integer and ordinary narrow
 * character constants, decoded ordinary narrow strings, typed scalar/void
 * casts, address/dereference, direct/promoted record-member expressions,
 * folded non-VLA layout queries, every integer unary and binary precedence tier,
 * simple assignment, fixed-argument prototyped calls; and explicit lvalue,
 * array, function, qualification, integer
 * promotion, usual-arithmetic, and assignment conversions. Block bindings use
 * lexical scope, share the outer function-body scope with definition
 * parameters, and retain stable public indices after their scopes close. Lvalue
 * conversion removes top-level const, volatile, and atomic qualification while
 * retaining the qualified source node. Definition parameter metadata retains
 * its adjusted qualified object type separately from normalized function-type
 * parameters. Calls currently accept represented scalar assignment
 * conversions or pointer qualification addition; extra variadic arguments
 * fail closed until default argument promotions are represented. Runtime
 * integer expressions are typed without constant folding. Unevaluated query
 * operands are type-checked through the same grammar and leave no public AST
 * nodes. Block-scope assertions remain pending and fail closed.
 * C11 `inline` is also retained as a canonical OR-summary across compatible
 * declarations; external-inline classification remains translation-unit
 * finalization policy. `_Thread_local`, `_Noreturn`, `_Alignas`,
 * `_Atomic(type-name)`, and complex/imaginary type specifiers are pending and
 * fail closed rather than being skipped.
 * Declaration/member/namespace counts otherwise consume checked job storage
 * rather than fixed frontend tables. Block typedefs, static/extern objects,
 * function declarations, block tag specifiers, attributes, initializers,
 * and static assertions remain explicit body boundaries. Control statements
 * other than return, conditional/comma/subscript and compound-assignment
 * operators, pointer arithmetic, floating arithmetic and non-void
 * conversions, universal-character/non-ordinary literals, calls without
 * prototypes, variadic arguments, code generation, object emission, and
 * Cupid #exe execution remain later frontend work and are diagnosed rather
 * than skipped. Tentative-definition state/finalization is not yet published,
 * so incomplete array declarations retain their parsed bounds in this slice. */

#endif
