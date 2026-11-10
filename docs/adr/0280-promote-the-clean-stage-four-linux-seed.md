# ADR 0280: Promote the clean stage-four Linux seed

## Status

Accepted on 2026-08-13. The dependent native Windows seed promotion is
complete under ADR 0281.

## Context

ADR 0279 added a fourth generation because the large-frame stack-probe change
altered compiler-produced objects. Stage two and stage three can describe the
transition from an older seed to the new code generator. Stage three and stage
four are the convergence pair.

The first uncapped Linux run proved that pair on a frozen worktree, but its
source was not committed. It could guide the review, but it could not supply
seed provenance. The fixed-point drivers later added repeated live-seed checks
and explicit lineage for the reconstructed Windows CupidDis main objects. The
promotion gate therefore required a new Linux proof from one named clean
commit, followed by a second proof from the promoted seed.

## Decision

Promote the five stage-four i386 Linux tools built from clean revision
`5d690c7508cc031a0cb32b2963bf16300b32e267`:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 454,160 | `7d6c4a538dcbb04663514445474dae394a6d8fead08c454885315777bf3e3867` |
| CupidC | 2,670,420 | `cafea40e4b5f5c3b68616e83c173555be6b0321e854bc31b2c540c5072f9c495` |
| CupidDis | 409,020 | `9b0983c087ac149380d8ef710987e9e799ebca7534da1030073a63d3395a00d8` |
| CupidLD | 312,792 | `a2119556894903b662d2e131a9a2436b99a3afdd1b1600a3df4d4669569a0295` |
| CupidObj | 392,688 | `99111b5db7586ac4b2ed00005f2fe2e89c66ed48f007d796206b116a088cdf7a` |

The 5,573-byte manifest has SHA-256
`f8528f5fcb68473f5078427dfc1c7dd5fce78413a56b45c6aa831971d827ca4f`.
Its provenance names generation four, the clean source revision, the 50-input
source count and snapshot, and the stage-three producer set that built the
promoted images. The snapshot SHA-256 is
`d8481a39e0d1c7f42779a8c9f5fc5de10d7e5b9bc4df63ce6afe9ddd9c9716da`.

The complete five-tool cohort remains one trust unit. CupidASM, CupidC, and
CupidLD are the producer trio. CupidDis and CupidObj remain checked outputs,
but their images and provenance move with the producers.

The Windows execution-seed verifier keeps an explicit historical parent-source
revision for the existing PE cohort. It does not alias that value to the
current Linux seed revision. This keeps the old PE seed verifiable while it
reproves against the promoted Linux plan. The Windows manifest is unchanged by
this Linux promotion. A clean native proof must establish its next parent
provenance before that manifest changes.

## Evidence

The clean Linux proof from revision
`5d690c7508cc031a0cb32b2963bf16300b32e267` passed in 1,383.775 seconds.
Stages three and four matched across all 19 C objects, the startup object, and
all five linked tools. Both stages passed five help cases, 18 successful
operations, and 16 useful failures. The report binds the 50-input snapshot
above.

After promotion, a second proof started from the new manifest and passed in
1,411.998 seconds. All five initial seed comparisons are true. Stages three
and four again match across the 19/1/5 artifact set, and both stages pass the
5/18/16 behavior matrix. This reproof ties the checked files to the stage-four
provenance recorded in the manifest.

The regenerated active-source audit retains digest
`f7af8cce01680c74bb452ed6ac018471bc26cc1d37f0b94bf2b70c5fa4d497f0`.
Its 2,673,345-byte JSON report has SHA-256
`381d62062c677b05ffa7bd87d52a985f0837e7772194ef66ce2e7aa27ad0845f`.
Its 12,269-byte Markdown summary has SHA-256
`1cb16ea4cbf4ec84d447bcb9e85b8ea2062078fec32a5e5254bfc700cc2d39ec`.

The standalone artifact-size policy module passed all 12 tests in 2.130
seconds. A production `make verify-artifact-sizes` attempt reached a
seed-triggered kernel compile, timed out after 604 seconds, and was stopped
cleanly. That production Make gate has not yet completed and is not counted as
a pass.

The earlier uncapped Linux and Windows runs remain useful preliminary history
in ADR 0279. They began from uncommitted source and are not substituted for the
clean Linux proof above. ADR 0281 records the later clean native Windows proof
and promotion against this Linux plan.

## Rejected alternatives

Promoting the preliminary Linux output was rejected because its report could
not name a clean source revision.

Promoting stage three was rejected because ADR 0279 defines stages three and
four as the convergence pair. Stage four is the output built by the converged
stage-three producer set.

Skipping the post-promotion run was rejected. A transition proof validates the
candidate, while the second run proves that every promoted input seed image
reproduces itself.

## Consequences

The checked i386 Linux bootstrap seed now has clean stage-four provenance. Its
normal Linux and WSL consumers receive the five identities recorded above.
The fixed-point plan remains 19 C sources, one startup object, and five tools,
with the 5/18/16 behavior matrix.

This promotion removes no host-control dependency. Python still coordinates
the bootstrap, and Windows uses its separately promoted PE execution seed for
output-bearing work. No active source rename is due, and `TempleOS/` remains
read-only reference material.
