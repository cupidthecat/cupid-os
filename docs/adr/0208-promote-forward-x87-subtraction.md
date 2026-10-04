# ADR 0208: Promote forward x87 stack subtraction

## Status

Accepted on 2026-08-01.

## Context

Revision `efec9c5f89358999a067a4a7c923d06d814d1639` added canonical
`FSUB ST(1), ST(0)` to the shared x86 catalogue and taught CupidC to preserve
both aligned GNU spellings used during the transition. The source catalogue
has 592 forms, 244 canonical mnemonics, 64 register names, and fingerprint
`F4420CB4`. The preceding checked seed still carried the 591-form
`DBE77533` catalogue, so it could not build a later `libm.cc` correction that
uses `fsubr %st, %st(1)`.

The capability revision was committed, tested, and pushed before the seed
candidate was built. The 19-source build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

## Decision

Promote all five stage-three tools as one checked cohort:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `267d5ce820aac6bdfdb418552c3c144f8eac30e8589d8f53bd52055c3adca12d` | yes |
| CupidC | 2,561,644 | `a4dff3c1c8ae975e9b8278920d36aefe6ad9b28a52503a6d5d4253e04e4a21af` | yes |
| CupidDis | 379,648 | `1ceeec3e65423f11a3b937dee355191ca0769cbfc4a374505f2aacf85db56ec8` | no |
| CupidLD | 266,672 | `2bdb6ce6b04678bb89c6bb4f7afac7e152ce6c4a07c4e14e1b3aee0c899008ec` | yes |
| CupidObj | 253,724 | `f78752dc01daf3d2a9dc9265425f9c60639f438d5dcb91a001cf40d7d241ded5` | no |

CupidASM, CupidC, and CupidDis change. CupidLD and CupidObj remain
byte-identical, but they stay in the promotion because the manifest is one
trust unit. The 5,440-byte manifest has SHA-256
`51045d31a941d3bf1c286ed9464b91f6b053eb5ca0d47b031f25353a1d10b2eb`
and names the pushed capability revision above.

## Evidence

The transition bootstrap completed in 658.3 seconds. It froze 41 source
inputs with SHA-256
`6ad00c61fa66a3ad713fe197fc1115fbc1f6cdac2944f75ef162a723203ba0d9`.
All 19 C objects, the startup object, and all five tool images matched between
stage two and stage three. Both stages passed five help cases, ten successful
operations, and six useful failures. Compared with the preceding seed,
CupidASM, CupidC, and CupidDis changed while CupidLD and CupidObj matched.
The 15,056-byte transition report has SHA-256
`1a6b5c4f6ccb239b9dc4f8c6eeb4f68d945297492916695bd72491318a79b9ef`.

After promotion, seed verification passed. A focused checked-seed test
assembled `fsub st1, st0` as `DC E9`, disassembled the bytes to the canonical
instruction, and rejected `fsub st0, st1` without replacing an existing
output. The same frozen cohort used CupidC to compile corrected and legacy GNU
templates to `DC E9` and `DC E1`, then used CupidDis to verify both meanings.
It passed in 2.244 seconds.

The post-promotion bootstrap completed in 652.3 seconds. Every promoted seed
image matched stage two, then all 19 C objects, startup, and five tool images
matched between stage two and stage three. The complete behavior matrix
passed again. The 15,053-byte report has SHA-256
`c8f52bb27b1be7a4e0a29c0353642d9ef13589013c39df839088da031473d810`.

The first complete seed-module run exposed one stale source-snapshot lock:
the report correctly carried `6ad00c61...`, while the test still expected the
preceding seed's `cc2cc479...` snapshot. The other 37 cases passed. After the
lock was corrected, the full fixed-point selector passed in 655.017 seconds.
A final complete module run, including the expanded CupidC carriage proof,
passed all 38 cases in 712.167 seconds. A separate checked-seed compile
reproduced the unchanged 43,736-byte `libm.cc` object in 4.433 seconds,
confirming that promotion alone does not change its production bytes.

The regenerated audit records 717 active inputs, 449 transforms, 254 feature
requirements, and 25 classified unreachable files. Its active-source digest
is `e122c69d045c6cba75645220b10ae141011f590cb2bec7568e50cb46985311c7`.
The 2,546,938-byte JSON has SHA-256
`432bf0764fef8a984daea054b3b61345fdaffea7584e1eae3c9f96cc2b34324b`,
and the 12,136-byte summary has SHA-256
`d7bff321cbd51fa17255c9a5950ce11da1c1a0574f6541df0fdf4189da149e63`.
Read-only regeneration passes.

The first full build-graph test found one stale lexical lock. The capability
contracts increased the generated `sizeof` inventory by seven net
occurrences, from 5,402 to 5,409, while the file count stayed at 168. After
updating that exact lock, all 68 graph tests passed in 566.281 seconds.

## Rejected alternatives

Keeping the preceding seed was rejected because the normal checked compiler,
assembler, and disassembler could not carry the new shared instruction form
into the next active-source correction.

Replacing only the three changed files was rejected. The checked manifest is
one five-tool trust unit, and promotion requires a complete stage-three fixed
point even when some bytes remain unchanged.

Changing `libm.cc` in the promotion commit was rejected. Seed carriage and
the seven active source corrections have different evidence: the former
needs a fixed-point proof, while the latter needs guest numerical checks.

Recording an unpushed source state was rejected. The manifest names the exact
pushed revision that produced the promoted binaries.

## Consequences

The checked seed now carries the 592-form `F4420CB4` catalogue. CupidASM can
emit `DC E9`, CupidDis renders the canonical instruction, and CupidC can build
the aligned corrected exponent and power templates. Active `libm.cc` still
uses the legacy spelling until the next runtime-tested source increment.

This promotion changes the checked trust root without moving a normal build
owner or changing a production source. Python still coordinates the staged
bootstrap, and Windows still runs the static i386 tools through WSL. Those
host dependencies remain open. `TempleOS/` remains untouched reference
material.
