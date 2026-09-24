# ADR 0154: Represent x87 round-down memory assembly

## Status

Accepted on 2026-07-28.

## Context

The unchanged `str_floor()` helper in `kernel/core/string.c` changes the x87
rounding mode long enough to round one `double` toward negative infinity:

```c
__asm__ volatile(
    "fldl %1\n\t"
    "fnstcw -2(%%esp)\n\t"
    "movw  -2(%%esp), %%ax\n\t"
    "movw  %%ax, -4(%%esp)\n\t"
    "andw  $0xF3FF, -4(%%esp)\n\t"
    "orw   $0x0400, -4(%%esp)\n\t"
    "fldcw -4(%%esp)\n\t"
    "frndint\n\t"
    "fldcw -2(%%esp)\n\t"
    "fstpl %0\n\t"
    : "=m"(r)
    : "m"(x)
    : "ax", "memory");
```

The statement loads the input before it borrows four bytes below ESP. It
saves the caller's x87 control word, changes only the rounding-control field
to `01`, rounds the value, restores the saved control word, and stores the
result. The `ax` clobber is real because the source uses AX to copy the saved
word. The `memory` clobber records the private stack writes and the ordering
contract.

Compiler head already represented the smaller x87 sine block in
`kernel/cpu/fpu.c`. It stopped on this statement's `ax` clobber before Linear
IR. Removing either clobber or rewriting the helper around the compiler would
hide active requirements instead of teaching CupidC how the source works.

## Decision

Compiler-head CupidC accepts this exact volatile template with one
modifiable, non-atomic `double` `=m` output, one addressable, non-atomic
`double` `m` input, and the exact `ax` plus `memory` clobber set. Qualified
indirect operands are valid when the output remains modifiable. Other
templates, constraints, widths, rvalues, bit fields, register objects,
atomic objects, and clobbers remain outside this slice.

The public assembly flags now include `CTOOL_C_ASSEMBLY_AX_CLOBBER`. The
frontend accepts that flag only for this template. Linear IR repeats the
complete template and metadata check, then evaluates the output address
before the input address, once each. Unreachable statements receive the same
validation and lowering.

The emitter loads the input from its evaluated address, then uses the
four-byte stack slot that held that consumed address as control-word scratch.
The pending output address stays at `[ESP]` and is not overwritten. After
rounding and control-word restoration, the emitter pops that output address
and stores the binary64 result.

The direct assembly sequence is 44 bytes:

```text
58 DD 00
D9 7C 24 FE
66 8B 44 24 FE
66 89 44 24 FC
66 81 64 24 FC FF F3
66 81 4C 24 FC 00 04
D9 6C 24 FC
D9 FC
D9 6C 24 FE
58 DD 18
```

These bytes decode as `POP EAX`, `FLD qword [EAX]`, `FNSTCW word
[ESP-2]`, the two AX word moves, 16-bit `AND` and `OR` at `[ESP-4]`,
`FLDCW word [ESP-4]`, `FRNDINT`, `FLDCW word [ESP-2]`, `POP EAX`, and
`FSTP qword [EAX]`. The x87 stack returns to its original depth. No frame
temporary is reserved.

The frontend, IR, and emitter each verify the exact template, flags, operand
counts, constraints, types, layouts, and qualifiers at their own trust
boundary. Forged frozen metadata cannot attach the AX flag or this emission
path to another statement.

## Evidence

Dedicated frontend, Linear IR, and object selectors cover local and
qualified indirect operands, reversed clobber order, one-time address
evaluation, source order, unreachable statements, deterministic repeats,
constrained output, rollback, and same-job recovery. Negative cases cover
the wrong floating width, a `const`, atomic, or register output, rvalue,
register, and atomic inputs, missing or duplicate clobbers, an extra
clobber, missing volatility, changed control masks, mismatched frozen
types, matching constraints, and forged layouts.

The two fixture functions are 71 bytes each. Their deterministic ELF32
object is 524 bytes with 142 bytes of text, five sections, three symbols, and
no relocations. Shared decoding checks all twelve direct instructions,
including each memory width, EAX or ESP base, signed displacement, AX lane,
immediate, order, and encoded size.

A bounded decoder-driven oracle handles only the exact `FNSTCW`, `FLDCW`,
and `FRNDINT` state used here. It runs eight binary64 cases under four
incoming rounding modes. The cases cover signed zero, positive and negative
fractions, positive and negative nonintegral values, and unchanged
integers. Every run checks the temporary control word, restoration of the
incoming word, exact rounded output, unchanged input, balanced x87 depth,
stack movement, the AX clobber's final replacement by the output address,
other register sentinels, the pending output pointer, scratch contents, and
memory guards. The proof does not execute native x87 code or claim a general
`FRNDINT` model.

The exact unchanged `str_floor()` definition is also extracted from
`kernel/core/string.c` under the audited `KERNEL_I386` profile. Two
compiles produce the same 420-byte ELF32 object with SHA-256
`448012fe57ec625c6075e97cf91163b994a0443238c5d6bdf25e4b839763f14e`.
The complete unchanged translation unit passes this block and reaches its
next independent frontier:

```text
/kernel/core/string.c:190:25: error CTB000010: floating cast is outside this expression slice
```

The complete hosted replays pass 77 frontend tests, 65 Linear IR tests, and
83 object tests. The static fixed-point check rebuilds all 19 stage-two and
stage-three C objects and matches all five linked tools. The strict native
Toolchain build, bootstrap audit, mutation-based audit replay, and normal
image build also pass.

## Rejected alternatives

Removing the AX or memory clobber was rejected because both describe effects
of the unchanged statement.

Allocating a new frame temporary was rejected because the active template
deliberately uses the slot just consumed from the evaluation stack. Reusing
that slot keeps the source's stack-relative offsets exact while preserving
the pending output address.

Treating AX or memory clobbers as generally supported was rejected because
the compiler does not yet preserve or model arbitrary assembly clobbers.

Running the sequence natively in the host test process was rejected because
it would temporarily change host x87 state and would make the contract depend
on host execution details. The bounded shared-decoder oracle checks the
required bytes and state transition safely.

Using a private instruction encoder was rejected because Cupid's shared x86
model already owns every required instruction form.

## Consequences

Compiler head now represents the complete unchanged `str_floor()` assembly
block. The rest of `kernel/core/string.c` still stops at the separate
double-to-`uint64_t` cast on line 190, so the translation unit is not ready
for ownership transfer or a `.cc` rename.

The checked bootstrap seed does not carry this capability. No production
owner, build recipe, ABI, runtime path, source suffix, or host dependency
changes. The updated CTXT pages change delivered help text when the normal
image is rebuilt.
