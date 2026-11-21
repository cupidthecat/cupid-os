# ADR 0168: Represent fixed-register input overlap

## Status

Accepted on 2026-07-28.

## Context

The unchanged CPUID helper in `kernel/cpu/simd.c` uses the normal GNU form:

```c
__asm__ volatile(
    "cpuid"
    : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
    : "a"(leaf));
```

EAX carries the input leaf before CPUID and the output value afterward.
This is a valid input/output overlap because `=a` is a write-only output
without the early-clobber modifier. Compiler head already represented CPUID
with a numeric `"0"` input, but it treated the source `"a"` input as a
second independent EAX assignment and rejected the statement.

Changing the active source to `"0"` would compile around the missing
compiler rule. It would also hide a GNU constraint form already used by the
OS, so the compiler must represent it directly.

## Decision

CupidC accepts a fixed-register input when exactly one write-only output in
the same statement owns the compatible fixed register. Read/write outputs
remain independent inputs themselves and cannot receive a second fixed
input. The frontend keeps the input's source constraint and writes the
output's zero-based position to `matching_output`. Numeric matching
constraints continue to use that field without changing their spelling.

The frontend first reserves every fixed output. If a fixed input encounters
an occupied register, it searches the current output slice. A compatible
output turns the input into a match. A collision with another independent
input still fails. The usual matching-input checks then require one input per
output, represented integer operands, and equal widths.

Linear IR reconstructs the fixed-register relationship from the frozen
constraints. It rejects an unavailable or incompatible output index, a
duplicate tie, and an independent collision. Lowering treats a valid fixed
overlap like a numeric tie: the input value follows its output addresses in
the source-ordered operand stack. Both sides must be represented integers
with the same target width, including when a caller supplies a frozen unit.

The generic i386 emitter accepts either kind of tie. For a fixed overlap it
maps the input and output constraints to the same physical register, then
checks their integer types and widths again. It pops the evaluated input into
the register selected for that output before the assembly template runs. The
existing CPUID path then emits CPUID, snapshots EAX, EBX, ECX, and EDX, and
restores EBX.

## Evidence

The focused frontend, Linear IR, and object selectors were added before the
implementation. All three failed on the former duplicate-fixed-register
diagnostic. After the change they pass.

A local GCC 15.2 oracle accepts a `=c` output with a `c` input and rejects
the same explicit input beside a `+c` output as an impossible constraint.
CupidC keeps the same write-only boundary.

The frontend contract checks both CPUID spellings, keeps the fixed input as
`a`, and requires `matching_output == 0`. It also rejects a second `a` input
tied to the same output, a mismatched input width, and a `c` input colliding
with a `+c` read/write output. Existing independent fixed-register collision
tests remain green.

The Linear IR contract requires the CPUID input value immediately after the
four output addresses. Frozen-unit mutations point the input at the wrong
output, add a second matching input, forge a read/write-output overlap, and
replace the EAX input with a real same-width binary32 constant. Each mutation
fails without changing the source unit or publishing partial IR. Repeated
lowering and same-job recovery remain deterministic. The combined frontend
and Linear IR modules pass 91 and 80 tests.

The two-axis review found that the first frozen-unit path checked only the
input and output byte widths after recognizing a fixed overlap. A forged
four-byte float, pointer, or aggregate could therefore reach IR and the
emitter. The binary32 mutation reproduced that acceptance before the fix.
Linear IR and emission now have their own represented-integer guards, and
the review found no other correctness issue. A lower-priority review note
identified repeated fixed-register decoding across the frontend, IR, and
emitter. Their independent validation remains separate in this change.

The object contract decodes the complete CPUID function and requires
`POP EAX` immediately before CPUID. It also keeps the existing numeric tied
NOP execution oracle, EBX save and restore checks, relocation-free output,
byte-identical repeat emission, rollback, and recovery. The seven neighboring
assembly object selectors pass together.

The hosted source locks now read:

| Source | Definitions | Statements | Expressions | Block bindings | Initializers |
| --- | ---: | ---: | ---: | ---: | ---: |
| `toolchain/cupidc_frontend.cc` | 407 | 16,052 | 106,261 | 2,407 | 1,479 |
| `toolchain/cupidc_ir.cc` | 254 | 7,084 | 65,836 | 930 | 340 |
| `toolchain/cupidc_emit.cc` | 296 | 7,327 | 62,643 | 892 | 501 |

A separate self-host object gate compiles the three changed implementation
files twice and checks the complete text:

| Source | Functions | Text bytes | Object bytes | Text fingerprint |
| --- | ---: | ---: | ---: | --- |
| `toolchain/cupidc_frontend.cc` | 407 | 822,022 | 976,512 | `503C286F` |
| `toolchain/cupidc_ir.cc` | 254 | 469,147 | 504,556 | `67557415` |
| `toolchain/cupidc_emit.cc` | 296 | 464,088 | 508,748 | `4CBCB346` |

A native hosted CupidC build passes the repository's warning-as-error
profile. A complete `KERNEL_I386` probe of unchanged
`kernel/cpu/simd.c` accepts the two flags restores and the CPUID overlap.
It stops later, at line 134, on the first unsupported `xmm1` clobber.
The regenerated build audit accounts for 698 active sources, 253 feature
IDs, 504 transforms, and 42 unreachable sources. Its active-source digest is
`8266d73b94adc85dad423397ca19db467a2f37b3af2d6d38e1eb60ac9bba43d3`,
and the checked JSON file has SHA-256
`a395404b91995c35cdbe6ac69decdcdcbd0ba3f8a44b2f5f75f69a1e40f0f775`.

## Rejected alternatives

Rewriting the source input from `"a"` to `"0"` was rejected because it
would make active source accommodate a compiler omission.

Treating the input as an independent EAX value was rejected because one
physical register cannot hold two live independent values at the template
boundary.

Special-casing only the CPUID template in the frontend was rejected. Fixed
input/output sharing is a constraint relationship, and every later stage
must be able to verify it from public metadata.

Dropping the original fixed constraint after matching was rejected. Keeping
`a` lets Linear IR and emission prove that the recorded output really owns
EAX.

## Consequences

Compiler head moves the unchanged SIMD root past its line-52 CPUID blocker.
The next local blocker is the `xmm1` clobber in the four-register SSE copy
block on line 134.

The checked seed predates this capability. The normal SIMD recipe remains
host-owned, and `kernel/cpu/simd.c` keeps its `.c` suffix. No production
object, normal image, ABI, runtime path, or host-dependency count changes in
this increment. A later seed promotion and complete SIMD proof are still
required.

`TempleOS/` remains untouched reference material.
