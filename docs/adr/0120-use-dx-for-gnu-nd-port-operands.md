# Use DX for GNU Nd port operands

- Status: Accepted
- Date: 2026-07-26

## Context

The remaining host-built 8259 PIC root uses two GNU inline assembly
statements:

```c
__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
__asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
```

The GNU `Nd` constraint offers two valid locations for the port. A constant
in the range accepted by `N` may be encoded in the instruction, while any
represented port value may use the `d` alternative in DX. CupidC already
handled the active DX-only port helpers but rejected the combined constraint
before it reached IR.

Removing `N` from the kernel source would make the source fit the compiler.
It would not improve the compiler or preserve the source's valid GNU
contract.

## Decision

CupidC recognizes `Nd` as an independent fixed-register input and selects
its `d` alternative. The frontend accepts represented 8-bit, 16-bit, or
32-bit integers under the same fixed-input rules as `a` and `d`. It reserves
EDX, rejects a second use of that fixed register, and retains the original
two-character constraint in the immutable operand record.

Linear IR treats `Nd` as an EDX input when validating a frozen assembly
slice. It still requires a value expression, an integer type of a represented
width, no matching-output index, and no fixed-register collision.

The i386 emitter accepts the exact active `outb %0, %1` and `inb %1, %0`
templates. Their port operand must be a 16-bit integer with `d` or `Nd`, and
their accumulator operand must use the active 8-bit `a` or `=a` form. Both
statements must remain volatile and may not carry extra clobbers. The emitter
uses the existing shared x86 model and produces `EE` for OUT and `EC` for IN.

Always choosing DX is a valid selection from the GNU alternative constraint.
Immediate-port selection is an optimization opportunity, not a requirement
for correct code.

## Rejected alternatives

Rewriting `kernel/cpu/pic.c` to use `d` was rejected because the unchanged
source already expresses a valid and useful compiler requirement.

Treating every two-letter constraint as a fixed register was rejected. Only
the exact source-driven `Nd` spelling is represented.

Passing the template to a host assembler was rejected because it would keep
the normal object dependent on an external toolchain and bypass CupidC's
typed metadata checks.

Implementing the `N` immediate branch before the active source needed it was
deferred. The current choice preserves behavior for constants and runtime
ports, although constants take the DX form.

## Consequences and evidence

The frontend, Linear IR, and object contracts each have a focused
`legacy-port-assembly` selector. Positive cases retain both exact templates,
the `Nd` spelling, operand widths, and fixed-register ownership. Negative
cases cover reversed spellings, pointer and wide inputs, duplicate EDX use,
forged matching metadata, type changes, partial templates, unexpected
clobbers, rollback, and same-job recovery.

The focused IR fixture contains 13 instructions with fingerprint
`c5fd6ded012c701a`. Its deterministic 436-byte ELF32 object has 76 text bytes,
three symbols, no relocations, and structural text fingerprint `690d98c5`.
A decoder confirms one 8-bit IN and one 8-bit OUT through DX.

Compiler head compiles the unchanged `kernel/cpu/pic.c` under the complete
`KERNEL_I386` profile. The resulting 2,408-byte object has SHA-256
`c1855a19e0cd285953996344493dcefe916f06d89fed706219718920b4d2ea5d`.
The six focused legacy and existing port wrapper tests pass, and the complete
toolchain contract target passes.

The checked seed still predates this capability. `pic.c` remains host-built
until a seed refresh, source rename, production wrapper proof, full image
build, and runtime smoke move ownership together.
