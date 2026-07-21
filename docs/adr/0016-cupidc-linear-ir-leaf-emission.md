# Lower CupidC leaf functions through typed linear IR

## Context

ADR 0003 calls for a shared linear IR between the typed CupidC frontend and machine-code emission. ADR 0014 publishes the typed function AST, ADR 0015 owns object construction, and ADR 0007 owns x86 encoding. The object path previously stopped whenever a translation unit contained a function definition.

Active source sets the first useful boundary. The unchanged `cemit_add_overflows` helper in `toolchain/cupidc_emit.c` has two 32-bit parameters, an unsigned subtraction and comparison, conditional selection, and a scalar return. Lowering that helper exercises real Toolchain source without rewriting it around a compiler limitation.

## Decision

CupidC exposes one public `ctool_c_lower_ir` operation. It borrows a frozen `ctool_c_translation_unit_t` and publishes immutable function and instruction arrays in the Toolchain job arena. Each function owns one contiguous instruction slice, its exact maximum abstract-stack depth, its binding and declared type, and both presumed and physical source locations. Instructions retain their result and input types, semantic operation or conversion, and source locations. Parameter-address instructions name the frontend's absolute parameter identity. Branches name an instruction relative to the current function.

The abstract stack keeps object addresses distinct from values. This preserves parameter object identity through lvalue conversion and prevents later code generation from treating every integer-shaped item as interchangeable. Each function begins and ends with an empty stack. Both incoming paths at a join must have the same address/value stack shape.

The first ABI slice accepts fixed, nonvariadic cdecl functions with `void` or 32-bit integer results and 32-bit integer parameters. A supported body is either an empty `void` compound statement or a compound statement containing one return. The expression slice covers parameter addresses and loads, 32-bit integer constants, represented implicit integer conversions that preserve the 32-bit value representation, addition, subtraction, signed or unsigned greater-than comparison, and 32-bit conditional selection. It emits value and void returns. An external-linkage definition that retains `inline` stops at a dedicated external-inline finalization diagnostic. Static inline definitions and a non-inline definition preceded by a compatible inline declaration can lower without crossing that deferred policy boundary.

Lowering runs once to count exact records and once to fill them. Failure zeros the public result, rewinds every allocation made by the operation, and keeps its structured diagnostic in the job. The diagnostic distinguishes invalid frozen input, unsupported types, statements, expressions, conversions, or ABI shapes, resource limits, and internal failures.

`ctool_c_emit_object` lowers the functions before it lays out the object. Nonempty `.text` precedes `.rodata`, `.data`, and `.bss`. Defined functions keep source order and receive local or global `STT_FUNC` symbols with exact section-relative offsets and sizes. Function alignment uses x86 NOP bytes.

Every machine instruction goes through `ctool_x86_encode`. The current cdecl frame uses `EBP`, reads parameter `n` at `[EBP + 8 + 4*n]`, and returns scalar values in `EAX`. Signed comparison uses `SETG`; unsigned comparison uses `SETA`. Conditional IR branches use fixed-width relative fields from the shared encoder and are patched to instruction offsets within the same function. They do not need ELF relocations.

Direct AST-to-x86 lowering was rejected because it would recreate the parser/backend coupling that ADR 0003 removes. X86-shaped IR was rejected because register and encoding facts belong to ADR 0007. A block-parameter or SSA graph may become useful for optimization later, but it would add predecessor and join machinery before active source requires it. The typed address/value stack is enough for this slice and keeps the public records straightforward to build with CupidC. Rewriting `cemit_add_overflows` into weaker source was not considered an acceptable workaround.

## Consequences and evidence

The IR contract extracts the exact unchanged helper body and pins its 12 instructions, three-entry maximum stack depth, parameter identities, types, conversions, branch targets, and source locations. It also pins the unchanged `add2` function in `bin/cupidc_test3.cc` as two parameter loads followed by `ADD` and a scalar return. Separate cases cover a constant return, an empty `void` return, a signed-to-unsigned assignment conversion, a static inline definition, and a non-inline definition after an inline declaration. Negative cases cover a malformed body, an unsupported `if` statement, an unsupported left shift, an explicit cast, plain and `extern` external-inline definitions, a 64-bit ABI, a constrained arena, rollback, and same-job recovery.

The object contract emits static data and represented functions in one object. It reads the object through `ctool_elf32_read`, decodes `.text` through `ctool_x86_decode`, and checks every machine byte as well as each mnemonic. Those byte oracles pin the EBP frame, EAX result, constants, parameter displacements 8 and 12, operand order, addition, and both signed and unsigned predicates. The contract also resolves both relative branch targets to the intended conditional arms. It checks section flags and alignment, function bindings and sizes, unchanged data bytes, byte-identical repeat output, external-inline and unsupported-body rollback, and output limits.

