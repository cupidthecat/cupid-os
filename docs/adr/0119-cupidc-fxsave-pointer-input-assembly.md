# Emit pointer-input FXSAVE assembly in CupidC

- Status: Accepted
- Date: 2026-07-26

## Context

`kernel/core/process.c` saves a fresh floating-point state image in two
process-creation paths:

```c
__asm__ volatile("fxsave (%0)" : : "r"(p->fp_state) : "memory");
```

The source also checks that `fp_state` is 512 bytes and aligned to at least
16 bytes. CupidC already represented fixed-register inputs, numbered matching
inputs, output-only general registers, and a memory clobber. The preceding
privileged-register slice added independent `r` and `c` inputs, but FXSAVE
still had no exact pointer-only contract or emission path. The compiler
therefore stopped at the first unchanged FXSAVE statement.

The shared x86 model already knew the FXSAVE mnemonic and memory encoding.
The missing work belonged at the GNU assembly operand seam and in the
compiler emitter. Rewriting the process code or moving the instruction to a
host-assembled helper would have hidden the active language requirement.

## Decision

In GNU mode, independent `r` keeps its broader four-byte integer or data
pointer contract. When the template is exactly `fxsave (%0)`, the frontend
narrows that input to a four-byte object or `void` pointer. Function pointers
and integers fail with an FXSAVE-specific diagnostic. The public assembly
operand keeps the source expression, pointer type, exact `r` constraint, and
the no-match sentinel.

Linear IR treats the pointer as one value-stack input to `ASSEMBLY`. The
whole-unit validator binds the exact template to its volatile and memory
flags, operand counts, constraint, no-match metadata, pointer type, and
four-byte target layout before lowering. It performs the same checks for
unreachable statements, so dead control flow cannot carry malformed frozen
metadata. Expression evaluation remains in source order and occurs once.

The i386 emitter recognizes only the exact template `fxsave (%0)`. It
requires a volatile statement, one independent pointer input, no outputs, and
the source's `memory` clobber. It pops the evaluated pointer into EAX and asks
the shared x86 model to encode FXSAVE at `[EAX]`. The statement needs no
temporary frame area, relocation, host assembler, or private opcode writer.

The source remains responsible for the instruction's address requirements.
The two static assertions and the `process_t` layout provide that proof for
the active calls.

## Rejected alternatives

Changing `process.c` to call an assembly wrapper was rejected. The existing
statement is ordinary GNU C used by active kernel code, and the compiler must
represent it directly.

Sending the template to a host assembler was rejected. That would add a host
code-generation dependency and bypass the shared x86 instruction model.

Writing `0F AE /0` directly in the compiler was rejected. The shared encoder
already owns instruction selection, operand validation, ModR/M construction,
and deterministic bytes.

Accepting arbitrary text around FXSAVE was rejected. Leading instructions,
trailing instructions, displacement changes, a different operand number,
FXRSTOR, and trailing whitespace remain distinct unsupported templates.

Executing FXSAVE in a hosted contract was rejected. The instruction writes a
large state area and depends on processor state and alignment. Shared
decoding proves the selected instruction and address form without adding a
native execution hazard.

## Consequences and evidence

The public frontend contract retains the exact active array-member decay,
volatile and memory flags, one pointer operand, source locations, and
immutable metadata. It rejects integer and function-pointer `r` inputs and an
unsupported `cc` clobber, then parses a valid unit again.

The Linear IR contract covers a parameter pointer, a pointer returned by a
one-time `next_state()` call, and an unreachable statement. It checks stack
depths, evaluation order, deterministic repeat lowering, malformed matching
metadata, an unsupported constraint, a forged pointer type, an eight-byte
target layout, rollback, and same-job recovery.

The deterministic object fixture emits two 20-byte functions, one for
`unsigned char *` and one for `void *`. Each function contains exactly one
decoded FXSAVE with bytes `0F AE 00`, a 32-bit EAX base, no index, no segment
override, and zero displacement. The complete object is 396 bytes with 40
bytes of `.text`, five sections, three symbols, and no relocations. Repeated
emission is byte-identical. Invalid template text, a missing memory clobber,
forged constraint metadata, output rollback, and recovery have separate
checks.

Compiler head also compiles the complete unchanged `kernel/core/process.c`
twice with the exact `KERNEL_I386` argument vector. The two validated
30,216-byte i386 ELF32 relocatable objects are byte-identical and have
SHA-256
`81ef6d428528b6fcc98826cda634abe5d9d0c00b8aa59cb374d7c1186b0320c5`.
Disassembly finds exactly two `0F AE 00` instructions at `.text` offsets
`0x1967` and `0x4d7c`.

The full native Toolchain contract target and all three focused public
selectors pass. No production object or runtime path changed, so this
compiler-head step does not claim a boot result. The checked seed predates
the capability, and `process.c` remains a host-owned `.c` source until a
seed refresh and production cutover pass their own gates.
