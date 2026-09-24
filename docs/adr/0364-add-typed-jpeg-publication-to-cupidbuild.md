# ADR 0364: Add typed JPEG publication to CupidBuild

## Status

Accepted on 2026-08-29.

## Context

CupidObj already validates the repository JPEG and writes its deterministic
i386 relocatable object. The normal recipe still enters that operation through
Hostbuild, which freezes the input and seed, runs an independent Python JPEG
check, and publishes the private candidate. A generic checked-tool call cannot
replace that coordinator because it does not own an output lock, candidate,
publication boundary, or rollback.

The JPEG transaction is small enough for CupidBuild's existing guarded host
layer. It needs one source, one manifest, and the six seed images, below the
sixteen-input limit. The 800,393-byte asset and its 800,860-byte object also fit
the 64 MiB per-file limit.

## Decision

Add a typed `cupidbuild embed-jpeg` operation. It accepts the production seed
manifest, repository root, repository-relative source, and repository-relative
output. The operation requires a promoted six-tool seed, freezes the source and
complete cohort, and runs the frozen CupidObj image as:

```text
wrap-jpeg PRIVATE_SOURCE --identity ORIGINAL_SOURCE -o PRIVATE_CANDIDATE
```

The original logical source spelling remains the symbol identity. The private
filename never appears in the exported `_binary_*` names.

After CupidObj succeeds, CupidBuild pins the candidate and requires a data-only
i386 `ET_REL`. Its only allocated payload is a writable, byte-aligned `.data`
section that exactly matches the frozen JPEG. The global start, end, and size
symbols must use the original logical identity and point to the expected
offsets. Relocations and other allocated payload sections are rejected. A
separate native parser then checks the frozen JPEG as one sequential SOF0 or
SOF1 frame. It accepts stuffed entropy bytes and restart markers and rejects
the same 21 malformed or unsupported classes as the existing CupidObj and
Python contracts, with the established diagnostics.

Before publication, CupidBuild rechecks the live source, manifest, seed
membership, all six tool images, private candidate, owner lock, output parent,
and existing destination. Publication uses the guarded transaction's pinned
parent and atomic replacement. Failure preserves the previous object and
removes the private transaction.

The shared host diagnostics now describe a source, output, and artifact rather
than a CupidASM-specific object. The existing assembly operations keep the
same behavior with accurate generic messages.

Both fixed-point behavior matrices run the typed success path through their
two compared CupidBuild generations, require byte-identical relocatable
objects, reject a progressive JPEG, and require both sentinel outputs to
survive. Their source-head inventories become 26 failures, six help cases, and
33 successes on Linux, and 15 failures, six help cases, and 20 successes on
native Windows.

This decision adds source capability and fixed-point carriage only. The active
seeds predate `embed-jpeg`, so the normal Make recipe remains on Hostbuild until
paired seed promotion proves and publishes the command.

## Evidence

The complete native Windows CupidBuild module passes 76 tests in 86.227
seconds, with three platform skips. It covers exact output parity and source
identity, malformed-input rollback, live-lock contention, source drift, seed
drift, competing output drift, manifest alias rejection, parser wiring, and
cleanup.

The dedicated native validator contract accepts SOF0, SOF1, and entropy data
with stuffing and a restart marker. It also checks all 21 rejection classes and
their exact diagnostics. Its object cases require exact payload bytes, section
policy, exact start, end, and size semantics, no relocations, no extra allocated
payload of any ELF section type, and no extra symbol. The focused Make target
passes.

The fixed-point coordinator's three focused registration, inventory, and
transaction contracts pass. The source-head success and failure CLI cases pass
against the production seed. The source-current fail-closed audit mutation
test passes in 248.702 seconds, including its dead-call, guard-removal, and
validator-wiring mutations. CupidC's 65 toolchain-contract tests also pass,
including compilation and linking of the changed CupidBuild sources.

A direct source-head run wrapped the active repository JPEG to 800,860 bytes
with SHA-256
`74ab86d88302c90385bb0b858632b0d6c4ac983d6be28c976dd1a3a348204b3e`,
matching the established production object exactly.

The active-source audit and its independent checked replay passed with the
new behavior counts and typed capability record. The generated JSON is
2,770,808 bytes with SHA-256
`1bd0058ebdf89a9ce8820e41fb564535280bc3df0c77d90c2b26eb85c015f191`.

The complete OS build compiled all 83 Doom roots, ran both kernel links
through CupidBuild, passed whole-image CupidDis validation, and stopped at the
exact-size gate on the three documentation-bearing kernel outputs. After the
first measurement update, review-driven contract and CTXT corrections forced a
second full build. That pass kept both ELF sizes stable and rejected only the
flat kernel, which grew another 184 bytes. Updating that one measured row let
the replay accept all 16 artifacts and publish the image. The final outputs
are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,601,080 | `eb69dd23dab38ffebb854c05b1e928f4438f274aec680be4747a61470aecf0d1` |
| `kernel/kernel.elf` | 9,732,152 | `70740dfd19c2bf1120463745e821c5c6a11e171adcdb0c81a088592bff639e3d` |
| `kernel/kernel.bin` | 9,504,508 | `47c1e47997704276fe201e8d8b7c0b998eaf8d30b55af1796a40e131770518e2` |
| `cupidos.img` | 209,715,200 | `604a48143344a0bbd3e18e1b625a689265b48ce8fb7510b787d9f6d2d337b09d` |

A private four-vCPU E1000 smoke passed the strong SMP runtime gate and ran
`/bin/ls.cc` to normal JIT completion. Its 35,451-byte serial log has SHA-256
`5ad14de3efdd72dc036f8f6c62fea3c868cfd10b946f88efe36427e9e8e9741b`.

## Alternatives considered

Using `cupidbuild run --tool cupidobj` was rejected because the generic runner
does not own a destination or publication transaction. Removing the
independent parser was rejected because CupidObj acceptance alone would leave
no separate native veto inside the future Python-free transaction. Moving the
Make rule now was rejected because a clean checkout would call a command that
the promoted seed does not yet contain.

## Consequences

Source-head CupidBuild can now own the complete JPEG object transaction after
paired seed promotion. The normal graph remains at 452 transforms, with
CupidBuild in 192 and Python in 260, until that later recipe handoff.

The new native contract is a `.cc` source. No active `.c` source is introduced
or left for renaming. `TempleOS/` remains untouched reference material.