This transfers no production build ownership. GCC or Clang still builds the hosted modules and contracts and produces the normal root and user C objects. The private in-kernel CupidC frontend and backend remain the embedded runtime JIT and AOT path. No OS artifact or runtime ABI changed, so this increment has no boot claim.

This first decision left external-inline finalization, explicit casts, calls and `R_386_PC32`, local objects and stack allocation, assignments, general blocks and control statements, labels, pointers, 64-bit integers, floating and aggregate values, broader ABI work, production integration, and self-hosting open. Later extensions close only the parts they name. Issue #25 stays open until the remaining work can carry unchanged active Toolchain functions and then normal OS C cohorts.

## Extension

ADR 0017 extends this interface with fixed direct calls, source-order argument evaluation, cdecl stack-slot placement, and local or external `R_386_PC32` relocations. The open call work above records the boundary when this first leaf decision was made. Indirect and variadic calls, 16-byte call-site alignment, and wider, floating, or aggregate call forms remain open.

The active `add2` function later extended the existing `BINARY` instruction and emitter path with 32-bit integer addition. No public record changed. The emitter pops the right operand into ECX and the left operand into EAX, emits `ADD EAX, ECX` through the shared x86 encoder, and pushes the result for the following return.

The unchanged `canvas_to_screen_x` and `canvas_to_screen_y` functions in `bin/paint.cc` extend `BINARY` with 32-bit integer multiplication. The emitter uses the same operand order and emits `IMUL EAX, ECX` through the shared x86 encoder. The low 32-bit result represents defined signed products and unsigned modulo arithmetic without a separate opcode. The contracts pin both 12-instruction IR slices, both 60-byte functions, and six direct-object relocations. A separate `0x80000001u` fixture pins the unsigned type and one exact 28-byte `IMUL` function. A 64-bit multiplication receives the unsupported-type diagnostic.

ADR 0018 adds absolute block-binding identities, automatic-local addresses, initializer stores, and deterministic fixed EBP slots. It does not make the wider statement, assignment, pointer, or ABI surface complete.

ADR 0019 adds absolute linked file-binding identities through `FILE_ADDRESS`, reuses `LOAD` for four-byte integer objects, and extends `BINARY` with signed or unsigned greater-than-or-equal. The emitter maps direct object addresses to text `R_386_32` relocations without exposing x86 or ELF details in the IR.

ADR 0021 adds graph-member identities through `MEMBER_ADDRESS`. It extends `FILE_ADDRESS` to complete record address roots while keeping record values, bit fields, subscript addresses, and pointer-based addresses outside this slice.

ADR 0022 adds `BIT_FIELD_LOAD` for bit-field reads from represented four-byte storage units. It consumes a record address and retains the graph-member identity until the i386 emitter applies the target byte offset, bit offset, width, and signedness. ADR 0063 adds `BIT_FIELD_STORE_VALUE` for plain assignment through direct, pointer-derived, and indexed record addresses. ADR 0064 adds compound assignment and prefix or postfix update at the same storage boundary. Non-four-byte storage, partial volatile mutation, and atomic access remain open.

ADR 0023 extends `BINARY` with equality, inequality, and bitwise AND for represented four-byte integers. Logical AND and logical OR use the existing branches to skip their right operands when the left value determines the result. Both paths join with one normalized integer value. The remaining operators stay open.

ADR 0024 extends `BINARY` with signed and unsigned 32-bit division and remainder. Signed emission uses `CDQ` and `IDIV`; unsigned emission clears EDX and uses `DIV`. Quotients come from EAX and remainders from EDX. Wide integer division and remainder remain open.

ADR 0025 extends `BINARY` with signed and unsigned 32-bit less-than and less-than-or-equal. The emitter uses `SETL` or `SETLE` for signed inputs and `SETB` or `SETBE` for unsigned inputs. All six integer comparison operators now reach the represented four-byte object path. Pointer relations and wide integer relations remain open.

ADR 0026 extends `BINARY` with 32-bit left shift, right shift, and bitwise OR. Shift counts keep their independently promoted four-byte integer type, while `input_type` retains the promoted left type. The emitter uses `SHL`, `SHR`, or `SAR` with `CL`, and bitwise OR uses `OR`. Wide shifts and wide bitwise OR remain open.

ADR 0027 extends `BINARY` with bitwise XOR for same-type represented four-byte integers. The emitter uses `XOR EAX, ECX`. A source guard pins the unchanged CPUID-toggle return expression without claiming the surrounding GNU inline assembly or general statement sequence. Wide XOR remains open.

ADR 0028 adds `UNARY` for bitwise complement over same-type represented four-byte integers. The emitter uses `NOT EAX`. A source guard pins the complete unchanged memory `align_up` helper and its surrounding unsigned arithmetic. Wide complement and the other unary operators remain open.

ADR 0029 extends `UNARY` with unary plus, negation, and logical not over represented four-byte integers. Unary plus emits no target instruction, negation uses `NEG`, and logical not uses `TEST`, `SETE`, and `MOVZX` to produce plain signed `int`. Active-source guards pin the negation in `dis_signed_bits` and the logical-not result in `cc_skip_brace_initializer`. Narrow, wide, floating, pointer, address, and dereference lowering remain open.

