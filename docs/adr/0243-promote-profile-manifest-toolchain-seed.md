# ADR 0243: Promote profile-manifest into the checked Toolchain seed

## Status

Accepted on 2026-08-08.

## Context

Revision `aeef93513e6ac899c933a09e4cacf05ef8b047df` adds CupidObj's
bounded, transactional `profile-manifest` operation. The preceding seed can
compile that source, but its CupidObj image stops at option parsing because it
predates the command.

The 19-source build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
The source closure still contains 41 files.

## Decision

Promote all five stage-three images as one checked cohort:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `1dc9061912f127d231d320940ba781781af663bde83852a613910394709ecc76` | yes |
| CupidC | 2,582,400 | `03084115bcacb1987db5513c8a8be9b7d884029b03ab4b212bf40d997871ae79` | yes |
| CupidDis | 379,648 | `a45fc4c57afd3bb02980e514d58c11588ba3a8bfa2f05ca348fe465cfdaf9749` | no |
| CupidLD | 266,672 | `2bdb6ce6b04678bb89c6bb4f7afac7e152ce6c4a07c4e14e1b3aee0c899008ec` | yes |
| CupidObj | 392,688 | `7137ad601a7c22178112fbf08163b36ff2064807caa99962df97d7ae7ae62f2b` | no |

CupidObj changes from the preceding cohort. The other four files are
byte-identical, but the manifest binds all five because they came from one
fixed-point generation. The 5,440-byte manifest has SHA-256
`bbc989d7008507a2961a5f940875270fb48b68bf7afb993f5774d70aea17fe91`.
It names the pushed source revision and retains the static i386 Linux target,
producer lineage, link orders, and build plan.

The fixed-point behavior gate moves with the seed. Both rebuilt stages must
list `profile-manifest`, hash the padding and repeated-block vectors, emit one
exact canonical manifest, and preserve existing outputs for truncated,
unsafe-path, and case-collision failures. The matrix is five help cases,
fifteen successful operations, and thirteen useful failures.

A direct checked-seed carriage test provides a separate boundary. It hashes an
empty header and a 129-byte header, checks exact canonical JSON, rejects an
unsafe path with a useful diagnostic, and preserves the old output sentinel.

## Evidence

The pre-promotion transition completed in 904.2 seconds with `CC`, `CXX`,
`CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `NM`, and `OBJCOPY`
set to commands that could not run. It froze 41 source inputs with SHA-256
`bbbeb2b9f1532c9e7574ec47bb05c428f308fa430cf5fafe33b6222488b1ea33`.
All 19 C object pairs, startup, and five rebuilt images match between stage
two and stage three. CupidObj differs from the preceding seed; the other four
seed images match. The 15,058-byte transition report has SHA-256
`6ef11227e3976131a45c270742559a05221d3e8627cd927a0201cbb9b844dc7d`.

Before promotion, the direct carriage test failed at option parsing with
status 2 and left no generated manifest. At that boundary, source support was
complete, but the checked seed did not carry it.

After promotion, the same test writes the exact 647-byte JSON file with
SHA-256
`3a33b2d2fd28187ae7b9538c7e068706c8d1fd6677b7d9b134547cd4626b230d`.
It rejects the unsafe path and leaves the existing `sentinel` bytes intact.
`make verify-bootstrap-seed` accepts the manifest and all five static ELF32
images.

An independent post-promotion rebuild completed in 794.6 seconds with the
same host code-generator commands poisoned. Every seed image matches stage
two, and stage two again matches stage three across all 19 C objects, startup,
five tools, and the 5/15/13 behavior matrix. Its 15,057-byte report has
SHA-256
`a62c62addd00decb2e656c24e3281e40bcc635dd82eead235d6187ee861f5a7c`.
The complete checked-seed module then passed all 45 tests in 868.426 seconds.

The promotion-only active graph regenerated in 71.5 seconds, and its stale
check passed in 69.2 seconds. The 68-test graph-audit module passed in 606.756
seconds. The graph remains at 719 active inputs, 449 transforms, 255 feature
records, and 25 accounted unreachable files. Its 2,558,749-byte JSON has
SHA-256
`f091427ffc79ca25a7d1af099a1969918deab6fe1e89a293823dacd31afbb8cf`.

## Rejected alternatives

Replacing only CupidObj was rejected. The unchanged images were rebuilt and
compared in the same generation, and the checked manifest is a five-tool trust
unit rather than a set of unrelated caches.

Leaving `profile-manifest` outside the fixed-point behavior gate was rejected.
Source compilation alone would not prove that both runnable stages expose the
command, hash long inputs correctly, or preserve outputs on semantic failure.

Moving the production Doom recipe in this commit was rejected. Seed carriage
proves that the command is available. Filesystem snapshots, independent Python
parity, live-input drift checks, locking, and atomic publication remain a
separate handoff.

## Consequences

Checked-seed CupidObj carries deterministic profile-manifest authoring. Python
continues to author the normal Doom input manifest until the guarded publisher
uses this command. No normal-build transform changes ownership in this
promotion, and no ordinary C or assembly source changes ownership, so no `.c`
to `.cc` rename is due. `TempleOS/` remains untouched reference material.
