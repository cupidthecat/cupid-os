# ADR 0160: Represent flags-restore assembly

## Status

Accepted on 2026-07-28.

## Context

The unchanged `simd_cpu_has_cpuid()` helper tests whether software can toggle
the ID bit in EFLAGS. It snapshots the flags, changes that bit, restores the
modified value, snapshots the result, and finally restores the original
value. Its two restore statements use this exact GNU assembly form:

```c
__asm__ volatile(
    "pushl %0\n\t"
    "popfl\n\t"
    :
    : "r"(value)
    : "cc");
```

Compiler head could already snapshot EFLAGS through `pushfl` and `popl`, but
it rejected the `cc` clobber before reaching object emission. Removing the
clobber or rewriting the source around that limitation would hide a real
machine-state effect.

## Decision

CupidC accepts `cc` as a public assembly flag only for the exact volatile
flags-restore template above. The statement must have no outputs, one
independent `r` input holding a non-atomic 32-bit integer, and exactly one
`cc` clobber. A different template carrying `cc`, a missing or duplicate
clobber, another constraint, a matching input, an added clobber, or a
different input width remains unsupported.

The frontend publishes `CTOOL_C_ASSEMBLY_CC_CLOBBER` with the frozen
assembly record. Linear IR checks the template, flags, operand slice,
constraint, expression type, and target layout again before lowering the
input value. The emitter repeats that check, consumes the evaluated value
through EAX, and asks the shared x86 model to emit:

```text
POP EAX
PUSH EAX
POPF
```

The first instruction consumes CupidC's expression-stack value. The final
two instructions reproduce the source operation and leave ESP balanced.
The path needs no frame temporary, symbol, or relocation.

## Evidence

A focused frontend test first failed on the formerly unsupported `cc`
clobber, then passed with the exact public metadata. Frontend failures cover
an unrelated template, duplicate or missing `cc`, the wrong input shape,
and same-job recovery.

A dedicated Linear IR selector checks the exact flag and operand record,
input evaluation before the assembly instruction, deterministic repeats,
rollback, and recovery. Frozen-unit mutations remove `cc`, add `memory`,
replace the template, request a fixed register, and forge a matching input.

The object selector decodes every instruction in the emitted function and
requires exactly one `58 50 9D` restore sequence. It also requires a
relocation-free object, byte-identical repeat emission, transactional
failure, and recovery. Seven neighboring assembly object selectors pass
together, covering legacy register and tied inputs, pointer output,
snapshots, call-next, port I/O, privileged registers, and operand-free
statements.

The combined frontend suite passes 91 tests, and the complete Linear IR
suite passes 80 tests. The active self-host object frontier passes with
updated deterministic locks:

| Source | Functions | Text bytes | Object bytes | Text fingerprint |
| --- | ---: | ---: | ---: | --- |
| `toolchain/cupidc_frontend.cc` | 407 | 822,022 | 976,512 | `503C286F` |
| `toolchain/cupidc_ir.cc` | 254 | 469,147 | 504,556 | `67557415` |
| `toolchain/cupidc_emit.cc` | 296 | 464,088 | 508,748 | `4CBCB346` |

A native hosted CupidC build passes with the repository's warning-as-error
profile. A complete `KERNEL_I386` compiler-head probe of unchanged
`kernel/cpu/simd.c` now accepts both flags-restore statements and stops
later, at line 52, where the CPUID statement assigns EAX to both a fixed
output and a fixed input.

The complete object Python module did not finish inside a 604-second test
ceiling. Its new decoded-object selector, the seven neighboring selectors,
and the active self-host frontier all pass separately. This timeout is
recorded as duration evidence, not as a passing full-module result.

## Rejected alternatives

Dropping the `cc` clobber was rejected because restoring EFLAGS changes the
condition codes.

Treating `cc` as a general no-op clobber was rejected. CupidC should not
publish metadata that its validator and emitter cannot honor for arbitrary
templates.

Emitting private opcode bytes was rejected because Cupid's shared x86 model
already owns `POP`, `PUSH`, and `POPF`.

Giving every input-only assembly statement the same emitter path was
rejected. The exact template and metadata check keeps this capability tied
to the active source requirement.

## Consequences

Compiler head moves the unchanged SIMD root past its former line-25
blocker. The next local blocker is the exact CPUID fixed-register
input/output overlap at line 52.

The checked seed predates this capability, so the normal SIMD recipe remains
host-owned and `kernel/cpu/simd.c` keeps its `.c` suffix. No production
object, normal image, ABI, runtime path, or host-dependency count changes in
this increment. A later seed promotion and full-source ownership proof are
still required.

`TempleOS/` remains untouched reference material.
