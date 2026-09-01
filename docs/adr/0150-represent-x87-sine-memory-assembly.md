# ADR 0150: Represent x87 sine memory assembly

## Status

Accepted on 2026-07-28.

## Context

The unchanged `stress_sin()` helper in `kernel/cpu/fpu.c` computes its result
with one x87 assembly statement:

```c
__asm__ volatile("fldl %1\n\t"
                 "fsin\n\t"
                 "fstpl %0\n\t"
                 : "=m"(r) : "m"(x));
```

The input and output are `double` objects. GNU memory constraints pass their
addresses to the assembly statement. The statement loads one binary64 value,
leaves the sine result in the same x87 stack position, and stores and pops
that value. It has no declared clobbers.

Compiler head already represented the preceding LDMXCSR and MOVSS memory
forms in this source. It still rejected the `double` input before Linear IR.
Changing the helper to pass pointers in general registers would avoid the
missing memory-operand support. Replacing the statement with lower-level C
would also hide the x87 requirement that the active source is meant to test.

## Decision

Compiler-head CupidC accepts exactly this volatile template:

```text
fldl %1
	fsin
	fstpl %0
```

The statement must have one `=m` output, one `m` input, and no clobbers. The
output must be a modifiable, non-atomic `double` lvalue. The input must be an
addressable, non-atomic `double` lvalue and may keep `const` or `volatile`
qualification. Other x87 templates, constraints, operand widths, rvalues,
bit fields, register objects, and atomic operands remain outside this slice.

The frontend freezes both typed operands. Linear IR evaluates the output
address first and the input address second, once each, including in
unreachable statements. The emitter consumes the addresses in reverse stack
order:

```text
POP EAX
FLD qword [EAX]
FSIN
POP EAX
FSTP qword [EAX]
```

Cupid's shared x86 model emits the three x87 instructions. The target byte
sequence is `58 DD 00 D9 FE 58 DD 18`. `FLD` raises the modeled x87 depth by
one, `FSIN` keeps it unchanged, and `FSTP` returns it to zero. The path uses
no frame temporary.

The frontend, IR, and emitter independently verify the template, volatility,
operand counts, slice bounds, constraints, expression types, layouts,
qualifiers, and absence of clobbers. A forged frozen unit cannot use the new
path with another assembly statement.

## Evidence

Dedicated frontend, Linear IR, and object selectors cover local and indirect
operands, qualified inputs, one-time address evaluation, source order,
unreachable statements, frozen metadata, deterministic repeats, constrained
output, rollback, and same-job recovery. Negative cases cover the wrong
floating width, a `const` or atomic output, rvalue and atomic inputs, register
constraints, changed templates, missing volatility, added clobbers, mismatched
types, and forged layouts.

Two pointer-based fixture functions each have the same 35-byte target image.
The complete deterministic ELF32 object is 440 bytes with 70 bytes of text,
five sections, three symbols, and no relocations. Shared decoding checks one
64-bit `FLD` from `[EAX]`, one operand-free `FSIN`, and one 64-bit `FSTP` to
`[EAX]`, with no index, segment override, or displacement.

A bounded decoder-driven oracle executes the target sequence sixteen times in
one machine state, alternating positive and negative zero. It checks the
binary64 sign bit, unchanged input bytes, output guards, two consumed address
words, callee-saved registers, and an empty x87 stack after every iteration.
The oracle deliberately models only the exact signed-zero identity needed to
check transport and stack balance. It is not a general implementation proof
for `FSIN`.

An exact `KERNEL_I386` command compiles unchanged `kernel/cpu/fpu.c` twice.
Both 6,620-byte ELF32 objects are byte-identical and have SHA-256
`14c3ea232b7d4455ceabd561c69293cc5849abae24d9f210aa69d64ed8c8a5cb`.
This closes the complete FPU root at compiler head.

The complete hosted replays pass 76 frontend tests, 64 Linear IR tests, and
80 object tests. An independent fixed-point run rebuilds all 19 stage-two and
stage-three objects and matches the five linked tools. The strict native
Toolchain build passes with `-Werror`.

The regenerated bootstrap audit and its checked comparison pass. All 62
mutation-based audit tests pass against 698 active sources, 253 feature IDs,
504 transforms, and 42 accounted unreachable files. The normal
`make -j2 all WAD_SRCS=` image build also passes and includes the updated CTXT
pages. Because the checked seed still predates this decision, that image is
an asset-integration result rather than a runtime claim for the new x87 path.

## Rejected alternatives

Passing pointers through general registers was rejected because the active
source correctly requests memory operands and should continue to express that
contract directly.

Treating arbitrary `m` operands or general x87 assembly as supported was
rejected because the compiler does not yet substitute general memory
operands or model arbitrary x87 stack effects.

Using a private instruction encoder was rejected because Cupid's shared x86
model already owns the required `FLD`, `FSIN`, and `FSTP` forms.

Using the host math library as an execution oracle was rejected because it
would test a different implementation and introduce host floating behavior
into a byte and state contract.

## Consequences

Compiler head now compiles the full unchanged `kernel/cpu/fpu.c` translation
unit. The checked bootstrap seed does not carry this capability, so the root
remains host-built and keeps its `.c` name. This decision changes no
production owner, build recipe, ABI, runtime path, or host dependency.

The next measured active-source frontier is the larger x87 control-word and
rounding statement in `kernel/core/string.c`. Compiler head reaches its
`"ax"` clobber at line 146. That statement also uses stack scratch space,
`fnstcw`, `fldcw`, `frndint`, `fstpl`, and a `memory` clobber. It needs a
separate language and stack-effect decision.
