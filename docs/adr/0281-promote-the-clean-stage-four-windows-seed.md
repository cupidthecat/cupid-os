# ADR 0281: Promote the clean stage-four Windows seed

## Status

Accepted on 2026-08-14.

## Context

The checked i386 Windows execution seed came from an earlier paired-stage
proof. It already ran output-bearing production commands directly on Windows,
but its provenance predated the large-frame stack-probe change and the
four-generation convergence rule in ADR 0279.

The preliminary uncapped Windows run proved that stages three and four matched
on a frozen worktree. It could guide the investigation, but it could not name a
clean source revision. The Linux seed was promoted first, giving the Windows
driver a clean and verified plan manifest. The Windows promotion gate then
required a fresh proof from a named clean commit and a second proof from the
promoted PE32 cohort.

## Decision

Promote the five native stage-four tools built from clean revision
`bd8fd28e6e0e097c4ee3a5c5de0b0706b7153930`:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 437,760 | `8134a9400c4cae7e6c7e72989aa9b23bbdcb56ba4d52a9ebb15363128e4a1f18` |
| CupidC | 2,595,840 | `706c427d8e89352623274ad8e3321680a89c58c08d1d90a279a8d5ad814668e0` |
| CupidDis | 387,584 | `07cff807224c425d686e32d54dc1ad541f57aaa624f7b736bba0f9ef5001ce6a` |
| CupidLD | 296,448 | `9fe3bd4fda9b87d678aa2eb6305e65b706ecdff074b16722faab23ce05cd8e02` |
| CupidObj | 375,808 | `079bc115e74772e6224e4da164115cc5696e357cca0cb1a0583985b88381cb79` |

The 2,118-byte execution manifest has SHA-256
`96bb80521ba679161008c9fa0891aff9d7ae172868cde107ff1a78feebdccfc9`.
It records the `paired-stage-four-native-windows` generation, the clean source
revision, the 50-input source count and snapshot, and the native stage-three
CupidASM, CupidC, and CupidLD producer lineage. It also records
`make bootstrap-windows-from-seed` as the passing fixed-point command. The
source snapshot SHA-256 is
`d8481a39e0d1c7f42779a8c9f5fc5de10d7e5b9bc4df63ce6afe9ddd9c9716da`.

The manifest keeps the Linux bootstrap plan as an explicit parent. Its parent
manifest has SHA-256
`f8528f5fcb68473f5078427dfc1c7dd5fce78413a56b45c6aa831971d827ca4f`
and source revision
`5d690c7508cc031a0cb32b2963bf16300b32e267`. The Windows manifest still omits
a build plan. Native reconstruction verifies and freezes the Linux manifest as
a separate input.

The five PE32 images remain one execution-seed trust unit. CupidASM, CupidC,
and CupidLD are its producer trio. CupidDis and CupidObj move with them as
checked outputs.

## Evidence

The clean Windows proof from revision
`bd8fd28e6e0e097c4ee3a5c5de0b0706b7153930` passed in 1,152.7 seconds. Stages
three and four matched across 20 C objects, two assembly objects, and all five
linked tools. Both stages passed five help cases, five successful operations,
and five useful failures. The report records `stage3=4` and binds the 50-input
snapshot above and the promoted Linux parent manifest.

The previous execution seed crossed the expected transition into stage two.
Its initial comparisons were false for CupidASM, CupidC, and CupidDis, and true
for CupidLD and CupidObj. That vector records the old-to-new seed transition;
the promotion decision rests on equality between stages three and four.

After promotion, a second proof started from the new manifest and passed in
1,130.9 seconds. All five initial seed comparisons are true. Stages three and
four again match across the 20/2/5 artifact set, and both stages pass the 5/5/5
behavior matrix. The reproof ties every checked PE32 image to the manifest
provenance above.

The focused Windows seed and native-boundary suite passed 15 tests in 16.452
seconds. The separate build-graph seed-role test also passed. Final review
found that JSON numeric equality let a floating `50.0` stand in for the integer
source count. New Windows and Linux negative cases reproduced that error, and
both pass after the verifier began requiring an exact integer type.

The full native user-equivalence target did not finish. Two captured attempts
timed out after 1,204.051 and 2,104.041 seconds without publishing the checked
contract cohort. An isolated compile of the last observed heavyweight source,
`toolchain/tests/cupidc_object_contract.cc`, passed in 826.192 seconds and
produced a 2,497,288-byte i386 object. The contract builder runs two sources at
once but gives every compile the same 900-second limit. That leaves 73.808
seconds of idle-host headroom for this source, so parallel contention can
exhaust the limit even when the compiler is correct. This gate is recorded as
timed out, not passed. Per-plan timeout and scheduling work remains a separate
follow-up.

The regenerated active-source audit retains digest
`f7af8cce01680c74bb452ed6ac018471bc26cc1d37f0b94bf2b70c5fa4d497f0`.
Its 2,673,345-byte JSON report has SHA-256
`d30b4b9f747a567f36f42e85ad621d8359f62d174f1fbdda403ee5bffacc5964`.
The 12,269-byte Markdown summary has SHA-256
`1cb16ea4cbf4ec84d447bcb9e85b8ea2062078fec32a5e5254bfc700cc2d39ec`.

The earlier uncapped Windows proof remains preliminary history in ADR 0279. It
began from uncommitted source and is not substituted for either clean run.

## Rejected alternatives

Promoting the preliminary Windows outputs was rejected because their report
could not name a clean source revision.

Keeping the older paired-stage manifest was rejected. Its binaries could run
the production workload, but its provenance did not describe the current
four-generation convergence rule or the promoted Linux plan parent.

Promoting only the three producer images was rejected. The execution seed is a
five-tool trust unit, so CupidDis and CupidObj must carry the same source and
producer provenance.

Skipping the post-promotion run was rejected. The first proof validates the
candidate. The second proves that the checked PE32 inputs reproduce all five
of their own images.

## Consequences

Windows output-bearing production commands now run the promoted stage-four
PE32 cohort with clean fixed-point provenance. The native reconstruction gate
remains 20 C sources, two assembly sources, and five tools, with the 5/5/5
behavior matrix.

This promotion removes no host-control dependency. Python still coordinates
the bootstrap, and Linux-seed contracts on Windows still use WSL. The promoted
tools carry CupidASM `--map` and CupidDis `--range-map`, so the guarded
bootloader publisher no longer waits on seed capability. Moving that publisher
onto the normal Make edge remains separate work.

No active source rename is due. The fixed-point source plan already uses
`.cc`, the remaining tracked `.c` files are outside the supported Cupid-owned
roots, and `TempleOS/` remains read-only reference material.
