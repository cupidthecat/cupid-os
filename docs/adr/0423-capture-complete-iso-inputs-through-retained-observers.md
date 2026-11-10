# Capture complete ISO inputs through retained observers

Date: 2026-10-05
Status: Source capability tested; guarded publication and seed carriage pending

The ISO manifest names entries without their kinds. Native publication needs to
preserve empty directories, freeze the complete input bytes and retain every
original binding through the final publication boundary. ADRs 0420–0422 provide
typed inventory validation, retained kind discovery and independent image checks.

Add an opaque `cupidbuild_iso_capture` that owns copied manifest, path and payload
bytes while borrowing the caller's observer. The observer must outlive the capture
and any transaction that uses it. Closing the capture frees its storage without
closing or resetting the observer. Failed construction clears the result and
frees all partial storage; a poisoned observer cannot authorize publication.

Parse portable manifest spellings before converting them to host C strings.
Discover the actual kind of every declared entry before validating the complete
typed graph. Observe file sizes before allocating payload copies, then validate
the populated inventory again. Capture exact direct membership for the fixture
root and every declared directory, including empty directories. Revalidate all
observations before returning immutable views. File source paths retain their
repository namespace; entry paths retain their separate ISO namespace.

The existing full 512-entry, 127-byte component and eight-directory-level limits
remain. Logical host paths are relative UTF-8; an empty fixture path selects the
observer root. Payloads retain the observer's 64 MiB per-file storage limit and
the complete request must fit the existing 32-bit image view. Oversized file
metadata fails before copying its payload. This limit covers the active fixture;
it must be extended if active input requirements grow. Capture creates no files,
locks or child processes and has no output-alias or publication authority.

Twenty methods pass with native and checked CupidC callers on each host: 80
selected, 76 executed and four expected platform skips. Checked callers execute
as native PE32 and ELF32. The new capture and caller objects form two identical
pairs. Reused core, image, inventory, host and runtime objects are rehashed against
their captured source and prior successful receipts before linking.

Tests cover the active fixture through checked CupidObj and the independent image
checker, all three full-capacity layouts, empty members, line endings, arbitrary
manifest order, maximum names/depth, UTF-8 host paths, hardlinked inputs, malformed
graphs, extra or missing members, wrong kinds, links/junctions/FIFOs, bounded
diagnostics, null arguments, repeated capture and concurrent jobs. Retained byte
digests remain unchanged after edits; same-size payload and manifest edits with
restored timestamps fail revalidation. File and directory replacement is either
denied by Windows sharing or detected on POSIX.

The first suite reused an existing fixture-directory name and incorrectly expected
an observer-root namespace change to preserve strict root metadata. Unique fixture
roots and an edit to an existing unobserved file correct those harness cases.
Both original failed suites remain recorded. An external verifier initially used
the wrong seed-record attribute; rereading the manifest at its explicit path
corrects that verifier. No capture implementation change was needed after its
first strict native compile.

Independent evidence is `iso-capture-paired-independent.json` under
`cupid-native-iso-proof-20261005`. It rereads 40 captured input files per host,
actual closed compile/link/suite commands, both image profiles, object pairs,
the original failed suites and unchanged installed seeds.

Guarded publication still needs to retain this observer at the final boundaries,
freeze the checked author and its file arguments, reject output/input aliases and
validate the authored candidate before replacement. Source capture alone does
not transfer the normal recipe, install seeds or change the current 447
CupidBuild/five Python ownership counts. The updated 157,552-byte manual passes
fresh installed-seed kernel/image and strict four-CPU runtime qualification.
Independent verification checks all sixteen artifacts and all 429 objects,
finding only the manual wrapper changed and preserving the existing FAT data.
Branch adoption still requires its qualified integration boundary.
