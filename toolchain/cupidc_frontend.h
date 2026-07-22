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
   * dual location. Linkage describes the shared entity. Type holds the
   * file-scope composite when the name is visible there, or the compatibility
   * accumulator for an entity known only through block declarations. A
   * composite type is symbol-local and never mutates a shared typedef graph. */
  ctool_c_storage_class_t storage;
  ctool_c_linkage_t linkage;
  /* False means the linked entity was introduced by a block declaration and
   * has no ordinary name at file scope. */
  ctool_bool file_scope_visible;
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
  /* Block-scope declarations retain their source storage spelling.
   * Enumerators always use NONE. */
  ctool_c_storage_class_t storage;
  ctool_u32 type;
  /* An extern object or block function aliases this canonical linked binding.
   * Other block bindings use AST_NONE because their identity is local. */
  ctool_u32 linkage_binding;
  /* Root in translation_unit.initializers. A typedef or uninitialized
   * automatic object uses AST_NONE. An initialized automatic object may own
   * an EXPRESSION, STRING, or LIST forest; a static object always owns its
   * semantic forest, including implicit zero initialization. */
  ctool_u32 initializer;
  /* ENUMERATOR only: evaluated target bit pattern and unsignedness. */
  ctool_u64 integer_bits;
  ctool_bool integer_unsigned;
  /* ENUMERATOR only: a type-name definition names the expression or
   * initializer that activates it. Declaration and function-prefix
   * enumerators use AST_NONE for both fields. */
  ctool_u32 activation_expression;
  ctool_u32 activation_initializer;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_block_binding_t;

