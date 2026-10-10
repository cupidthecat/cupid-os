# ADR 0430: Publish complete ISO images through retained transactions

## Status

Implemented and qualified in the source API, direct command and producer plans
on Windows and Linux. Native and Cupid-built callers pass, including the complete
retained fixture. Complete staged source-cohort qualification, seed installation
and normal recipe handoff remain open.

## Context

ADRs 0423, 0424 and 0427 through 0429 supply complete input capture, bundle
transport, the 528-file transaction, observer binding and private-input capacity.
The normal ISO publisher still coordinates those obligations in Python. The
native operation must retain their full contracts together through replacement.

## Decision

Add `cupidbuild_iso_publish` with an explicit root, fixture manifest and tree,
output, both seed manifests and selected release. Reserve all 528 inputs before
borrowing a frozen path. Open the capture observer after transaction namespace
creation, compare its manifest bytes with the implicit frozen source, and freeze
each captured file under a short private name. Require captured and frozen bytes
to agree while retaining original identities and output-alias rejection.

Capture and freeze the release, both manifests and all twelve images. Validate
the complete pair against the release and the Windows reference against actual
Linux manifest bytes. Each cohort directory has exactly its selected manifest
and six declared images. Check every image's declared size/digest and actual
i386 execution profile, including the opposite host's cohort. Only the host's
frozen CupidObj becomes executable and runs. The caller authorizes the release;
the operation checks its claims and lifetime without authenticating it.

Encode the complete captured request into CUPISO1. Its retained private input
uses exactly 68,165,648 bytes of capacity. Compare the captured bundle snapshot
with encoded bytes, require it before and after execution, and preserve the
existing 60-second author deadline. Independently compare every candidate image
byte with captured input and layout before calling `publish_if_changed`.

Borrow the capture observer through every publication boundary. Recheck private,
frozen, original and candidate inputs around authoring and validation. Reject an
output inside the fixture tree or either seed directory. Equal image bytes retain
the old timestamp. Ordinary rejection restores verified prior output or absence
under the existing transaction recovery rules. Close the transaction before its
borrowed observer and output parent. A result reports an actual committed
replacement even if a later cleanup fails; other failed result fields are zero.

## Evidence

The source command is `publish-iso-fixture`. It requires `--root`, `--manifest`,
`--fixtures`, `--output`, `--linux-manifest`, `--windows-manifest` and
`--seed-release`; duplicate, missing and unknown options fail before publication.
The native Make link and both source producer plans carry the shared bundle
codec and all four ISO modules. Capture retains mutable allocation owners
separately from its borrowed const views, so the strict native build can release
them without discarding const qualifiers.

The current Linux producer plan has 34 C sources and SHA-256
`ac8edd3ceb4e253439858bbe77c2674933517ec7939bcbe81f1b65ada0d921e3`.
The shared reader retains the earlier bundle and installed plans. Its five new
Windows profiles bind the Linux plan, Windows plan and source count together:

| Profile | Source inputs | Windows plan SHA-256 |
| --- | ---: | --- |
| ANSI | 85 | `0787562d0768485fa614c941fc79a7c6e329c64b6956261cca945a95c2ef9f56` |
| UTF-8 | 90 | `e3bb4c45bb7633d95b205dcbab6405bb569b4cc71965a2eb52b8dfc78e370e18` |
| UTF-8 with long paths | 91 | `5f6a59e696fb7edafdc5dda0b0cc67aa06550556a39816f27081b5a41a81adfc` |
| UTF-8 with final-path aliases | 91 | `d04c045db6492070389894c81364d5a6eada0ee135373f9d2ea1954386aaeb88` |
| UTF-8 with long paths and aliases | 92 | `0dfd1982dc1cd7c9d625c4c0546fc20f13fe3c9ae4dc8cbcf8835ff2e6b4e12d` |

These are source contracts. Complete staged qualification and seed installation
remain separate requirements.

Nineteen native methods pass on each host. They cover the active fixture, equal
timestamps, absent-output recovery, empty members, order and CRLF, all 512 files,
maximum names and depth, UTF-8 and long host paths, both complete cohorts,
release/pair/image corruption, matching digests with an invalid image profile,
missing images, directory links, invalid parents, shared input identities,
output aliases, bounded diagnostics and null arguments. A profile-valid faulty
author writes an invalid candidate; the independent checker rejects it and a
fresh proper author recovers.

Four native fault methods exercise seven actual publication interruptions per
host. Directory membership changes before launch, before mutation and after
installation fail. Same-size payload, release and opposite-cohort edits with
restored timestamps also fail after installation. Existing output retains exact
bytes and timestamp, while an absent output returns to absence. Transaction
names are removed and every case subsequently publishes successfully.

