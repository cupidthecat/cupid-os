# ADR 0173: Represent the libm cdecl bridges

## Status

Accepted on 2026-07-29.

## Context

The last 18 file-scope assembly definitions in `kernel/cpu/libm.c` are
public ABI bridges:

- `pow`, `powf`, `hypot`, `hypotf`, `nextafter`, and `nextafterf` take two
  arguments.
- `asin`, `asinf`, `acos`, `acosf`, `sinh`, `sinhf`, `cosh`, `coshf`,
  `tanh`, `tanhf`, `cbrt`, and `cbrtf` take one argument.

Each public function receives ordinary System V i386 cdecl arguments. It
calls a matching `libm_*_impl` C function, whose floating result arrives in
x87 ST(0). CupidC callers expect scalar floating results in XMM0, so the
wrapper spills ST(0) at the source width and moves that value into XMM0
before returning.

The wrappers cannot tail-jump to their implementation functions. They must
copy the original argument words above the return address, issue a normal
cdecl call, and reclaim those copied words. A double argument contributes
two words and a float argument contributes one. The repeated source
displacement stays constant because each push moves the next original word
into the same ESP-relative position.

Compiler head already represented every preceding statement and file-scope
assembly effect in this translation unit. The exact `pow` template at line
846 was the last unchanged-source failure. Passing these definitions to GAS
would leave the final `libm.c` object boundary outside CupidC.

## Decision

CupidC recognizes the 18 complete templates listed above. Recognition is
exact. Other file-scope GAS text remains unsupported.

Each template must match a visible external wrapper declaration and a
visible external `libm_*_impl` declaration. The wrapper and callee must use
the same scalar precision and the expected one- or two-argument prototype.
Both declarations become required object symbols. A missing callee, internal
linkage, or a mismatched prototype fails before object publication.

One emitter handles all four stack shapes:

| Shape | Copied words | Text bytes | Call relocation offset |
| --- | ---: | ---: | ---: |
| Unary `double` | 2 | 31 | 9 |
| Unary `float` | 1 | 27 | 5 |
| Binary `double` | 4 | 39 | 17 |
| Binary `float` | 2 | 31 | 9 |

The emitter asks Cupid's shared x86 model for each ESP-relative push, direct
call, stack adjustment, x87 store, XMM0 move, and return. It does not append
raw opcodes.

Every call uses one `R_386_PC32` relocation to the matching implementation
symbol. The encoded call field is `-4`, and the relocation carries a known
addend of `-4`.

## Evidence

The isolated bridge contract and unchanged-source probe were changed before
the emitter. The probe failed at `pow` on line 846 with the existing
unsupported file-scope template diagnostic. After the emitter change, the
isolated contract passes with 18 functions in 558 text bytes and exactly 18
text relocations.

The symbol layout is:

| Wrapper | Offset | Size | Callee relocation |
| --- | ---: | ---: | ---: |
| `pow` | 0 | 39 | 17 to `libm_pow_impl` |
| `powf` | 39 | 31 | 48 to `libm_powf_impl` |
| `asin` | 70 | 31 | 79 to `libm_asin_impl` |
| `asinf` | 101 | 27 | 106 to `libm_asinf_impl` |
| `acos` | 128 | 31 | 137 to `libm_acos_impl` |
| `acosf` | 159 | 27 | 164 to `libm_acosf_impl` |
| `sinh` | 186 | 31 | 195 to `libm_sinh_impl` |
| `sinhf` | 217 | 27 | 222 to `libm_sinhf_impl` |
| `cosh` | 244 | 31 | 253 to `libm_cosh_impl` |
| `coshf` | 275 | 27 | 280 to `libm_coshf_impl` |
| `tanh` | 302 | 31 | 311 to `libm_tanh_impl` |
| `tanhf` | 333 | 27 | 338 to `libm_tanhf_impl` |
| `cbrt` | 360 | 31 | 369 to `libm_cbrt_impl` |
| `cbrtf` | 391 | 27 | 396 to `libm_cbrtf_impl` |
| `hypot` | 418 | 39 | 435 to `libm_hypot_impl` |
| `hypotf` | 457 | 31 | 466 to `libm_hypotf_impl` |
| `nextafter` | 488 | 39 | 505 to `libm_nextafter_impl` |
| `nextafterf` | 527 | 31 | 536 to `libm_nextafterf_impl` |

