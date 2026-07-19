# Lower lexical block declarations through CupidC IR

## Context

ADR 0018 lowered represented automatic locals only when their declarations formed a prefix in the function's outer compound statement. The frontend already publishes declaration statements throughout a function and permits a declaration as a `for` initializer.

Active source needs both forms. `bin/browser/woff.cc` contains this loop header:

```c
for (int i = 0; i < total_sfnt; i = i + 1)
```

`cir_validate_initializer_ownership` in `toolchain/cupidc_ir.c` declares temporary values inside its loop body. The complete active functions need types and expressions beyond this increment, but rewriting their declarations would hide real CupidC requirements.

## Decision

Before lowering a function, `ctool_c_lower_ir` walks the statement forms it currently supports and records the complete source-ordered block-binding range owned by that function. The walk follows compound children, both arms of `if`, loop bodies, and a `for` declaration initializer. Every declaration must own the next contiguous slice in the frontend block-binding table.

Declaration statements then use the automatic-local lowering from ADR 0018 wherever they occur in a supported compound statement. A `for` declaration initializer uses that same path before the condition target is recorded. Each represented binding becomes visible before its initializer is lowered, so point-of-declaration behavior does not change.

Unreachable statements still pass through the count-only validation context introduced for structured control flow. When that validation succeeds, it advances the real block-binding cursor and visible end to the validated values. This consumes lexical ownership without publishing instructions for unreachable initializers.

The frontend remains responsible for C name lookup and lexical scope resolution. IR lowering verifies contiguous ownership, function boundaries, supported object types, and references against the frozen function range. It does not resolve identifiers again.

No public IR record changes. `LOCAL_ADDRESS`, `STORE`, and the fixed EBP slots from ADR 0018 already represent these objects. Complete four-byte integer objects with none, `auto`, or `register` storage remain the supported boundary. Narrow, wide, pointer, floating, aggregate, block-static, incomplete, and over-aligned automatic objects keep their existing diagnostics. `switch`, labels, and `goto` also remain unsupported by hosted IR.

Adding block-binding slices to the public function record was rejected because the frontend's statement tree already contains the ownership data. A private scan avoids a public format change and checks caller-mutated units before result allocation. Treating unreachable declarations as no-ops was also rejected because it would leave later bindings unowned and could hide unsupported types.

## Consequences and evidence

The declaration-initialized `for` fixture lowers to 17 exact instructions. Its local index uses one fixed slot, its false branch reaches instruction 16, and its backward jump reaches instruction 3. The nested-compound fixture lowers two initialized locals in the two `if` arms to 16 instructions with a maximum abstract-stack depth of two. A loop-body fixture consumes declarations in `while`, `do`, and `for` compounds while publishing ten control-flow instructions. A return followed by an uninitialized declaration publishes only the two return instructions while still consuming the declaration's binding.

The deterministic ELF32 contract combines those four functions in 238 text bytes. Their sizes are 87, 80, 11, and 60 bytes. The first function uses `[EBP-4]`. The nested function reserves `[EBP-4]` and `[EBP-8]`. The unreachable declaration and the unreferenced loop-body declarations reserve no frame slots. The object has five symbols including the null symbol, no relocations, exact decoded branch targets, and byte-identical repeat emission.

Negative contracts keep the type boundary explicit. A `long long` declaration in a `for` initializer and a `long long` declaration after an unconditional return both receive `CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE`. A nested declaration whose binding slice overlaps the previous arm receives `CTOOL_C_IR_DIAG_INVALID_UNIT` at both public seams. IR failure preserves the frozen unit. Object failure also leaves output empty and rewinds allocations made during the operation. Existing ownership mutations continue to reject duplicate or cross-function block-binding slices as invalid input.

Active-source guards pin the browser declaration loop and the nested declarations in `cir_validate_initializer_ownership`. Neither source file is changed.

This is hosted bootstrap evidence. GCC or Clang still builds the shared modules and contracts and produces the normal root and user C objects. The private in-kernel compiler remains the embedded runtime JIT and AOT path. This decision transfers no production ownership and does not require an OS boot claim.

Issue #25 remains open. `switch`, labels, `goto`, other local representations and storage durations, pointer and aggregate values, broader calls and ABI work, production integration, and staged self-hosting remain.

## Extension

ADR 0037 lowers direct identifier labels and `goto` through the same function body. The declaration scan follows a label's body, and count-only validation no longer changes live label targets. `switch`, other local representations and storage durations, pointer and aggregate values, broader calls and ABI work, production integration, and staged self-hosting remain.

ADR 0051 lets static declarations in supported nested or unreachable declaration positions reach object emission. The existing block-binding order still controls declaration visibility and gives each static object its stable target identity.