The test release explicitly declares authority over real role/profile-valid
images and a replacement author. It is a synthetic operation fixture, not a
qualified new seed cohort. Neither successful publication nor these fixture
claims qualify the complete current source plan for promotion.

The first one-method probe inverted the host runner's zero-success convention.
The operation corrects that comparison. The first full suite also assumed an
empty manifest was accepted and indexed a nonexistent request-path slot. The
existing producer requires a nonempty manifest; a declared empty directory is
the valid empty-content image. The next suite treated a valid file-to-directory
change before capture as invalid. The corrected case gives a required parent
the wrong kind. Failed command logs remain retained.

The first checked Windows full-size call exceeded the test's 600-second outer
deadline. The surrounding retained validation now has 1,200 seconds; CupidObj
still has its unchanged 60-second production deadline. Native Linux also found
that `--root .` left a terminal dot component after absolute-root conversion.
The command now removes dot components and repeated separators from POSIX root
spellings without resolving links or parent components. The latter still reach
the retained observer's rejection rules.

The same spelling tests found Windows absolute-root conversion preserving a
trailing separator for `./` and `.//`. The command trims that separator while
keeping a drive root intact. The earlier failed native and checked command
receipts remain separate from the corrected command qualification.

The current in-OS manual is 162,971 bytes, SHA-256
`1089d4ef4557cbdeb5be27c3cf0c986bf33f67745b0838d2add459fef1700cb2`.
Its installed-seed build and independent check pass with all 1,589 captured
inputs, 429 objects and sixteen artifact checks. The private four-CPU boot
completes `ls` and the existing SMP checks. The 200 MiB image has SHA-256
`f30afc3b7e591146e1457d5aa65e7447454df5f1a5eeee98915080b258d150e5`.
Across the API and command manual checks, measured sizes become 9,597,100 bytes
for the raw kernel, 9,695,676 for the pass-one ELF and 9,826,748 for the final ELF.
Only those exact policy rows change. Evidence is
`cupid-native-iso-publication-cli-manual-20261006/manual-independent-windows.json`.

Independent evidence is `iso-publication-paired-independent.json` under
`cupid-native-iso-proof-20261005`. Its composed API, current command, paired
profile and supporting matrix has 343 executions and three platform skips.
Four current eight-method command runs pass, including `.`, `./` and `.//` root
spellings. The earlier Linux batch failures remain failures; their unchanged
passing API/profile rows are combined with the corrected command receipts.

All four retained full fixtures produce exactly 66,342,912 bytes with SHA-256
`44f47e3463e4a7f046b7d52034b7fcf411b799b9d1ecbf71a316bf7f8378ebe1`.
Their complete requests are 67,176,834 bytes. The checked Windows and Linux calls
take 679.246 and 523.121 seconds. Seven checked object pairs are identical; the
publisher is 22,200 bytes, SHA-256
`94695810e4e385bfb78724a34307455cc215cbd93e202595560846d61e44761c`.

Both complete 39-method native preprocessing suites pass with 419 tracked and
four generated roots, fifty hosted Linux cases and 61 conditional expressions.
Both seven-tool source link and runtime parity checks pass. Their initial failure
came from the test helper's sixteen-object bound, which rejected CupidBuild's
twenty-object link after five valid tools and their expected missing-runtime
diagnostics. The helper now reserves 32 objects and derives its guard from that
array. Repeat links, runtime omission rejection and recovery remain checked.

The 147-method bootstrap/direct-tool selection and 83-method contract selection
pass after their minimal and historical fixtures retain the appropriate source
counts and plan digests. Complete staged bootstrap and long-alias source-head
qualification remain separate. Eleven focused graph checks pass, including
source closures, measured conditional inventory, unknown-token rejection and
preprocessing manifest drift. Canonical generation and stale-output checks pass
with 780 active inputs, 255 feature kinds and 51 unreachable sources. The measured
`sizeof` inventory is 7,062 occurrences in 189 files. The four added preprocessing
roots and updated fixed-point source/link declarations remain explicit; older
supported plans are retained.

## Consequences

The complete operation is callable from native and Cupid-built API callers and
the current source command. Both supported producer plans and shared reader
profiles carry it. Installed CupidBuild and the normal ISO recipe retain their
accepted cohorts. Complete committed-source bootstrap, seed installation, real
regeneration and feature 17 still precede normal recipe handoff. Normal ownership
remains 447 CupidBuild and five Python actions across 452 transforms. `TempleOS/`
remains read-only and excluded from progress counts.