typedef struct {
  /* Function-scope label identity. LABEL and GOTO statements refer to this
   * table instead of repeating or resolving source spellings later. */
  ctool_string_t name;
  /* Defining LABEL statement. The statement may follow an earlier GOTO. */
  ctool_u32 statement;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_label_t;

typedef enum {
  CTOOL_C_INITIALIZER_EXPRESSION = 1,
  CTOOL_C_INITIALIZER_ZERO,
  CTOOL_C_INITIALIZER_INTEGER,
  CTOOL_C_INITIALIZER_STRING,
  CTOOL_C_INITIALIZER_ADDRESS,
  CTOOL_C_INITIALIZER_LIST
} ctool_c_initializer_kind_t;

typedef enum {
  CTOOL_C_INITIALIZER_ADDRESS_NONE = 0,
  CTOOL_C_INITIALIZER_ADDRESS_STRING,
  CTOOL_C_INITIALIZER_ADDRESS_BINDING
} ctool_c_initializer_address_kind_t;

typedef struct {
  ctool_c_initializer_kind_t kind;
  /* Destination object or subobject type after initializer conversion and
   * any array-bound completion. */
  ctool_u32 type;
  /* First value token, an explicit list's opening brace, or the object name
   * for implicit zero initialization. */
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
  /* EXPRESSION: converted runtime expression root. It may be an automatic
   * object's root or a leaf in an automatic LIST. Other kinds use AST_NONE. */
  ctool_u32 expression;
  /* INTEGER: target-width converted value bits. */
  ctool_u64 integer_bits;
  /* STRING: effective character bytes copied into the target array. The
   * target type supplies any remaining semantic zero initialization.
   * ADDRESS with a STRING base: bytes of the static literal object,
   * including its terminator. */
  ctool_bytes_t string_bytes;
  /* ADDRESS: semantic base and signed i386 target-byte addend. The
   * destination pointer type above retains the initializer conversion. */
  ctool_c_initializer_address_kind_t address_kind;
  /* STRING bases use AST_NONE. BINDING bases use a stable file-binding
   * index. */
  ctool_u32 address_reference;
  ctool_i32 address_addend;
  /* LIST: slice in translation_unit.initializer_elements. Edges name direct,
   * explicitly initialized subobjects, and every referenced initializer
   * precedes this node. Omitted subobjects are implicitly zero initialized.
   * Other kinds use AST_NONE and zero. */
  ctool_u32 first_element;
  ctool_u32 element_count;
  /* Enumerators introduced by this initializer's designator. They become
   * visible before the initializer subtree is visited. Initializers without
   * a slice use AST_NONE and zero. */
  ctool_u32 first_block_binding;
  ctool_u32 block_binding_count;
} ctool_c_initializer_t;

typedef struct {
  /* ARRAY parent: element index. RECORD parent: absolute graph member index. */
  ctool_u32 subobject;
  /* Root initializer for this subobject. */
  ctool_u32 initializer;
} ctool_c_initializer_element_t;

#define CTOOL_C_AST_NONE 0xffffffffu

typedef enum {
  CTOOL_C_STATEMENT_COMPOUND = 1,
  CTOOL_C_STATEMENT_EXPRESSION,
  CTOOL_C_STATEMENT_DECLARATION,
  CTOOL_C_STATEMENT_RETURN,
  CTOOL_C_STATEMENT_FOR,
  CTOOL_C_STATEMENT_BREAK,
  CTOOL_C_STATEMENT_CONTINUE,
  CTOOL_C_STATEMENT_IF,
  CTOOL_C_STATEMENT_WHILE,
  CTOOL_C_STATEMENT_SWITCH,
  CTOOL_C_STATEMENT_CASE,
  CTOOL_C_STATEMENT_DEFAULT,
  CTOOL_C_STATEMENT_DO,
  CTOOL_C_STATEMENT_LABEL,
  CTOOL_C_STATEMENT_GOTO
} ctool_c_statement_kind_t;

typedef struct {
  ctool_c_statement_kind_t kind;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;

  /* COMPOUND: ordered slice of translation_unit.statement_children. */
  ctool_u32 first_child;
  ctool_u32 child_count;
  /* EXPRESSION: index into translation_unit.expressions, or AST_NONE for the
   * null statement `;`.
   * RETURN: returned expression, or AST_NONE for `return;`.
   * CASE: folded integer constant converted to the promoted type of its
   * enclosing SWITCH condition. */
  ctool_u32 expression;
  /* DECLARATION: ordered slice of translation_unit.block_bindings. */
  ctool_u32 first_block_binding;
  ctool_u32 block_binding_count;
  /* LABEL/GOTO: canonical index into translation_unit.labels. */
  ctool_u32 label;
  /* IF: required controlling expression and body statement, plus optional
   * else_body statement. The body statements precede this node.
   * WHILE/DO: required controlling expression and body statement. DO retains
   * the post-test form; its body is parsed before its condition.
   * FOR: optional EXPRESSION/DECLARATION initializer statement, optional
   * controlling expression, optional iteration expression, and required body
   * statement. Omitted clauses use AST_NONE; an omitted condition denotes
   * C's nonzero constant. The initializer and body precede this node.
   * SWITCH: required promoted integer condition and body statement.
   * CASE/DEFAULT: required labeled body statement. CASE also uses expression
   * for its converted constant value. Nested labels precede their parents.
   * BREAK/CONTINUE are targetless leaves; lowering resolves their nearest
   * enclosing control target. */
  ctool_u32 initializer_statement;
  ctool_u32 condition;
  ctool_u32 iteration;
  ctool_u32 body;
  ctool_u32 else_body;
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
  CTOOL_C_EXPRESSION_UPDATE,
  CTOOL_C_EXPRESSION_CAST,
  CTOOL_C_EXPRESSION_MEMBER,
  CTOOL_C_EXPRESSION_CONDITIONAL,
  CTOOL_C_EXPRESSION_COMPOUND_LITERAL,
  CTOOL_C_EXPRESSION_VARIADIC_START,
  CTOOL_C_EXPRESSION_VARIADIC_ARGUMENT,
  CTOOL_C_EXPRESSION_VARIADIC_COPY,
  CTOOL_C_EXPRESSION_VARIADIC_END
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
  CTOOL_C_EXPRESSION_OPERATOR_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_DIVIDE_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_REMAINDER_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_ADD_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR_ASSIGN,
  CTOOL_C_EXPRESSION_OPERATOR_PREFIX_INCREMENT,
  CTOOL_C_EXPRESSION_OPERATOR_PREFIX_DECREMENT,
  CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_INCREMENT,
  CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_DECREMENT
} ctool_c_expression_operator_t;

typedef enum {
  CTOOL_C_CONVERSION_NONE = 0,
  CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
  CTOOL_C_CONVERSION_ARRAY_TO_POINTER,
  CTOOL_C_CONVERSION_FUNCTION_TO_POINTER,
  CTOOL_C_CONVERSION_QUALIFICATION,
  CTOOL_C_CONVERSION_INTEGER_PROMOTION,
  CTOOL_C_CONVERSION_USUAL_ARITHMETIC,
  CTOOL_C_CONVERSION_ASSIGNMENT,
  /* Object pointer converted to or from a qualified or unqualified void
   * pointer for equality or assignment conversion. */
  CTOOL_C_CONVERSION_POINTER,
  /* C11 null pointer constant converted to its destination pointer type. */
  CTOOL_C_CONVERSION_NULL_POINTER,
  /* `float` converted to `double` by the default argument promotions. */
  CTOOL_C_CONVERSION_FLOAT_PROMOTION
} ctool_c_conversion_kind_t;

typedef struct {
  ctool_c_expression_kind_t kind;
  /* Type produced by this node. A conversion node carries the result type;
   * its child retains the source-semantic type. */
  ctool_u32 type;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;

  /* IDENTIFIER: file-binding index. PARAMETER: parameter index.
   * BLOCK_BINDING: block-binding index. MEMBER: direct graph-member index.
   * COMPOUND_LITERAL: initializer-root index for the unnamed automatic
   * object. The expression's own absolute index is that object's identity. */
  ctool_u32 reference;
  /* CALL: ordered slice of expression_children, callee first.
   * CONDITIONAL: condition, selected-when-nonzero, selected-when-zero.
   * VARIADIC_START: cursor lvalue, then final named parameter.
   * VARIADIC_ARGUMENT/VARIADIC_END: cursor lvalue.
   * VARIADIC_COPY: destination cursor lvalue, then converted source cursor.
   * IMPLICIT_CONVERSION/CAST/MEMBER/UNARY/UPDATE: one source-expression
   * child. COMPOUND_LITERAL has no expression children because its
   * initializer owns a separate postorder forest. */
  ctool_u32 first_child;
  ctool_u32 child_count;
  /* IMPLICIT_CONVERSION: exact semantic conversion applied to one child. */
  ctool_c_conversion_kind_t conversion;
  /* UNARY/BINARY/ASSIGNMENT/UPDATE: represented source operator. */
  ctool_c_expression_operator_t operation;
  /* ASSIGNMENT/UPDATE: arithmetic computation type; plain `=` uses result
   * type. Other expression kinds use CTOOL_C_TYPE_NONE. */
  ctool_u32 computation_type;
  /* INTEGER_CONSTANT: target-width constant bit pattern; type carries
   * rank/sign. This includes target-folded non-VLA layout queries. */
  ctool_u64 integer_bits;
  /* STRING: decoded target bytes including the trailing null byte. */
  ctool_bytes_t string_bytes;
  /* Enumerators introduced by a type name in this expression. The slice
   * activates after block_binding_child_offset direct children have been
   * visited in source order. Expressions without a slice use AST_NONE, zero,
   * and zero. */
  ctool_u32 first_block_binding;
  ctool_u32 block_binding_count;
  ctool_u32 block_binding_child_offset;
} ctool_c_expression_t;

typedef struct {
  /* Canonical function entity in translation_unit.bindings. */
  ctool_u32 binding;
  /* Exact definition declarator type and definition-local source spelling. */
  ctool_u32 declared_type;
  ctool_c_storage_class_t storage;
  ctool_u32 function_declaration_flags;
  /* Enumerators introduced by the definition's parameter list. This prefix
   * enters scope before the outer compound statement and remains visible
   * through that statement. Body declarations own later binding slices. */
  ctool_u32 first_block_binding;
  ctool_u32 block_binding_count;
  /* Function-scope slice in translation_unit.labels. Entries are ordered by
   * first definition or goto reference in this definition. */
  ctool_u32 first_label;
  ctool_u32 label_count;
  /* COMPOUND statement index. */
  ctool_u32 body;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_function_definition_t;

typedef enum {
  CTOOL_C_OBJECT_DEFINITION_EXPLICIT = 1,
  CTOOL_C_OBJECT_DEFINITION_TENTATIVE
} ctool_c_object_definition_kind_t;

typedef struct {
  /* Canonical file-scope object entity in translation_unit.bindings. */
  ctool_u32 binding;
  /* Definition-local type and storage spelling. A finalized tentative
   * definition uses the canonical composite type at translation-unit end. */
  ctool_u32 declared_type;
  ctool_c_storage_class_t storage;
  ctool_c_object_definition_kind_t kind;
  /* Root of one static-duration semantic initializer forest. */
  ctool_u32 initializer;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_object_definition_t;

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
  /* One finalized record for each file-scope object definition. Explicit and
   * tentative definitions own static-duration initializer forests. */
  const ctool_c_object_definition_t *object_definitions;
  ctool_u32 object_definition_count;
  /* Source-ordered block bindings survive after their lexical scopes close. */
  const ctool_c_block_binding_t *block_bindings;
  ctool_u32 block_binding_count;
  /* Semantic object initializer forests. Storage duration comes from the
   * owning file-object definition, block binding, or compound-literal
   * expression. LIST nodes may contain runtime EXPRESSION leaves only under
   * an automatic owner. */
  const ctool_c_initializer_t *initializers;
  ctool_u32 initializer_count;
  /* Direct LIST edges. Nested lists retain independent slices, and those
   * slices concatenate in LIST postorder. */
  const ctool_c_initializer_element_t *initializer_elements;
  ctool_u32 initializer_element_count;
  /* Canonical function-scope labels. Function definitions own disjoint,
   * source-ordered slices, and statements refer to entries by index. A GOTO
   * reference is a semantic cross-reference, not a postorder child, so its
   * defining LABEL statement may appear before or after the jump. */
  const ctool_c_label_t *labels;
  ctool_u32 label_count;
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
 * tags, parameter metadata, object initializers, function definitions, and
 * typed body AST tables.
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
 * parameter storage, and represents compound, expression, declaration,
 * scalar or aggregate return, `if`/`else`, counted `for`, `while`, `do`,
 * `switch`, `case`, `default`, `break`, and `continue` statements;
 * function-scope identifier labels and direct `goto` statements through one
 * canonical label table per definition;
 * automatic/register block-object bindings with converted scalar or
 * whole-object record expressions, narrow character-array strings, and
 * recursive array or structure lists with direct designators whose leaves
 * retain runtime scalar or compatible record-valued assignment expressions;
 * block-scope compound literals whose lvalues name one automatic object per
 * source site. The initializer runs at every evaluation, and fixed scalar,
 * array, or structure types use the same automatic initializer forms;
 * block-scope static objects with implicit or explicit pointer zero,
 * target-converted integer constants, narrow character-array strings,
 * direct narrow-string addresses or addresses of linked file objects and
 * functions, and matching recursive lists; file-scope object definitions
 * with exact declaration storage and type, explicit or finalized tentative
 * ownership, and the same semantic static initializer forest.
 * Lists retain explicit subobjects in postorder, apply brace elision, leave
 * omitted tails implicitly zero initialized, and complete direct
 * unknown-bound arrays without mutating shared typedefs;
 * file/block/parameter references, target-typed integer and ordinary narrow
 * character constants, decoded ordinary narrow strings, typed scalar/void
 * casts, address/dereference, direct/promoted record-member expressions,
 * folded non-VLA layout queries, every integer unary and binary precedence
 * tier, pointer addition/subtraction and normalized subscripting with primary
 * or C11 digraph brackets, simple and compound assignment, prefix/postfix
 * increment/decrement, right-associative C11 conditional values including
 * same-record results, fixed-argument prototyped calls; and explicit lvalue,
 * array, function, qualification, integer promotion, usual-arithmetic, and
 * assignment and null-pointer conversions. Block bindings use lexical scope,
 * share the outer function-body scope with definition parameters, and retain
 * stable public indices after their scopes close. Declaration-position enums
 * retain each enumerator's value and type in that same binding stream. Enum
 * definitions in record members, function-definition parameter lists, and
 * block type names use the same stream. Function prefixes and expression or
 * initializer activation records preserve the exact point where each
 * enumerator becomes visible. Lvalue conversion removes top-level const,
 * volatile, and atomic qualification while retaining the qualified source
 * node. Definition parameter metadata retains
 * its adjusted qualified object type separately from normalized function-type
 * parameters. An empty identifier-list definition retains its non-prototype
 * function type and has no parameters; nonempty old-style identifier lists
 * remain an explicit boundary. Calls apply assignment conversion to declared
 * parameters and default argument promotions to ellipsis arguments or every
 * argument passed without a prototype. Exact `float` and `double` values can
 * cross assignments, initializers, named calls, and returns. An unnamed
 * `float` argument is promoted to `double`; an unnamed `double` value and
 * `va_arg(arguments, double)` are also represented. Runtime integer
 * expressions are typed without constant folding. Unevaluated query
 * operands are type-checked through the same grammar and leave no public AST
 * nodes. Valid floating arithmetic compounds and updates remain explicit
 * deferred features, while integer-only floating compounds and aggregate
 * compound/update operands are constraint errors. Block-scope assertions
 * remain pending and fail closed.
 * C11 `inline` is also retained as a canonical OR-summary across compatible
 * declarations; external-inline classification remains translation-unit
 * finalization policy. `_Thread_local`, `_Noreturn`, `_Alignas`,
 * `_Atomic(type-name)`, and complex/imaginary type specifiers are pending and
 * fail closed rather than being skipped.
 * Declaration/member/namespace counts otherwise consume checked job storage
 * rather than fixed frontend tables. Block-scope struct and union tags obey
 * lexical scope, including forward declarations, same-scope completion, and
 * nested shadowing. Tags introduced by a function definition's parameter list
 * share the outer body scope and expire after that definition. Tag-only
 * declarations may carry a represented storage-class specifier or type
 * qualifier when they introduce a tag, remain declaration statements, and
 * create no runtime binding. An empty declaration with storage or type
 * qualification cannot merely name a visible tag. A `for` declaration may use
 * a visible record or an anonymous record for its object, but cannot introduce
 * a named tag or omit the object. An anonymous enum may declare a `for` object
 * and its enumerators share the loop scope. A visible enum tag can be used in
 * a block type name or record member, and a new enum definition is represented
 * in either context. Block declaration attributes, union or Cupid class
 * initializer lists, and static assertions remain explicit boundaries.
 * Chained, promoted, or overriding designators, union lists, arithmetic and
 * casts on static addresses, floating constants, static-data allocation, and
 * relocation lowering remain pending. Comma
 * expressions, floating arithmetic and non-void floating conversions,
 * universal-character/non-ordinary literals, `long double` call values,
 * non-scalar arguments without declared parameter types, code generation,
 * object emission, and Cupid #exe execution remain later frontend work and are
 * diagnosed rather than skipped. */

#endif
