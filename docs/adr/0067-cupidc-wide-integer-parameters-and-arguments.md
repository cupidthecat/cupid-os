# Pass eight-byte integer parameters through CupidC

## Context

ADRs 0065 and 0066 let hosted CupidC carry an eight-byte integer through constants, call results, object access, assignment, discard, and return. Function parameters still stopped at the older four-byte scalar boundary.

The unchanged `ctool_buffer_put_le64` declaration in `toolchain/ctool.c` has the next active ABI shape:

```c
ctool_status_t ctool_buffer_put_le64(ctool_buffer_t *buffer, ctool_u64 value)
```

CupidASM calls that function with an evaluated eight-byte data value. Replacing the value with two source-level words or changing the helper's interface would hide the compiler requirement. The helper body also shifts and narrows the value, so parameter transport is necessary but does not make the complete function buildable by hosted CupidC.

## Decision

Hosted CupidC accepts signed and unsigned eight-byte integers in declared parameter positions. A callee computes each EBP-relative parameter address from the full sizes of the parameters before it. An eight-byte parameter occupies two adjacent little-endian words in one cdecl stack slot. Loading it copies both words into the same private snapshot used by other wide values.

Direct and indirect callers may pass a wide value when the function type declares an eight-byte parameter at that position. Argument evaluation still follows source order, and each wide argument remains one Linear IR stack handle. The i386 emitter reserves an outgoing argument area, clears it, then copies the snapshot into its eight-byte destination. Represented scalar arguments keep four-byte slots, and structure arguments keep their existing four-byte-rounded inline spans.

The outgoing byte count includes the full width of every declared argument before the existing call-alignment calculation runs. ESP therefore remains aligned to sixteen bytes immediately before `CALL`. The caller removes the complete outgoing area and the private argument handles after the call. A wide result still uses EDX:EAX and does not use the structure hidden-result pointer.

A declared wide parameter in the named part of a variadic prototype uses the same rule. Values passed through an ellipsis, or through a call without a prototype, still require the represented four-byte scalar path. Hosted CupidC rejects an eight-byte value in either undeclared position with the existing ABI diagnostic. Wide conditions, arithmetic, compound mutation, increment or decrement, and mixed-width conversion remain outside this decision.

The emitter's structure-only call path was not copied. Its outgoing-area logic already handled byte spans, cdecl order, cleanup, and call alignment. The path now also handles wide parameter slots, and its internal name describes the shared outgoing area. Publishing two Linear IR values for one parameter was rejected because it would expose the i386 word pair to target-neutral control-flow and value analysis.

Lowering and object emission remain transactional. A rejected ellipsis or unprototyped argument leaves the frozen translation unit unchanged, publishes no partial result, rewinds operation storage, and allows later work in the same job.

## Consequences and evidence

The IR contract pins the unchanged `ctool_buffer_put_le64` signature and its call in CupidASM. Its nine-function fixture covers a single wide parameter, mixed four-byte and eight-byte parameters, conditional selection, direct and indirect calls, and a declared wide parameter followed by a represented ellipsis argument. A variadic callee starts its cursor after a final wide parameter and reads the next four-byte value. The fixture publishes fourteen parameter addresses, eleven wide parameter loads, three wide-result direct calls, one wide-result indirect call, one narrow-result direct call, seven named wide argument positions, one variadic start, one variadic read, one variadic end, and seven wide returns. Repeated lowering is deterministic and preserves the frontend unit.

The deterministic ELF32 contract emits the same nine functions with ten symbols, four `R_386_PC32` relocations, and one `R_386_32` relocation. Each relocation is tied to its caller span and intended target symbol. Decoding checks every wide copy, outgoing-area clear, return, and call site. A relocated i386 oracle executes the single, mixed, conditional, direct, indirect, named-variadic, and cursor-after-wide cases. It checks returned values, the original argument words, ESP, and EBP.

Separate IR and object failures pass an eight-byte value through an ellipsis and through an unprototyped call. Both receive the focused ABI diagnostic without output. The initial IR contract stopped at the former parameter check. The first object contract then stopped at the four-byte argument-size assumption. Extending those two seams made the contracts pass without changing `ctool.c`, `cupidasm.c`, or another active OS source.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, its contracts, and every normal C object. No production object, build transform, boot path, host dependency, or ownership count changes.

Issue #25 remains open. At this decision boundary, the full `ctool_buffer_put_le64` body still needed wide shifts and narrowing. Conditions, arithmetic, mutation, values without declared parameter types, floating values, production integration, staged self-hosting, and the fixed-point bootstrap were also unfinished.

## Extension: complete little-endian helpers

ADR 0068 adds the wide shifts, usual arithmetic widening, bitwise AND, and explicit byte narrowing used by both active little-endian helpers. Their complete unchanged bodies now lower and emit. The declared-parameter ABI in this decision is unchanged.

ADRs 0069 and 0070 later add wide comparisons, scalar conditions, addition, subtraction, unary plus, unary minus, and bitwise complement. ADR 0071 adds signed and unsigned wide switch dispatch, ADR 0072 adds multiplication, and ADR 0073 adds division and remainder. Wide mutation, values without declared parameter types, floating values, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished.
