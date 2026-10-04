# ADR 0213: Promote the returns-twice-capable Toolchain seed

## Status

Accepted on 2026-08-02.

## Context

Revision `b1106c28abc5a3905655a4b6df9d40737fb88c36` adds the GNU
`returns_twice` declaration and direct-call boundary described by ADR 0212.
The revision was committed and pushed before the seed candidate was built.
The preceding checked CupidC rejected the attribute, so it could not carry
this compiler capability into later checked builds.

The 19-source build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

## Decision

Promote all five stage-three Toolchain images as one checked cohort:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `267d5ce820aac6bdfdb418552c3c144f8eac30e8589d8f53bd52055c3adca12d` | yes |
| CupidC | 2,574,032 | `8d810739494123a3da1cba34f75f58c005e8796f2cb4e85ba57eead1578a1f4d` | yes |
| CupidDis | 379,648 | `1ceeec3e65423f11a3b937dee355191ca0769cbfc4a374505f2aacf85db56ec8` | no |
| CupidLD | 266,672 | `2bdb6ce6b04678bb89c6bb4f7afac7e152ce6c4a07c4e14e1b3aee0c899008ec` | yes |
| CupidObj | 253,724 | `f78752dc01daf3d2a9dc9265425f9c60639f438d5dcb91a001cf40d7d241ded5` | no |

Only CupidC changes from the preceding cohort. Its earlier image was
2,561,644 bytes with SHA-256
`a4dff3c1c8ae975e9b8278920d36aefe6ad9b28a52503a6d5d4253e04e4a21af`.
The other four tools retain their previous sizes and hashes, but remain part
of the promotion because the manifest treats the five tools as one trust unit.

The new manifest is 5,440 bytes with SHA-256
`40ebc0e976eef3ddd4b79aab83407b1131a288414247e5d6eff6bce88cde06bc`.
It names the pushed capability revision and retains the static i386 Linux ABI,
producer lineage, build plan, and five link orders.

This decision promotes compiler capability only. Active
`kernel/doom/dglibc.cc` remains on the unannotated compatibility form, with a
27-byte `dg_setjmp` body and a 38-byte `dg_longjmp` body. Moving that source to
the corrected form still requires a separate migration and guest proof.

## Evidence

The transition froze 41 source inputs with SHA-256
`65d13673bd8787eff4bd78dc601a30a5126cf8a6c26a0c3d99661b0f32913c98`.
All 19 C objects, the startup object, and all five tool images matched between
stage two and stage three. Both stages passed five help cases, ten successful
operations, and six useful failures. Compared with the preceding seed, only
CupidC differed from stage two. The other four initial seed images matched
their stage-two replacements.

The 15,054-byte transition report has SHA-256
`31d413aa425b70320c4b5eb7fe511cd3e2f1fd70f064ffb301bbefe730da8811`.

The post-promotion reproof completed in 696.4 seconds. All five promoted seed
images matched stage two. It used the same 41-input snapshot and comparison
counts: 19 C objects, one startup object, and five tools. It also repeated the
full behavior matrix. Its 15,053-byte report has SHA-256
`e885ace7994f8276a52107ba77d02ecabab3593f919694d3b148f1e7c77bb6d1`.

A focused carriage test first ran against the preceding seed and stopped at
its unsupported GNU attribute diagnostic. With the promoted seed, the test
passed in 1.269 seconds. Two compiles produced the same 500-byte object with
SHA-256
`992a554a6fe0d23cba3f33c0faedcf44004c635a75924e3c61847fd1d2540fb8`.
The negative case converted a marked function to a pointer. CupidC rejected
the conversion and left the existing sentinel output unchanged.

The complete checked-seed module passed all 39 tests in 791.687 seconds. It
included another poisoned-host fixed-point rebuild, the production Doom,
libm, kernel-entry, and SIMD carriage checks, checked-tool behavior, and the
manifest, source-drift, tamper, and publication failure cases.

The promoted seed also rebuilt the normal 20-artifact Toolchain contract
cohort with host compiler and linker commands poisoned. The run passed in
3,102.5 seconds. Stage-two and stage-three objects and executables matched,
the hosted runtime passed, live inputs still matched their frozen copies, and
the published cohort verified. Its 18,231-byte, 45-input manifest has SHA-256
`6aba176b437bbd7fa9a4f6b3cbc6dd0000875b216f8bae22c9b571f01f66858f`.

The active-build audit still contains 717 inputs, 449 transforms, 254 feature
requirements, and 25 classified unreachable files. Its active-source digest
is `5daf197a8bd5d1cdd8d78233daf264db92ef809b48c451c4e89b000ba32ccda9`.
The 2,547,062-byte JSON has SHA-256
`93d98153a6bde55787b8eb9840a13a7b25519eb93085d3a71148fe0328c597a9`,
and the 12,136-byte summary has SHA-256
`f81c6ce5c88e263040b4872658c022ebbf0f4dc15cbb33e7f9d57711bcd7a3fb`.
The read-only audit drift check passes.

The first complete graph-module run found one stale lexical lock. The
returns-twice contract had added 46 `sizeof` tokens while the contributing
file count stayed at 168, moving the exact total from 5,413 to 5,459. After
that correction, all 68 graph tests passed in 623.502 seconds.

## Failed approach

The first transition used a launcher with a five-second timeout. The launcher
returned before the bootstrap finished, leaving the run detached. The
bootstrap nevertheless completed every gate and published its report and
stage directories. A monitored duplicate then completed the same gates and
refused to overwrite the existing destination, as the publisher requires.
The published transition report and the independent post-promotion reproof
provide the retained evidence.

## Rejected alternatives

Keeping the preceding seed was rejected because checked CupidC could not parse
the GNU attribute used by the carriage fixture.

Replacing only `cupidc.elf` was rejected because the manifest promotes and
verifies all five tools as one cohort, even when four files remain unchanged.

Changing active dglibc in the promotion was rejected because seed carriage
does not prove the corrected non-local jump path in a guest.

## Consequences

The checked seed now carries the represented returns-twice declaration,
direct-call spill and restore behavior, control-flow safety check, and marked
function-pointer rejection. Later checked compiler stages can build and test
the same boundary without a native compiler.

No normal build owner or host dependency changes. Active dglibc remains on
the compatibility form. Its corrected source migration and guest runtime proof
remain open.
