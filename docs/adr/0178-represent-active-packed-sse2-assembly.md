# ADR 0178: Represent the active packed SSE2 assembly

## Status

Accepted on 2026-07-29.

## Context

CupidC could compile `kernel/cpu/simd.c` through its EFLAGS restore and CPUID
statements. It then stopped at the first `xmm1` clobber in the 64-byte copy
loop. The rest of the source uses five more packed SSE2 statement shapes for
16-byte copying, color broadcast and streaming stores, alpha blending, and
saturating row addition.

Those statements are part of the active implementation. Replacing them with
scalar C or changing their register use would weaken the source to fit the
compiler. Passing the templates to a host assembler would also leave a hidden
toolchain dependency.

## Decision

Represent the six exact volatile statement shapes used by the unchanged SIMD
source.

The frontend records clobbers for XMM0 through XMM7. It requires the exact
clobber set for each template, zero outputs, and the source's ordered `r`
inputs. Destination and source lanes must be represented object or `void`
pointers. The broadcast value, alpha, inverse alpha, and rounding value must
be represented 32-bit integers. A missing, duplicate, or extra XMM clobber,
changed template, wrong constraint, wrong operand count, or wrong type is a
diagnostic.

Linear IR evaluates each input once in source order and keeps the packed
operand slice with the assembly record. Its trust boundary repeats the
template, flags, constraint, type, and target-layout checks before publishing
IR.

The i386 emitter consumes the evaluated operands through EAX and EDX. The
five-input blend also consumes its three scalar values from the existing
evaluation stack before loading the two pointers. It emits the active
`MOVD`, `MOVDQU`, `MOVNTDQ`, `PSHUFD`, unpack, multiply, add, shift, pack,
exclusive-or, and saturating-add operations through Cupid's shared x86 model.
No template text is sent to an external assembler.

The accepted surface remains source driven. Other packed SSE templates,
general `x` constraints, XMM outputs, and arbitrary register allocation are
still outside this decision.

## Evidence

Frontend contracts cover all six source forms and all thirteen input
operands. They lock the exact XMM and memory clobbers and reject a missing
clobber, a duplicate `xmm1`, and a changed template. The complete frontend
module passes 93 tests.

Linear IR contracts preserve input order and stack depth, including an
unreachable statement that must still be validated. Mutation cases reject
missing or extra XMM flags, a forged template, wrong constraints, and wrong
operand layouts. Repeated lowering and same-job recovery pass. The complete
IR module passes 82 tests.

The object fixture emits six functions in 385 text bytes, with seven symbols,
five sections, and no relocations. Every instruction decodes through the
shared x86 reader. Two complete objects match byte for byte. Frozen-record
mutations fail without replacing an existing output, the bounded-output case
fails cleanly, and the same job recovers.

Compiler head compiles unchanged `kernel/cpu/simd.c` twice under the exact
`KERNEL_I386` profile. Both validated ELF32 relocatable objects are 8,768
bytes and have SHA-256
`fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`.
The native Toolchain contract target also passes, including the updated
self-host source and object frontiers and all 587 shared x86 forms.

## Rejected alternatives

Rewriting the SIMD loops as scalar C was rejected because it would remove an
active optimized path.

Changing the assembly to use fewer XMM registers was rejected because the
source already expresses a valid SSE2 implementation.

Using a host assembler as an inline-assembly escape was rejected because it
would preserve the dependency this bootstrap is removing.

Treating every XMM template as valid was rejected because CupidC does not yet
have general XMM allocation or constraint semantics. Exact source-driven
forms keep the current boundary testable.

## Consequences

Compiler head now emits every statement in unchanged `kernel/cpu/simd.c`.
The checked seed still predates this capability, so the normal recipe remains
host-owned and the source keeps its `.c` suffix. Seed promotion, checked-seed
object proof, production transfer, normal image validation, and guest runtime
validation remain separate steps.

This change does not alter CupidASM, CupidDis, the shared x86 catalogue,
normal object ownership, the kernel image, or an ABI. `TempleOS/` remains
untouched reference material.
