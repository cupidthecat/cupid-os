# ADR 0370: Promote the kernel-symbol CupidBuild seeds

## Status

Accepted on 2026-08-30.

## Context

ADR 0369 added a typed `cupidbuild generate-ksyms` transaction. Source-head
CupidBuild could freeze the pass-one kernel ELF and six-tool seed, capture
CupidDis symbol rows without a shell, compare CupidObj output with an
independent KSYM renderer, and publish through the guarded transaction. The
normal Make recipe could not use the command while the checked Linux and
Windows CupidBuild images predated it.

The new operation changes only CupidBuild. The other five tool sources and
both platform build plans are unchanged. A useful promotion therefore needs a
paired six-tool trust-unit update even though ten of the twelve executable
images remain byte-identical.

## Decision

Promote the paired stage-four CupidBuild images built from revision
`9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6`. Both manifests bind the same
59-input source snapshot:

```text
bac22f6a59871326ec40a58ab143eea1675b689251c76950d43d860cb2539fcd
```

The Linux plan remains
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`.
The native Windows plan remains
`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`.

The promoted images are:

| Platform | Image | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Linux | `cupidbuild.elf` | 340,548 | `b83daf1d4e37739501c56045da30a035477d539bbf20f13994c7c8ada0781b01` |
| Windows | `cupidbuild.exe` | 355,328 | `3ec41c553ee26e37ed1153a13cf666c0ae5de860f92a7f18279c2943046f7588` |

The ten CupidC, CupidASM, CupidDis, CupidLD, and CupidObj images match the
preceding checked pair exactly. The 6,602-byte Linux manifest has SHA-256
`770f979407f930deba0c9ba887bcd14f2350a785b1c0df6b31ddc2659c46eaae`.
The 2,852-byte Windows manifest has SHA-256
`bf6147cf2e8249372869a24e5b8477ffb785d9a48eef80209366cfbaff19c7db`
and names the exact Linux manifest bytes in its pairing field. The original v1
parent fields remain unchanged because they record construction lineage, not
the immediately preceding v2 publication.

## Evidence

The candidate Linux fixed point matched 22 C objects, startup, and all six
tools between stages three and four. Its 27/6/34 behavior matrix passed. The
51,567-byte report has SHA-256
`d94e1f8908bbecb9331a6e6b20c007b64f24a5b6b18b1f2562d000973402d2c2`.

The candidate native Windows fixed point matched 23 C objects, three assembly
objects, and all six PE32 tools. Its 16/6/21 behavior matrix passed. The
64,673-byte report has SHA-256
`1eb9a1d1f0572a226436afcc2e6cde033a39e8466799a6e4f636a09f02b83ab0`.

Both promoted seeds then consumed themselves. Every initial image reproduced
as stage two, and stages three and four remained equal. The Linux reproof
retained the 27/6/34 behavior matrix; its 51,566-byte report has SHA-256
`1630a9a157bd726f5b121d16d6663332ad172d7e189dcf6fc599f4d6a45cfc78`.
The Windows reproof retained 16/6/21; its 64,672-byte report has SHA-256
`6cc52a01a3eb747467c0f640cad23d006f169dc219fd257b56cd4c950b01aa08`.

## Consequences

Both active CupidBuild seeds now carry `generate-ksyms`, including its exact
success and malformed-ELF rollback behavior. Seed carriage makes the direct
Make handoff possible, but does not perform it. The normal kernel-symbol edge
remains Python-coordinated until a separate commit changes production
ownership and proves the complete build and boot path.

The seed promotion itself changes only the two CupidBuild policy rows.
Synchronized embedded documentation moved `kernel/kernel.bin` from 9,511,132
to 9,511,584 bytes, so its exact row was updated after the first size gate
reported the new measurement. No active C source changes suffix: every
Cupid-owned translation unit already uses `.cc`. `TempleOS/` remains
read-only reference material.
