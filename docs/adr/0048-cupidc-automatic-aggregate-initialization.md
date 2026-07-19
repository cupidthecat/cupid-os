# Lower automatic aggregate initializer lists in hosted CupidC

## Context

ADR 0014 publishes automatic array and structure initializer lists as an immutable forest. Direct edges retain source order and name one array element or structure member. Omitted subobjects have no edge and therefore remain implicit zero. ADR 0044 gives referenced fixed automatic arrays and records deterministic EBP-relative storage, but hosted IR lowering still rejected every initializer forest attached to that storage.

Active Toolchain source requires this path without changing its declarations. `toolchain/cupidc_pp.c` initializes `ctool_string_t no_name` with a pointer and an integer. `toolchain/cupidc_frontend.c` contains seven automatic declarations of the form `{0}`, including `ctool_c_type_node_t node = {0}`. These are ordinary C initializer lists, not a reason to replace aggregates with scalar temporaries.

The existing IR can already name an automatic object with `LOCAL_ADDRESS`, select a structure member with `MEMBER_ADDRESS`, lower represented scalar expressions, and store those values. It lacked an operation for the aggregate's implicit-zero rule and an address operation that retained a direct fixed-array selector.

## Decision

`ctool_c_lower_ir` accepts a `LIST` root for a complete fixed automatic array or complete structure that fits the ADR 0044 frame policy. Before publishing instructions, it validates the complete inline object graph and its target layouts. Qualified and aligned wrappers must preserve the represented object. Arrays must have a fixed nonzero bound and consistent element layout. Structure members must fit their record layout. The walk stops at pointer referents, so the pointed-to type does not become part of the automatic object. Union and Cupid class lists remain unsupported.

The supported object cannot contain a `volatile` or `_Atomic` subobject. This is a deliberate boundary. Bulk initialization would otherwise erase the access semantics that those qualifiers require. Omitted bit fields and omitted wide or floating scalar members may receive the aggregate's semantic zero. Explicit leaves still have to pass the represented runtime-value path.

Every accepted initializer starts with `LOCAL_ADDRESS` followed by `ZERO_OBJECT`. `ZERO_OBJECT.type` and `input_type` both name the complete aggregate. The instruction consumes that address and semantically initializes the whole object to zero. It does not expose a byte pattern as the public C rule.

Lowering then walks explicit initializer edges in source order. It rebuilds the path from the root object for each leaf, using `MEMBER_ADDRESS` for a direct structure member and the new `ELEMENT_ADDRESS` for a direct fixed-array element. `ELEMENT_ADDRESS.input_type` names the array, `type` names its element, and `reference` is the checked element index. Frame offsets and byte offsets remain private to the i386 emitter.

An explicit leaf must be a represented scalar `EXPRESSION`. The frontend's conversions stay in the expression tree, and `STORE` retains the final destination type. This preserves source-order side effects even when direct designators select subobjects out of layout order. Automatic string leaves, explicit bit-field leaves, record-valued expression leaves, wide or floating expression leaves, and atomic expression leaves remain unsupported. Whole-record expression initialization and aggregate assignment also remain outside this decision.

The i386 emitter realizes `ZERO_OBJECT` by popping the destination, preserving EDI, moving the destination to EDI, clearing EAX, loading the complete object size into ECX, issuing `CLD`, and emitting `REP STOSB`. It then restores EDI. This sequence establishes the direction flag before the string operation and preserves the cdecl callee-saved register. The current supported i386 object representations use zero bytes for semantic zero, but the public IR keeps the semantic rule so another target can choose a different implementation.

`ELEMENT_ADDRESS` pops the array address, adds the checked element-size multiple, and pushes the selected element address. The emitter rechecks the fixed bound, element identity, target sizes, and range before writing code.

The operation remains transactional. Invalid ownership, selectors, type identities, or layouts produce the invalid-unit diagnostic. Unsupported leaf and qualifier boundaries produce the unsupported-type diagnostic. Either result publishes no partial IR or object and rewinds operation storage.

Expanding every omitted subobject into frontend initializer records was rejected. The forest deliberately records only explicit clauses, and eager expansion would duplicate target layout work. Zeroing only named scalar leaves was also rejected because it would miss padding, omitted members, and omitted bit fields. Reusing pointer arithmetic for direct array selectors was rejected because it would discard the selector identity and duplicate integer-expression machinery. Static initializer serialization was not reused because automatic leaves can call functions and must write into the current stack frame at runtime.

## Consequences and evidence

The focused IR fixture publishes 65 exact instructions with a maximum abstract-stack depth of three. It covers the active `no_name` shape, a nested structure and array with direct designators, a narrow integer leaf, source-order stores, and implicit zero for omitted elements. A separate structure proves that one `ZERO_OBJECT` plus one explicit store also initializes omitted bit fields and an omitted 64-bit member. Mutated selectors and array layouts fail transactionally, while an explicit 64-bit leaf keeps the unsupported-type boundary.

The object fixture emits two functions. `aggregate_paths` initializes a 40-byte object, evaluates four calls in source order, and stores them at the selected member and element offsets. `active_zero_record` initializes the active 16-byte `{0}` structure shape. The object has seven symbols and four source-ordered `R_386_PC32` call relocations. A decoder-driven symbolic executor checks the complete zero extent, call results and order, selected store values and offsets, the EDI save and restore around zeroing, and byte-identical repeated emission.

Useful object negatives retain automatic string leaves, explicit bit-field leaves, and record-valued expression leaves as unsupported. The semantic contract also rejects a reachable by-value type cycle and a nonzero volatile bit field while accepting a volatile zero-width separator because it creates no stored subobject. The final focused Python IR and object run passes all 36 tests in 21.260 seconds.

The active-source guards pin unchanged declarations in `cupidc_pp.c` and `cupidc_frontend.c`. This is hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, ELF32, and contracts. The host compiler still produces the normal root and user C objects, while the private in-kernel CupidC compiler remains the runtime JIT and AOT path. No production artifact, ABI owner, boot path, or host dependency changes here.

Issue #25 remains open for aggregate values and assignment, the deferred initializer leaves, Boolean mutation, narrow bit fields, atomic access, 64-bit and floating runtime values, variadic calls, call-site alignment, production integration, staged self-hosting, and the fixed-point bootstrap.

## Extension: structure-valued leaves

Supported structure-valued expression leaves later gained runtime lowering. The initializer still zeros the complete root object first and rebuilds each explicit direct-subobject path in source order. A structure leaf evaluates to an instruction-owned snapshot, then the existing `STORE` instruction copies the complete object into the selected destination. Whole-record expression initialization uses the same copy path without `ZERO_OBJECT`, and `STORE_VALUE` supplies value-preserving structure assignment. The supported object must be a complete structure with alignment no greater than four and no stored `volatile` or `_Atomic` subobject. Automatic strings, explicit bit-field leaves, unions, Cupid classes, array values, over-aligned objects, and wide or floating scalar expression leaves remain deferred. This extension changes hosted capability only.
