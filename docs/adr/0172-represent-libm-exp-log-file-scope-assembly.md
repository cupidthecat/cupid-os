# ADR 0172: Represent the libm exponent and logarithm wrappers

## Status

Accepted on 2026-07-28.

## Context

The next GNU file-scope assembly in `kernel/cpu/libm.c` defines two
read-only constants and eight functions:

- `libm_log2e_const` and `libm_ln2_const`
- `exp2` and `exp2f`
- `exp` and `expf`
- `log2` and `log2f`
- `log` and `logf`

The base-two exponent pair splits its argument with `FRNDINT`, evaluates the
fraction with `F2XM1`, and applies the integer part with `FSCALE`. The natural
exponent pair first multiplies by `log2(e)` and then uses the same sequence.

The base-two logarithm pair places one below the argument before `FYL2X`.
The natural logarithm pair places `ln(2)` there instead. The natural forms
therefore need absolute relocations to the local data labels.

Compiler head already represented the opening math wrappers, the `fabs`
data and wrappers, the eight rounding functions, and the two remainder
functions. The aligned constant block on line 544 was the next
unchanged-source failure. Passing the block or its functions to a host
assembler would leave a real CupidC object-path requirement unresolved.

## Decision

CupidC recognizes the complete constant block and all eight complete
function templates. The data block must appear before any natural exponent
or logarithm wrapper. It may appear only once, and its labels may not
collide with C declarations. Each function template must match one external
scalar prototype at the same precision.

The data effect appends these exact little-endian values to `.rodata` at
alignment eight:

| Symbol | Offset | Bytes |
| --- | ---: | --- |
| `libm_log2e_const` | 0 | `FE 82 2B 65 47 15 F7 3F` |
| `libm_ln2_const` | 8 | `EF 39 FA FE 42 2E E6 3F` |

Both labels are local `STT_NOTYPE` symbols. File-scope data effects are
placed in source order before ordinary read-only C objects, so a later
four-byte fixture object begins at offset 16.

The emitter shares one exponent sequence between all four exponent
functions. It uses Cupid's x86 model for the x87 loads, register-stack
operations, result stores, SSE moves, and returns. It does not append opcode
bytes directly.

`exp` and `expf` load `libm_log2e_const`. `log` and `logf` load
`libm_ln2_const`. A checked absolute-memory helper requires the encoder to
publish one four-byte displacement field with an absolute relocation, then
adds one `R_386_32` record. The base-two forms have no relocation.

## Evidence

The isolated object contract and unchanged-source probe were changed before
the emitter. They failed at the constant block on line 544. After the
emitter change, both pass and the unchanged source reaches `pow` on line
846.

The eight functions occupy 264 text bytes:

| Symbol | Offset | Size | Relocation |
| --- | ---: | ---: | --- |
| `exp2` | 0 | 37 | none |
| `exp2f` | 37 | 37 | none |
| `exp` | 74 | 45 | function offset 6 to `libm_log2e_const` |
| `expf` | 119 | 45 | function offset 6 to `libm_log2e_const` |
| `log2` | 164 | 23 | none |
| `log2f` | 187 | 23 | none |
| `log` | 210 | 27 | function offset 2 to `libm_ln2_const` |
| `logf` | 237 | 27 | function offset 2 to `libm_ln2_const` |

The resulting `.text` relocation offsets are 80, 125, 212, and 239. Every
relocation has type `R_386_32`, a known zero addend, and the expected local
target.

The decoder checks every instruction and operand in each function. It checks
the source-width argument and result accesses, absolute constant loads,
register-stack operands, XMM0 result moves, and stack adjustments. The
exponent functions reach x87 depth three, the logarithm functions reach
depth two, and every function returns with its incoming x87 depth and ESP.

Negative contracts change one constant bit, remove the block, move it behind
`exp`, duplicate it, collide a label with a C declaration, give `exp` the
float prototype, forge assembly metadata, and exhaust the output limit. Each
failure preserves the parsed unit and publishes no partial object. A later
valid emission in the same job matches the original object byte for byte.

Compiler head parses `toolchain/cupidc_emit.cc` as 314 definitions, 7,774
statements, 65,903 expressions, 941 block bindings, and 581 initializers.
Its deterministic self-host frontier object contains 314 functions, 489,091
text bytes, and 542,576 object bytes, with text fingerprint `36B99E5C`.

A fresh strict native Toolchain build passed all contracts, linked all six
static i386 artifacts, and completed the self-host link check. A
Cupid-built compiler-generation object passed in 239.893 seconds, and the
five-tool static fixed point passed in 771.967 seconds. All 62 build-graph
audit tests passed in 624.548 seconds. The final direct object and source
replay passed both tests in 22.252 seconds.

The final audit regeneration and drift check passed. It records 698 active
sources, 253 feature IDs, 504 transforms, and 42 accounted unreachable
files. Its active-source digest is
`9396c1fda51e0175c6211a0c1da654da438c0173293c900827f1b6d89e0d3d5b`.
The generated JSON has SHA-256
`a98342b8bcf5912cc25d07d69349719196b7b7f02061c0e5e53be8b72d68c3f9`;
the Markdown report has SHA-256
`83b99f0222133ca2405592e2825e71ffc5231a7d7a75216a19c9b4756e7035b8`.

## Rejected alternatives

Passing these templates to GAS was rejected because CupidC object output
must not gain a host-assembler dependency.

Expressing the constants as host floating-point literals was rejected
because the source fixes their binary encodings and local assembly labels.

Copying the exponent sequence four times in the emitter was rejected because
the active functions use the same x87 algorithm after their optional
scaling step.

Replacing the wrappers with ordinary C was rejected because their x87 stack
program and XMM0 return bridge are active source behavior.

Recognizing only the four relocation-free wrappers was rejected because it
would leave the data and relocation path unfinished at the same source
boundary.

## Consequences

Compiler head moves unchanged `kernel/cpu/libm.c` from line 544 to the
file-scope `pow` definition on line 846. The next increment needs that exact
bridge to `libm_pow_impl()` before the following source can move.

The checked seed predates this family. The normal `libm.c` recipe remains
host-owned, and the source keeps its `.c` suffix. No production object,
image, ABI, runtime path, ownership count, or host-dependency count changes
in this increment. Issue #26 remains open for `pow` and later assembly
forms.

`TempleOS/` remains untouched reference material.
