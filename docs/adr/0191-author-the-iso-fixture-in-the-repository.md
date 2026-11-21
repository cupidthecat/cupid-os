# ADR 0191: Author the ISO fixture in the repository

## Status

Accepted on 2026-07-30.

## Context

The root build already ran CupidC, CupidASM, CupidObj, CupidLD, and CupidDis
from the checked seed. Forced regeneration of `test_iso/hello.iso` still
looked for `mkisofs`, `genisoimage`, or `xorrisofs` inside
`tools/hostbuild.py`. The generated graph described that recipe as Python
orchestration, so it did not expose the external author.

The old 389,120-byte fixture also carried checkout timestamps and 150 blocks
of author-specific tail padding. Its content worked in Cupid OS, but two hosts
could not recreate the same bytes without matching author versions, options,
environment, and source metadata.

Cupid OS reads a small data-image contract. It requires 2,048-byte blocks, a
primary volume descriptor at block 16, one embedded root record, contiguous
file extents, block-bounded directory records, and an inline SUSP `SP` marker.
Only Rock Ridge `NM` names affect guest behavior. The active feature test
enumerates all six root entries, reads a 4 KiB spanning fixture, decodes the
baseline JPEG, and exercises mount-pool lifetimes. It does not need El Torito,
Joliet, hybrid boot structures, symlinks, or relocated deep directories.

## Decision

`tools/hostbuild.py` owns a deterministic ECMA-119 author for the tracked
fixture. It writes the zeroed system area, primary descriptor, descriptor
terminator, little- and big-endian path tables, breadth-first directories,
and sorted contiguous file extents. All both-endian fields agree. Dates are
fixed at 2000-01-01 00:00:00 UTC, and the volume identifier is
`CUPID_OS_TEST`. The Volume Set Identifier is the d-character-only
`CUPID_OS_TEST_FIXTURE`.

The image uses the older `RRIP_1991A` profile already present in the accepted
fixture. Every directory-stream record carries deterministic read-only `PX`
and `TF` metadata. Ordinary records add a complete `NM` name. The root `.`
record keeps `SP` inline and points `CE` at one bounded continuation block
after the complete directory stream. The optional `ST` field is omitted,
matching the previous xorriso fixture and keeping sequential readers active
for later records. Cupid OS uses `SP`, `CE`, and `NM`; it ignores `PX` and
`TF`.

Source names use the portable letters, digits, dot, underscore, and dash
alphabet and are at most 127 bytes, which matches the guest VFS directory
buffer and cannot alter Make prerequisite grammar. Per-directory
case-insensitive collisions fail because the guest cannot distinguish them.
Stable uppercase 8.3 identifiers receive numeric suffixes when two long names
share the same fallback. Files without an extension retain ECMA-119's empty
extension separator, as in `README.;1`. Directory records and path-table
entries are sorted by the allocated identifier, not the source spelling. A
ninth directory level is rejected because this writer does not emit Rock
Ridge relocation records. Links, junctions and other Windows reparse points,
special files, overlong records, 32-bit size overflow, and output paths inside
the fixture tree are also rejected.

Authoring reads the complete tree into an immutable snapshot. Rendering uses
only that snapshot. Before publication, hostbuild rereads
`test_iso/fixtures.manifest`, scans the live tree again, and confirms that the
existing output has not changed. It writes a closed temporary file beside the
destination, flushes it, and publishes with `os.replace`. A failure leaves the
old image in place. Identical bytes keep the existing file and timestamp. An
output hard-linked to any fixture is rejected.

`gen-big` remains a separate transform and no longer runs inside
`build-iso`. The seven-line manifest lists every fixture directory and file.
Make carries the same seven portable paths as explicit prerequisites; a test
requires that list to equal the manifest before the audit may move. Raw
manifest text never enters Make grammar. Hostbuild still requires the live
tree to match the manifest exactly. The ISO rule also depends on
`tools/hostbuild.py`, its imported `bootstrap_toolchain.py` module, and the
Makefile. The audit classifies it as `package_iso9660_image`.

## Evidence

