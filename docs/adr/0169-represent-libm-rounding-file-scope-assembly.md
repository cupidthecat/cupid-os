# ADR 0169: Represent the libm rounding wrappers

## Status

Accepted on 2026-07-28.

## Context

The eight rounding wrappers after `fabsf` in `kernel/cpu/libm.c` are GNU
file-scope assembly definitions:

| Operation | Double | Float | x87 rounding control |
| --- | --- | --- | --- |
| Round down | `floor` | `floorf` | `RC=01` |
| Round up | `ceil` | `ceilf` | `RC=10` |
| Round to nearest, ties to even | `round` | `roundf` | `RC=00` |
| Round toward zero | `trunc` | `truncf` | `RC=11` |

Every wrapper saves the caller's x87 control word, clears its two rounding
bits, installs the requested mode, runs `FRNDINT`, and restores the original
word. The float and double forms differ in their x87 load and store widths
and in the final SSE move to XMM0.

Compiler head already represented the twelve opening math wrappers and the
following `fabs` data and functions. The first `floor` template was the next
unchanged-source failure. Rewriting these definitions in C or sending them
to a host assembler would hide an active source requirement.

## Decision

CupidC recognizes the eight complete templates as one exact file-scope
rounding family. Each template must define the matching global function and
must agree with its existing `double (double)` or `float (float)`
declaration. General file-scope GAS input remains outside this boundary.

The emitter derives precision from each double and float pair and maps the
four operation pairs to control bits `0x0400`, `0x0800`, `0x0000`, and
`0x0c00`. It emits every instruction through Cupid's shared x86 model:

1. Load the scalar argument from `4(%esp)` onto the x87 stack.
2. Reserve eight stack bytes and save the original control word at
   `(%esp)`.
3. Load AX, clear the rounding field with `0xf3ff`, and OR the selected mode
   when it is nonzero.
4. Store the patched word at `2(%esp)`, load it, run `FRNDINT`, and reload
   the original word.
5. Pop the rounded value into the width-specific result slot, move it to
   XMM0 with `MOVSD` or `MOVSS`, restore ESP, and return.

The nearest-even pair has no OR instruction. This keeps its object bytes
faithful to the source instead of emitting a harmless extra operation. Every
wrapper reaches x87 depth one, returns to its incoming x87 depth, restores
the caller's control word, and balances ESP.

## Evidence

The object contract was extended before the emitter. It failed on the first
`floor` template with the established unsupported-file-scope-assembly
diagnostic. The unchanged `libm.c` probe also stopped at line 281. After the
emitter change, both tests pass and the unchanged source reaches `fmod` on
line 465.

The combined opening-wrapper fixture now has 632 text bytes and no
relocations. The eight new functions occupy 384 bytes:

| Symbol | Offset | Size |
| --- | ---: | ---: |
| `floor` | 248 | 48 |
| `floorf` | 296 | 50 |
| `ceil` | 346 | 48 |
| `ceilf` | 394 | 50 |
| `round` | 444 | 44 |
| `roundf` | 488 | 46 |
| `trunc` | 534 | 48 |
| `truncf` | 582 | 50 |

The decoder checks every operand, instruction, immediate, width, symbol
offset, and symbol size. It also proves that `round` and `roundf` omit the OR
instruction. A second emission must be byte-identical. Negative cases replace
the `0xf3ff` mask and give `floor` the float prototype; both fail without
changing the parsed unit or publishing partial output. The same job then
emits the valid unit again.

The focused rounding and neighboring assembly group passes seven tests in
31.360 seconds. The complete frontend and Linear IR modules pass 171 tests in
38.719 seconds. A warning-as-error hosted Toolchain build passes and links
all six static i386 artifacts. Cupid-built CupidC reproduces the changed
emitter object in 289.993 seconds, and the five-tool static fixed point passes
in 855.513 seconds. Final audit regeneration passes in 85.0 seconds, its
drift check passes in 73.8 seconds, and all 62 build-graph tests pass in
581.434 seconds.

The hosted source locks are:

| Source | Definitions | Statements | Expressions | Block bindings | Initializers |
| --- | ---: | ---: | ---: | ---: | ---: |
| `toolchain/cupidc_frontend.cc` | 407 | 16,052 | 106,261 | 2,407 | 1,479 |
| `toolchain/cupidc_ir.cc` | 254 | 7,084 | 65,836 | 930 | 340 |
| `toolchain/cupidc_emit.cc` | 300 | 7,405 | 63,099 | 898 | 522 |

The self-host frontier object locks are:

| Source | Functions | Text bytes | Object bytes | Text fingerprint |
| --- | ---: | ---: | ---: | --- |
| `toolchain/cupidc_frontend.cc` | 407 | 822,022 | 976,512 | `503C286F` |
| `toolchain/cupidc_ir.cc` | 254 | 469,147 | 504,556 | `67557415` |
| `toolchain/cupidc_emit.cc` | 300 | 467,561 | 515,632 | `F49AB960` |

The regenerated graph still has 698 active sources, 253 feature IDs, 504
transforms, and 42 accounted unreachable files. Its active-source digest is
`8287cc2659a15b404f412b30fc3643c08ce1fd7fe7fe739f08a4bb8a4f1afced`.
The 1,526,996-byte JSON has SHA-256
`481a484df5c36d579521a4312ce871178681f9c908178fda0ba51c0fe25cc667`.
The 15,060-byte Markdown summary has SHA-256
`1c42b82f36735d7f10e010f6b6291827047c9af46300549a7a7e0fd91dda6471`.

## Rejected alternatives

Replacing the wrappers with ordinary C was rejected because it would change
the active source and remove its explicit control-word behavior from the
compiler roadmap.

Passing the templates to GAS was rejected because CupidC object output must
not gain a hidden host-assembler dependency.

Implementing only `floor` and `floorf` was rejected because all four adjacent
operation pairs use the same source design and can share one checked emitter
path.

Changing `round` to round halfway values away from zero was rejected because
the active wrapper explicitly selects x87 nearest-even mode. This increment
represents the source as written.

Leaving the patched control word active on return was rejected because each
wrapper promises to preserve the caller's x87 environment.

## Consequences

Compiler head moves unchanged `kernel/cpu/libm.c` from line 281 to the
file-scope `fmod` definition on line 465. The next work needs the exact
`FPREM` loop, status-word test, local backward branch, and x87 stack pop used
by `fmod` and `fmodf`.

The checked seed predates this family. The normal `libm.c` recipe remains
host-owned, and the source keeps its `.c` suffix. No production object, image,
ABI, runtime path, ownership count, or host-dependency count changes here.
Issue #26 remains open for `fmod` and later assembly forms.

`TempleOS/` remains untouched reference material.
