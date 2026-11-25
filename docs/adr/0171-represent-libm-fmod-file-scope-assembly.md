# ADR 0171: Represent the libm remainder wrappers

## Status

Accepted on 2026-07-28.

## Context

The next two GNU file-scope assembly definitions in
`kernel/cpu/libm.c` implement `fmod` and `fmodf` with x87 `FPREM`.
`FPREM` may complete only part of a reduction, so both wrappers read the x87
status word and repeat while C2, bit 10, remains set.

The loop begins with `x` in ST(0) and `y` in ST(1). After convergence,
`FSTP ST(1)` discards the divisor while keeping the remainder in ST(0). The
wrapper then returns that value through XMM0 at the source scalar width.

Compiler head already represented the preceding twelve opening functions,
the `fabs` masks and wrappers, and the eight rounding wrappers. The first
`fmod` definition at line 465 was the next unchanged-source failure.
Rewriting the loop in C or passing it to a host assembler would hide a real
source requirement.

## Decision

CupidC recognizes the two complete templates as one exact file-scope
remainder family. The templates must define the matching global symbol and
agree with an existing `double (double, double)` or
`float (float, float)` declaration. General file-scope GAS input remains
outside this boundary.

The emitter uses Cupid's shared x86 model for the complete body:

1. Load `y`, then `x`, at the source width so the loop starts with the
   required x87 stack order.
2. Mark the `FPREM` instruction as the backward target.
3. Store the x87 status word in AX and test C2 with `TEST AX, 0x0400`.
4. Emit a short `JNE` and patch its signed byte displacement back to
   `FPREM`.
5. Pop ST(1), move the remaining result through the width-specific stack
   slot into XMM0, restore ESP, and return.

The short-branch helper asks the shared encoder for a rel8 form and rejects
targets outside the signed byte range. It does not append opcode bytes
directly. The exact loop uses displacement `-10` and targets function offset
8.

Both functions reach x87 depth two, keep that depth through each partial
reduction, and return to their incoming depth. ESP is balanced, and no
relocation is needed.

## Evidence

The object contract and unchanged-source probe were changed before the
emitter. Both failed at the first `fmod` template on line 465. After the
emitter change, the two tests pass and the source reaches the aligned
constant block on line 544.

The combined math fixture now has 702 text bytes and no relocations. The two
new symbols occupy 70 bytes:

| Symbol | Offset | Size |
| --- | ---: | ---: |
| `fmod` | 632 | 35 |
| `fmodf` | 667 | 35 |

The decoder checks all twelve instructions in each body. It checks the
argument widths and offsets, `FPREM`, `FNSTSW AX`, the `0x0400` test, the
short backward branch, `FSTP ST(1)`, the scalar result store, the XMM0 load,
and balanced stack adjustment. The rel8 operand follows the shared decoder
contract with both logical and encoded widths set to eight bits.

A second emission must match the first byte for byte. Negative cases replace
the C2 mask with `0x0200` and give `fmod` the float prototype. Both fail
without changing the parsed unit or publishing partial output. The same job
then emits the valid unit again.

The first complete build-graph run passed 61 of 62 tests. The remaining
drift gate reported eleven new `sizeof` expressions in the expanded C
contract, moving the pre-replay count from 5,159 to 5,170 across the same
168 files. The remote conversion increment had independently moved its
count to 5,163; the combined inventory is 5,174. The focused drift selector
and complete module passed before replay.

The combined hosted source lock for `toolchain/cupidc_emit.cc` is 307
definitions, 7,593 statements, 64,407 expressions, 921 block bindings, and
537 initializers. Its self-host frontier object has 307 functions, 477,755
text bytes, 527,264 object bytes, and text fingerprint `28484716`.

| Gate | Result |
| --- | --- |
| Initial red fmod object and source probes | 2 expected failures in 21.231 seconds |
| Green fmod object and source probes | 2 tests passed in 23.467 seconds |
| Neighboring x87 and file-scope object group | 9 tests passed in 22.798 seconds |
| Complete frontend and Linear IR modules | 171 tests passed in 32.322 seconds |
| Self-host frontier object lock | 1 test passed in 34.158 seconds |
| Strict hosted Toolchain build | All native contracts passed in 21.1 seconds; six static i386 artifacts linked |
| Cupid-built compiler object and five-tool fixed point | 2 tests passed in 995.321 seconds |
| Bootstrap audit regeneration | Passed in 78.6 seconds |
| Bootstrap audit drift check | Passed in 71.1 seconds |
| Focused manifest-drift selector | 1 test passed in 177.445 seconds |
| Complete build-graph audit module | 62 tests passed in 587.935 seconds |
| Combined frontend, IR, fmod object, and source replay | 173 tests passed in 53.490 seconds |
| Combined self-host frontier object lock | 1 test passed in 27.691 seconds |
| Final combined strict Toolchain build | All native contracts passed in 31.8 seconds; six static i386 artifacts linked |
| Final combined compiler object and five-tool fixed point | 2 tests passed in 890.182 seconds |
| Final combined bootstrap audit regeneration | Passed in 70.2 seconds |
| Final combined bootstrap audit drift check | Passed in 60.1 seconds |
| Final combined build-graph audit module | 62 tests passed in 570.420 seconds |

The regenerated graph still contains 698 active sources, 253 feature IDs,
504 transforms, and 42 accounted unreachable files. Its active-source
digest is
`5786b7a161f7f24a341e5794151b50b6b95b560bbbd3b65abe1aaa218f0bfcef`.
The 1,526,996-byte JSON has SHA-256
`5de096f73e4d9733b9b6a2e5889a3fd949023a0f42bea25e89f6f2fbec59df26`.
The 15,060-byte Markdown report has SHA-256
`ea882a5e450f44124cd639de531f6da53bad6701f5df06de8ed3772b169a46d1`.

## Rejected alternatives

Replacing the wrappers with ordinary C was rejected because the partial
reduction loop and x87 stack cleanup are part of the active source.

Passing the templates to GAS was rejected because CupidC object output must
not acquire a hidden host-assembler dependency.

Using a near branch was rejected because the source's local backward branch
fits the canonical short form and the exact object contract should retain
that form.

Appending `75 F6` directly was rejected because the compiler must exercise
the shared x86 model and its range checks.

Running `FPREM` once was rejected because large exponent differences may
require several reduction steps.

## Consequences

Compiler head moves unchanged `kernel/cpu/libm.c` from line 465 to the
file-scope read-only constant block on line 544. The next work needs the
aligned `libm_log2e_const` and `libm_ln2_const` data effect before the
following exponent and logarithm wrappers can move.

The checked seed predates this family. The normal `libm.c` recipe remains
host-owned, and the source keeps its `.c` suffix. No production object,
image, ABI, runtime path, ownership count, or host-dependency count changes
in this increment. Issue #26 remains open for the constant block and later
assembly forms.

`TempleOS/` remains untouched reference material.