The tracked image is now 61,440 bytes, or 30 blocks, with SHA-256
`40359c1cec72219f21e87ce71b31e621209036042440e1b38c5e59de157e0fb6`.
The host `file` probe recognizes ISO9660 volume `CUPID_OS_TEST`. Windows
libarchive 3.5.2 lists all six Rock Ridge root names and
`sub/nested.txt`.

The hostbuild suite runs 42 tests on Windows: 41 pass, and the
filesystem-dependent case-collision test skips. Its independent parser checks
the descriptor and d-character fields, both path-table byte orders,
identifier ordering, extensionless file syntax, even and block-bounded
directory records, fixed dates, `SP`, `PX`, `TF`, `CE`, the canonical
extension identifier, forward continuation placement, the absence of optional
`ST` terminators, `NM` names, nested directories, empty files, an exact-block
file, and a two-block file. Equivalent trees created in opposite orders with
different timestamps produce identical bytes.

Feature 17 now iterates the mounted root and requires all six Rock Ridge
display names exactly once with the expected file or directory type. Its
existing lookup, content, spanning-read, JPEG, multi-mount, and pool checks
remain in the same guest command.

A settled-source Windows root build completed in 382.440 seconds with empty
standard error. A read-only FAT16 extraction matched the embedded `/hello.iso`
to the tracked image byte for byte. The private four-vCPU e1000 frontier then
passed in 232.2 seconds. It contains one directory-name marker, one final
Feature 17 pass, ten completed in-OS CupidC commands, six USB mass-storage
lifetimes, 71,963 changed framebuffer pixels, 8,270,885 AC97 frames, and
78,284 PC-speaker frames. Its 46,452-byte serial log has SHA-256
`cf0c5b521115087e718a8b0acfcf5c5c0fb4c106dec7b1ea85d7f979f9356596`.

Negative tests cover missing or non-directory roots, unsafe outputs,
non-ASCII, Make-metacharacter, and overlong names, case-only collisions, file
and directory symlinks, an NTFS junction while the Python 3.12 junction helper
is disabled, an output directory, an output symlink, a hard-linked output, an
undeclared or missing manifest member, a ninth directory level, manifest
drift, source-byte drift, membership drift, output drift, and a failed final
replacement. Each applicable case preserves the previous published image.

The first repository-authored candidate put the continuation block before the
directory stream. Cupid OS can seek directly, so the guest accepted it, but
the sequential libarchive reader rejected the backward `CE` request. Moving
the continuation after every directory fixed that error. The next probe
showed only the first `NM` name because an optional `ST` field stopped that
reader's global SUSP scan. The prior xorriso image did not emit those fields;
removing them restored every long name without changing the guest contract.

The generated 500-transform audit names the exact twelve ISO inputs and the
three `big.bin` inputs. All 67 build-graph tests pass in 555.892 seconds.
Canonical audit regeneration and drift checking also pass. The 2,564,353-byte
JSON has SHA-256
`7b07bb3f19f8d72f1968f31c176ab66d7ae94ffbc47d672adf10904d4dcdc15b`.
The active-source digest is
`acb45e969e42b40aca599fed4d8aa90075f4b88e2938993146ee409d0915f3b0`.

## Rejected alternatives

Keeping an external author was rejected because it left a hidden host
dependency and host-specific metadata in an otherwise checked root graph.

Reproducing xorriso's 150-block padding and live filesystem timestamps was
rejected because neither byte range is part of the guest contract.

Emitting only `SP` and `NM` was rejected because that would be a private SUSP
profile while the image claimed Rock Ridge. The chosen `RRIP_1991A` records
remain compact and compatible with the existing reader.

Regenerating `big.bin` inside ISO authoring was rejected because Make already
models that file as a separate transform. Packaging now treats every fixture
as immutable input.

## Consequences

`mkisofs`, `genisoimage`, and `xorrisofs` are no longer needed by `make all`,
`make sync-iso`, fixture regeneration, or tests. They may still serve as
manual interoperability oracles.

Python remains a host orchestration and image-packaging dependency. The
writer is intentionally a data-fixture author, not a general optical-disc
mastering library. Bootable ISO media, Joliet, symlinks, deep-tree
relocation, and multi-extent files remain outside its contract.

The broader Python-free and native Windows bootstrap work remains open.