The shared decoder walks every instruction. It checks each push displacement
and width, the direct call field, caller cleanup, result scratch space, x87
store width, XMM0 move, return, ESP balance, and x87 depth balance.

Negative contracts change a stack displacement, remove a callee declaration,
change callee and wrapper prototypes, forge assembly metadata, and exhaust
the object limit. Each failure preserves the parsed unit and publishes no
partial object. The same job can emit the valid object afterward, and two
successful emissions match byte for byte.

The unchanged 43,736-byte, 1,500-line `kernel/cpu/libm.c` source retains
SHA-256
`f1c13c83b758394189cc74ed6addfd9dfa99d42064c349c548476686b26cabce`.
Two exact kernel-profile compiles now produce the same valid 16,164-byte
ELF32 relocatable object with SHA-256
`ccfb59839b058020a3cdc30c8e6db7ebac8845215a38ff974b3cbca876574eac`.

Compiler head parses `toolchain/cupidc_emit.cc` as 320 definitions, 7,872
statements, 66,568 expressions, 956 block bindings, and 644 initializers.
Its deterministic self-host frontier object contains 320 functions, 494,170
text bytes, and 554,852 object bytes, with text fingerprint `B1A52C41`.

The isolated bridge test passed in the focused development run. The bridge
and complete-source pair passed two tests in 20.625 seconds. The adjacent
file-scope and self-host group passed six tests in 28.659 seconds. The
focused frontend aggregate contract passed in 13.696 seconds, and the
self-host frontier object lock passed in 31.893 seconds.

A complete frontend and Linear IR replay passed all 171 tests in 27.827
seconds after six active-source occurrence locks were refreshed from the
regenerated audit. A fresh strict native Toolchain build passed every
contract, linked all six static i386 artifacts, and completed the self-host
link checks in 67.9 seconds. A Cupid-built compiler reproduced the hosted
`cupidc_ir.cc` object twice in 241.646 seconds. The five-tool static fixed
point passed in 800.342 seconds. The final adjacent object replay passed all
six tests in 27.287 seconds. The final direct bridge and unchanged-source
replay passed both tests in 22.060 seconds.

The regenerated audit records 698 active sources, 253 feature IDs, 504
transforms, and 42 accounted unreachable files. Its active-source digest is
`95c2eb5c3af777d6b6901d491b502e0658ddac0bbcaea7d834138d810979e909`.
The generated JSON has SHA-256
`d23e49eefd4885508fd63f10454d9dd69f0b9e361e0d97a68ce255fea411529d`;
the Markdown report has SHA-256
`53f72262e6dbae27da017e08cc83a662368f83438beb5c6909cc66b922951298`.
Regeneration passed in 78.6 seconds, the drift check passed in 78.7 seconds,
and all 62 build-graph audit tests passed in 673.729 seconds.

## Rejected alternatives

Passing the templates to GAS was rejected because CupidC object output must
not acquire a host-assembler dependency.

Appending raw opcode arrays was rejected because these instructions and
relocations belong to Cupid's shared x86 model.

Tail-jumping to an implementation function was rejected because the
implementation expects a new cdecl argument block above its own return
address.

Rewriting the wrappers or their implementation algorithms was rejected
because both are active ABI behavior.

Writing 18 separate emitters was rejected because the source has only four
stack shapes and one result bridge.

## Consequences

Compiler head now emits the complete unchanged `kernel/cpu/libm.c`
translation unit. No unsupported file-scope assembly remains in that file.
General GAS input remains outside CupidC.

The checked seed predates this family and the earlier compiler-head libm
work. The normal `libm.c` recipe therefore remains host-owned, and the
source keeps its `.c` suffix in this increment. Seed promotion and a checked
production transfer are separate work. No production object, image, ABI,
runtime path, ownership count, or host-dependency count changes here. Issue
#26 remains open for that transfer.

`TempleOS/` remains untouched reference material.