ADR 0030 uses `CONVERT` with `CTOOL_C_CONVERSION_NONE` for explicit casts between represented four-byte integer types. This keeps source casts distinct from implicit conversions while allowing the i386 emitter to preserve the existing 32 bits without a target instruction. The final return expression in `dis_signed_bits` pins the unsigned-to-signed direction, and a focused function pins the reverse direction. Narrow, wide, floating, pointer, and `void` casts were open when this record was accepted. ADR 0041 later added represented object-pointer casts, ADR 0045 added represented narrow integer casts, and ADR 0047 added explicit casts to `void` for represented scalar and `void` operands.

ADR 0031 adds recursive lowering for return and expression statements, compound statements without declarations, and `if` with optional `else`. It reuses `BRANCH_ZERO` and `JUMP`, permits multiple direct returns, and tracks whether each path can reach the next statement. The complete unchanged `dis_signed_bits` helper pins the selection path. Loops, `switch`, labels, `goto`, and declarations inside nested compounds remain open.

ADR 0032 adds pre-test `while` loops with represented four-byte integer conditions. It reuses `BRANCH_ZERO` for the forward exit and `JUMP` for the backward edge to condition evaluation. The complete unchanged `syscall_sleep_ms` helper pins the loop path. `do`, `for`, `break`, `continue`, `switch`, labels, `goto`, and declarations inside nested compounds remain open.

ADR 0033 adds post-test `do` loops with represented four-byte integer conditions. It lowers the body before the condition, branches to the exit on zero, and jumps back to the body when the condition remains true. A guarded inner loop from Doom's unchanged `D_Display` function pins the path. `for`, `break`, `continue`, `switch`, labels, `goto`, and declarations inside nested compounds remain open.

ADR 0034 adds `for` loops with optional expression initializers, represented four-byte integer conditions, and optional discarded iteration expressions. Initializers run once, conditions run before the body, and iteration expressions run before the backward edge. The guarded loop header from `url_hash_hex` in `bin/browser/url_hash.cc` pins the path. Declaration initializers, `break`, `continue`, `switch`, labels, `goto`, and declarations inside nested compounds remain open.

ADR 0035 adds `break` and `continue` for the nearest enclosing `while`, `do`, or supported `for` loop. Both statements reuse `JUMP`. Loop frames resolve exits and the different continuation points before the IR is published. Active guards in `cir_validate_initializer_ownership` pin unchanged examples of both statements. Declaration-initialized loops, `switch`, labels, `goto`, and declarations inside nested compounds remain open.

ADR 0036 extends the existing automatic-local path to declaration statements in supported compound statements and `for` initializers. A private source-order scan establishes each function's complete block-binding range before lowering. Successful count-only validation advances the same ownership cursor for unreachable declarations. `switch`, labels, `goto`, other local representations, and production integration remain open.

ADR 0037 adds direct identifier labels and `goto`. It validates each function's canonical label slice, uses a fixed-point pass to find reachable targets, and resolves forward jumps before publishing IR. Direct jumps stay function-relative and need no ELF relocation. `switch`, computed `goto`, GNU label addresses, other local representations, and production integration remain open.

### Structure-value ABI extension

A later hosted extension carries complete supported structures through the same typed address/value stack. One structure value occupies one abstract handle. `LOAD` captures an lvalue in instruction-owned frame storage, and structure-returning calls receive their own result slots. `STORE`, `STORE_VALUE`, `DISCARD`, conditional joins, and `RETURN_VALUE` retain their public instruction kinds while applying complete-object copy semantics. The i386 emitter uses `CLD` plus `REP MOVSB`, preserves ESI and EDI, and sends a hidden structure-return pointer at `EBP + 8`. The callee returns that pointer in EAX and selects the shared x86 `RET imm16` form for `RET 4`. The original scalar byte contracts remain unchanged. This extension remains hosted evidence and transfers no production ownership.

### Runtime narrow string extension

ADR 0053 adds `STRING_LITERAL_ADDRESS` and `COPY_STRING`. A runtime literal address retains its absolute frontend expression index, while a copy retains its semantic initializer index and consumes a selected character-array address. The i386 emitter owns each local `.rodata` symbol, emits its `R_386_32` use, and copies initializer bytes with `CLD` plus `REP MOVSB`. Automatic destinations are zeroed first, so bytes beyond the retained string keep their implicit zero value.

### Eight-byte integer result extension

ADR 0065 carries an eight-byte integer through constants, matching conditionals, fixed call results, discard, and return. One public scalar stack entry names an emitter-private two-word snapshot. This keeps the IR independent of the i386 EDX:EAX return pair. Parameters, lvalue access, arithmetic, and mixed-width conversion remain outside the extension.
