# ADR 0367: Promote the stddef and typed JPEG seeds

## Status

Accepted on 2026-08-29.

## Context

ADR 0364 added the typed `cupidbuild embed-jpeg` transaction. ADR 0365 then
added the hosted `<stddef.h>` needed by its public `size_t` interface, raising
the frozen toolchain closure from 58 inputs to 59. The first paired candidates
converged, but their CupidBuild images still rejected a promoted manifest with
59 inputs. ADR 0366 repaired that bounded transition while retaining the
existing 58-input generation and rejecting every other v2 count.

The provisional candidates that exposed the parser mismatch were not suitable
for publication. They were built from the source revision before ADR 0366 and
could not consume the closure they claimed. A promotion therefore required a
fresh Linux and native Windows reconstruction from the named compatibility
commit, followed by another self-consumption proof after the candidate bytes
became the checked seeds.

## Decision

Promote the paired stage-four generation built from source revision
`cac3c08fb0dd7c22299e1a2475a49f51982549a2`. Both manifests bind the 59-input
snapshot with SHA-256
`3c3218219472735ba1073e1ca7b1f67ee75bf123fb0be77d2c65e019a6aebdef`.
The Linux build plan remains
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`;
the native Windows plan remains
`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`.

The Linux manifest is 6,602 bytes with SHA-256
`f55ec976a78701595b3da58c5d75c5e49ba61a5329e7cf39d814adfc0e9b255f`.
The Windows manifest is 2,852 bytes with SHA-256
`b966ecaafe4acf76d563f2698c2a185487696fa11a96c63ff0b88fc901ad0573`.
The Windows record binds that exact Linux manifest, so the two platform seeds
remain one promotion unit.

Only CupidC and CupidBuild differ from the preceding checked cohorts. The
other eight images reproduced byte for byte. The complete promoted inventory
is:

| Platform | Tool | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Linux | CupidASM | 496,664 | `1517bff9353ae7663825dbcee20084a50e296061b3085bab2c0719eea714c770` |
| Linux | CupidBuild | 315,252 | `3947aee09776ab5f6c79daba443b47fc67de5ea0805741533cf4c2d42a039300` |
| Linux | CupidC | 2,691,756 | `e50758041199044e269e6b6dae52065cc2de2153efeb13b6b6983279ee2935c0` |
| Linux | CupidDis | 538,556 | `4a1326e12291c83e2193cf27630b9271d1c299faf39db9ad7fa74d11cd52fc47` |
| Linux | CupidLD | 312,928 | `0dd697544f4806cf1d769cf59a8a7c37d7355f8360f3513458bfff2261c8a5cb` |
| Linux | CupidObj | 392,820 | `e9958b28c3230fe83c4bf409797208d735887c54d4ebffd0565b4a91f45142fb` |
| Windows | CupidASM | 479,744 | `9c50e204262a0b05b12d4fc0924670c66092d053ad12b99134ab79a254ef07ae` |
| Windows | CupidBuild | 333,312 | `fea9253e9d571433dee56e92e8801bd12635020b6838839dfd8096a20b6d5908` |
| Windows | CupidC | 2,620,416 | `fb7efa82fdcffa6a36a5c44bb83abe5b6a10ce7487c946eb3fab206e436b8522` |
| Windows | CupidDis | 516,608 | `588485d496209eecf437e6f6fc9d02474d5c4ac1f236af86bdaad9f3f2d705ce` |
| Windows | CupidLD | 296,960 | `aaa7b51a290646ef1d972f4904b1ed176a4dc912e53c1bc4cbdd8d1e39d8495f` |
| Windows | CupidObj | 375,808 | `b6f6a5b66f8e2bcb4b779a16428d7b77a956113c5ca301344537b35839611572` |

Keep the v1 parent lineage recorded in both manifests. Promotion changes the
checked child generation; it does not rewrite the history of the seed that
started the fixed point.

## Evidence

The fresh Linux candidate report is 51,567 bytes with SHA-256
`bee8f3683ead007d39cbc2707d229d712b2acc44ad3b79ebcd333143c551665f`.
Stages three and four match across all 22 C objects, the startup object, and
six tools, and the 26/6/33 behavior matrix passes.

The fresh native Windows candidate report is 64,673 bytes with SHA-256
`9977f4914940548ed0504eda4bc80224302fc64faafe67964edbe089394cab86`.
Stages three and four match across all 23 C objects, three assembly objects,
and six tools, and the 15/6/20 behavior matrix passes. In both candidate
reports, the initial seed matches stage two for CupidASM, CupidDis, CupidLD,
and CupidObj. CupidC and CupidBuild differ; those are the only two images
changed by this promotion.

The checked manifest verifiers accepted all six promoted images on each
platform before the candidates were used for a second fixed-point run.

Linux then rebuilt from the promoted seed in about 39 minutes 17 seconds. Its
51,565-byte report has SHA-256
`13dbb5599e6d0fdeabe1e4166e007509df7a7569db1f7069eef42266f43c7c0c`.
All six initial images equal stage two, all 22 C objects, startup object, and
six tools match between stages three and four, and the 26/6/33 behavior matrix
passes. An independent inventory and rehash found no mismatch among the 29
stage-four files.

Native Windows rebuilt from its promoted seed in about 34 minutes 23 seconds.
Its 64,671-byte report has SHA-256
`b487fde4080718df4d44c1013d341b77dca0bfee080b3d65fc89abc96b4a6a98`.
All six initial images equal stage two, all 23 C objects, three assembly
objects, and six tools match between stages three and four, and the 15/6/20
behavior matrix passes. A direct binary comparison and independent rehash
found no mismatch among the 32 stage-four files.

The complete repository validation is recorded in the bootstrap log alongside
this decision.

The final OS replay found one more 58-only consumer in the Cupid-built
artifact-size contract. Its Linux and Windows manifest parsers now accept the
same bounded 58/59 transition as CupidBuild and reject 57 and 60. The contract
also requires both manifests to use the same admitted count, so neither mixed
58/59 ordering can pass as a paired seed. Focused tests cover the active pair,
the retained 58-input pair, both mixed-count failures, and the neighboring 57
and 60 rejections.

## Alternatives considered

Publishing only CupidBuild was rejected. CupidC also changed when the hosted
header and strict `__builtin_offsetof` support entered the closure, and a
checked cohort is promoted as a complete six-tool unit.

Describing the candidates as a 58-input generation was rejected. `<stddef.h>`
is a real compiler input, so excluding it would make the manifest's
provenance false.

Reusing the provisional pre-ADR-0366 candidates was rejected. They had found
the compatibility fault, but their own CupidBuild images could not consume a
59-input promoted manifest. Fresh construction from the repaired revision was
the only useful promotion evidence.

## Consequences

Both checked seed cohorts now carry hosted `<stddef.h>`, strict
`__builtin_offsetof`, and the typed JPEG transaction. The normal JPEG Make
edge remains Python-coordinated until a separate handoff moves that production
ownership to CupidBuild and proves rollback, audit, build, and runtime behavior.

The normal bootstrap closure has 59 inputs on both platforms. All active C
toolchain sources keep the `.cc` suffix, and `TempleOS/` remains untouched
reference material.
