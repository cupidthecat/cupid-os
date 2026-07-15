# Lower direct record-member addresses through CupidC IR

## Context

The hosted CupidC path could load a four-byte linked object, but it stopped when an unchanged function selected a member of a file-scope record. `drivers/timer.c` contains the first small active function at this boundary:

```c
uint32_t timer_get_frequency(void) {
    return timer_state.frequency;
}
```

`timer_state` has type `timer_state_t`. Its leading `uint64_t ticks` member places `frequency` at byte offset 8 under the fixed i386 layout. The frontend already retains the record binding, the direct graph-member identity, the member's qualified type, and the lvalue conversion. IR lowering needed to preserve that information without turning the public instruction into a target byte offset.

## Decision

Linear IR adds `MEMBER_ADDRESS`. Its `reference` is the absolute index of the direct member in the frontend type graph. Its `input_type` is the complete record operand type, and its `type` is the selected member type. The instruction consumes one record address and pushes the member address.

`FILE_ADDRESS` may now push the address of a complete file-scope record as well as a represented four-byte integer object. This does not make record values loadable. A following `MEMBER_ADDRESS` must select a complete ordinary member, and the existing `LOAD` still limits represented values to four-byte integers.

Lowering checks that the member belongs to the operand's complete structure or union, that record qualification reached the published member type, and that the member and result layouts agree. It also checks the member byte range against the record size. A bit field remains outside `MEMBER_ADDRESS` because it has no C address. ADR 0022 adds the separate `BIT_FIELD_LOAD` value operation for represented four-byte fields.

The i386 emitter keeps the graph-member identity until target emission. It pops the record address into EAX, adds the member's i386 layout offset through the shared x86 encoder, and pushes the adjusted address. An offset of zero omits the `ADD`. The base `FILE_ADDRESS` still emits one direct-symbol `R_386_32` relocation with addend zero, so the member offset does not change ELF symbol identity or relocation ownership.

Folding offset 8 into the `timer_state` relocation was rejected. That would work for this one file object but would erase the member identity and would not extend to nested, local, or pointer-based record addresses. Publishing a byte offset in IR was rejected because layout and instruction selection belong to target emission. Treating a bit field as an ordinary member address was rejected because it would claim semantics that neither the IR nor emitter represents.

## Consequences and evidence

The source-driven IR contract guards the unchanged `timer_get_frequency` body. Its four instructions are `FILE_ADDRESS`, `MEMBER_ADDRESS`, `LOAD`, and `RETURN_VALUE`, with a maximum abstract-stack depth of one. The contract also pins the 20-byte `timer_state_t` layout and the `frequency` member at byte offset 8.

The exact object function is 20 bytes. It contains `ADD EAX, 8`, defines the 20-byte zero-initialized `timer_state` object in `.bss`, and carries one `R_386_32` relocation at text offset 4 against that object with addend zero. Reading the object through the shared ELF32 module and decoding the function through the shared x86 module verifies every byte, symbol, relocation, and instruction.

Caller-mutated member identity and an out-of-record member layout receive the invalid-unit diagnostic without publishing partial IR. The existing constrained-output, repeat-emission, rollback, and same-job recovery contracts continue to apply. ADR 0022 records the later bit-field read contract and its separate negative cases.

This is hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, and ELF32 modules and their contracts. The private in-kernel CupidC path still produces every normal OS C object. No production artifact, ABI owner, build transform, host dependency, boot path, or runtime behavior changes here.

Issue #25 remains open. Bit-field writes and non-four-byte storage units, subscript and pointer-based addresses, compound and update lowering, atomic ordering, other value widths, nested and general statements, broader calls and ABI work, production integration, and staged self-hosting still remain.
