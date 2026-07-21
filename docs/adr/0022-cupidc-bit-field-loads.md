# Lower four-byte bit-field reads through CupidC IR

## Context

The hosted CupidC path could address an ordinary record member, but it stopped when a function read a bit field. A bit field has no C address, so `MEMBER_ADDRESS` cannot represent the access. Doom already depends on this operation. Its video adapter declares four eight-bit channels in one `uint32_t` storage unit and reads the red, green, and blue fields while converting pixels:

```c
struct color {
    uint32_t b:8;
    uint32_t g:8;
    uint32_t r:8;
    uint32_t a:8;
};
```

The frontend and i386 layout already retain the direct graph member, qualified record type, storage-unit byte offset, bit offset, width, and signedness. Linear IR needed a value operation that kept the semantic member identity without pretending that the field was addressable.

## Decision

Linear IR adds `BIT_FIELD_LOAD`. Its `reference` is the absolute direct-member index from the frontend graph. Its `input_type` is the complete record operand type, and its `type` is the unqualified field value type. The instruction consumes one record address and pushes the extracted integer value.

Lowering recognizes the lvalue-to-value conversion around a bit-field member. It checks that the member belongs to the complete record, that record qualification reached the member designator, and that the graph and layout agree on type, width, and storage range. The current slice accepts a complete four-byte integer storage unit that fits inside the record object and produces a four-byte integer result. Widths from one through 32 are represented. Narrow `_Bool`, character, and short storage units, packed records that are smaller than their declared storage unit, wider units, writes, atomic access, and indirect record addresses remain outside this decision.

The i386 emitter pops the record address into EAX, applies the member's storage-unit byte offset, and loads the complete 32-bit unit. It shifts the selected field to the high end of EAX, then uses `SHR` for unsigned fields or `SAR` for signed fields. This both masks the field and extends the result correctly. A 32-bit field needs no shifts. The base file-object address keeps its `R_386_32` relocation and zero addend.

Treating the field as an ordinary member address was rejected because C does not allow taking that address. Publishing byte and bit offsets directly in IR was rejected because the frontend member identity and target layout already own those facts. Loading only the bytes touched by the field was also rejected for this slice because the target layout defines a four-byte allocation unit and the volatile access contract must use that unit consistently.

## Consequences and evidence

The source guard pins the unchanged `struct color` declaration in `kernel/doom/src/i_video.h` and the red, green, and blue reads in `kernel/doom/src/i_video.c`. The focused IR fixture uses a volatile file-scope color and lowers `state.r` to `FILE_ADDRESS`, `BIT_FIELD_LOAD`, and `RETURN_VALUE`. It retains the `r` graph member at bit offset 16 with width 8.

The exact ELF32 object covers three access shapes. The unsigned Doom-shaped read emits `SHL 8` followed by `SHR 24`. A signed five-bit field at storage byte offset 4 emits `SHL 24` followed by `SAR 27`. A full-width unsigned field at byte offset 8 emits no shifts. The three functions occupy 63 text bytes and use three `R_386_32` relocations at offsets 4, 25, and 49, all against their direct objects with addend zero. Repeated emission is byte-identical.

A narrow `_Bool` field and an `_Atomic unsigned int` field receive the unsupported-type diagnostic. A valid GNU packed fixture proves the distinct compact-layout boundary: its record is one byte with alignment one, while its one-bit field retains a four-byte declared storage unit with alignment one. That read is unsupported rather than malformed because the emitter cannot load the complete storage unit without reading past the record object. Mutated units with an out-of-range bit span, a byte offset outside the record, or mismatched graph and layout widths receive the invalid-unit diagnostic without publishing partial IR. Existing arena, frozen-unit, deterministic-output, and recovery checks still apply.

This is hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, and ELF32 modules and their contracts. The host C compiler still produces the normal root and user C objects. The private in-kernel CupidC path remains the embedded runtime JIT and AOT path. No production artifact, ABI owner, build transform, host dependency, boot path, or runtime behavior changes here.

ADR 0063 later adds value-preserving plain assignment for represented four-byte bit fields, including pointer-derived and indexed record addresses. Issue #25 remains open. Narrow and wide storage units, bit-field compound assignments and updates, atomic ordering, other value widths, production integration, and staged self-hosting still remain.
