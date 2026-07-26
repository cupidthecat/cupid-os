# Capture the next instruction address with CupidC

- Status: Accepted
- Date: 2026-07-26

## Context

The stack-trace helpers in `kernel/lang/as.c` and `kernel/lang/cupidc.c`
capture EBP with an existing register snapshot, then capture EIP with the
same GNU assembly statement:

```c
__asm__ volatile("call 1f\n1: popl %0" : "=r"(eip));
```

The local call pushes the address of label `1`, and the following pop moves
that address into the output while restoring ESP. Compiler head already
represented the neighboring register snapshots, but it rejected the call
template during object emission. Rewriting the helpers to use a builtin or
leaving these roots permanently on the host compiler would have hidden a real
source requirement.

This requirement does not need a general inline assembler, GNU local-label
namespace, or arbitrary control transfer. It needs one exact state-read form
with a typed output and defined stack behavior.

## Decision

In GNU mode, CupidC accepts the exact template
`call 1f\n1: popl %0` with one volatile `=r` output. The output must be a
modifiable, non-atomic, complete 32-bit integer object. Inputs, clobbers,
fixed-register constraints, pointer outputs, alternate labels, and a
nonvolatile spelling are rejected.

The public frontend keeps the decoded template and its single output operand
in the existing immutable assembly tables. It gives call-leading templates
outside this exact form a direct unsupported-template diagnostic. GNU-disabled
source keeps the existing GNU assembly diagnostic.

Linear IR evaluates the output address once, then emits one `ASSEMBLY`
instruction. Whole-unit validation checks the exact template, flags, operand
partition, constraint, expression type, and four-byte integer layout.
Unreachable assembly records receive the same metadata checks even when they
produce no instruction.

The i386 emitter assigns the output through the existing deterministic
general-register planner. It emits `CALL rel32` with a zero displacement,
followed immediately by `POP r32`, using the shared x86 model for both
instructions. A zero call displacement targets the first byte after the call,
which is the pop instruction. The ordinary assembly output path then stores
the captured address through the saved output destination and preserves EBX.
No relocation or host assembler is involved.

## Rejected alternatives

A dedicated EIP builtin was rejected. The unchanged source already expresses
the operation in GNU C, and representing that source keeps the compiler
roadmap tied to active requirements.

Passing the template to an external assembler was rejected. It would add a
host code generator and split instruction validation from Cupid's x86 model.

Adding general numeric labels and arbitrary call targets was rejected for
this increment. The active form has one fixed local target and no relocation.
Broader inline-assembly control flow needs its own language and object
contract.

Treating the call as an ordinary C call was rejected. It has no callee, must
push the address of the following instruction, and must not create a symbol
or relocation.

## Consequences and evidence

The frontend contract covers the exact public metadata and rejects a fixed
register, pointer output, nonvolatile statement, matching input, memory
clobber, changed labels, and GNU-disabled source. Every failure preserves the
previous translation unit and permits another parse in the same job.

The Linear IR contract fixes the live sequence to `LOCAL_ADDRESS`,
`ASSEMBLY`, `LOCAL_ADDRESS`, `LOAD`, and `RETURN_VALUE`, with a maximum
abstract stack depth of one. A dead helper publishes only `RETURN_VOID`, but
its call-next metadata is still validated. Forged flags, labels, constraints,
and types fail transactionally. Repeated lowering is identical.

The object contract fixes the 54-byte `capture_eip` function. Shared decoding
finds `E8 00 00 00 00` at byte 16 and `POP EAX` at byte 21. A relocated
state oracle checks that EAX receives the address at byte 21, ESP and EBP are
restored, the caller return slot is unchanged, and EBX, ESI, and EDI retain
their incoming values. The object has no relocation and repeats byte for
byte. A failure after partial template emission and a 64-byte output limit
both leave output empty, and the same job then reproduces the valid object.

Compiler head compiles both unchanged production roots twice under the
complete `KERNEL_I386` profile. `kernel/lang/as.c` produces a 148,056-byte
i386 relocatable object with SHA-256
`f88e783dd6fdb3687fbd70981efe12d71bd9e66fabc0bd244f18925047e6167c`.
`kernel/lang/cupidc.c` produces a 288,168-byte object with SHA-256
`b7a977c057eab72010a63e405f7d08cc9c929f38a30051f04edf3742a97c4d3e`.
Both repeated objects are byte-identical and pass the shared ELF32 validator.
The complete Toolchain contract target also passes.

This is compiler-head evidence only. The checked seed predates the capability,
so both roots remain host-owned and keep their `.c` names. A checked-seed
refresh, production recipe transfer, source rename, full image build, and
runtime proof remain separate work.
