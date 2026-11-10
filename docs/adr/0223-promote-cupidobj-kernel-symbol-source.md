# ADR 0223: Promote CupidObj kernel-symbol source generation

## Status

Accepted on 2026-08-03.

## Context

ADR 0222 added a transactional `ksyms-source` operation to CupidObj. It turns
canonical CupidDis symbol text into the packed `.cc` source used by the
kernel's second link. The checked CupidObj seed predated that command, so the
normal build could not use it without first changing the bootstrap trust root.

The complete capability work was committed and pushed at revision
`6f880cc3cf5cced72b81e0d66079aaca913d0a03` before the promotion candidate was
built. The fixed-point source plan remains nineteen C roots and has SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

## Decision

Promote all five stage-three images as one checked cohort:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `267d5ce820aac6bdfdb418552c3c144f8eac30e8589d8f53bd52055c3adca12d` | yes |
| CupidC | 2,574,032 | `8d810739494123a3da1cba34f75f58c005e8796f2cb4e85ba57eead1578a1f4d` | yes |
| CupidDis | 379,648 | `1ceeec3e65423f11a3b937dee355191ca0769cbfc4a374505f2aacf85db56ec8` | no |
| CupidLD | 266,672 | `2bdb6ce6b04678bb89c6bb4f7afac7e152ce6c4a07c4e14e1b3aee0c899008ec` | yes |
| CupidObj | 270,700 | `a8de7de19d1ffbec90f0603f0f796f4a03fa74b8181c62f0f395b22a52423d1d` | no |

Only CupidObj changes from the preceding cohort. Its earlier image was 253,724
bytes with SHA-256
`f78752dc01daf3d2a9dc9265425f9c60639f438d5dcb91a001cf40d7d241ded5`.
The other four binaries remain byte-identical, but the manifest treats all five
as one trust unit.

The 5,440-byte manifest has SHA-256
`729cd702e34695cf9ee619d10446ce80838ed9a25a14efa856833b2bf37629f3`.
It names the pushed capability revision and retains the static i386 Linux ABI,
producer lineage, build plan, and five link orders.

This decision promotes the command without changing the normal kernel-symbol
recipe. Python remains its production owner until a separate change transfers
that recipe with its own build and boot proof.

## Evidence

The transition bootstrap completed in 720.9 seconds. It froze 41 source inputs
with SHA-256
`26555c8a95721689f502fea47c52da8911d10307af3142d82b4da0a53d0bfba0`.
All nineteen C objects, startup, and five tool images matched between stage two
and stage three. Both stages passed five help cases, eleven successful
operations, and seven useful failures. Compared with the preceding seed, only
CupidObj differed from stage two. The 15,054-byte transition report has
SHA-256
`2a58f7c69b4f423f459b04b1553d029baf46570b9bb323bf144d0496d48a05c0`.

The new carriage test first failed against the old seed because CupidObj
printed its usage text and returned status two for `ksyms-source`. With the
promoted seed, it passed in 1.323 seconds. It reproduced the Python oracle's
source exactly, rejected an invalid address on line two, and kept an existing
destination unchanged. `make verify-bootstrap-seed` also passed. The other 39
checked-seed tests passed in 89.345 seconds.

The post-promotion reproof completed in 759.4 seconds. All five promoted seed
images matched stage two, then every object and image matched between stage two
and stage three. It repeated the same source snapshot and behavior matrix. The
15,053-byte report has SHA-256
`e44d6387ff1963814ba21dc000f6998cfc324851689addfc887b6260516cb0e9`.

The active-build audit and its independent drift check passed together in
245.5 seconds. They retain 718 active inputs, 449 reachable transforms, 255
feature requirements, and 25 classified unreachable files. The active-source
digest remains
`48a25995a6eb517807dca2f77234ed953ca7ae967845fad446c9a011d0941f75`.
The 2,554,943-byte JSON has SHA-256
`9818abc044ac022d19a1b50727b4a902c4af2b4b5e8c6ad54bb0ef504d365d2d`,
and the 12,136-byte summary has SHA-256
`0d1ce1a07ffe3d4d17e84814f55872c3dc9f09f3ec436d1c5381595f076b704b`.

## Rejected alternatives

Keeping the old seed was rejected because the next production handoff would
have depended on a command that the checked trust root could not run.

Replacing only `cupidobj.elf` was rejected because the manifest verifies and
promotes all five images as one cohort, even when four remain unchanged.

Pointing the manifest at an unpushed worktree was rejected. Its source revision
names the pushed commit that produced the candidate.

Combining seed promotion with production ownership was rejected. Seed carriage
proves that the command is reproducible; changing the two-pass kernel pipeline
also needs ordered invocation, drift, parity, full-image, and guest evidence.

## Consequences

Checked-seed CupidObj can now generate kernel-symbol source and carries the
same positive, negative, rollback, and fixed-point behavior as source head.
Later checked builds can use the command without a native compiler or a new
bootstrap binary.

No normal build owner moves in this decision. Python still coordinates the
checked bootstrap, Windows still executes the static tools through WSL, and the
normal symbol recipe still uses Python to render the source. Those are separate
ownership boundaries.
