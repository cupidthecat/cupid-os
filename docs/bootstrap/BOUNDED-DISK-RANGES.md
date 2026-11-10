# Bounded disk composition transfers

The full retained Windows publisher exceeds its 600-second limit with
sector-by-sector composition. Tagged measurements separate source capture,
composition, publication and cleanup. Composition takes 117.271 seconds on
Windows and 46.885 on Linux. The Windows diagnostic installs the correct private
image at 578.764 seconds and then times out during cleanup. The original failure,
prior-image backup and borrowed-input checks remain recorded in
[the publisher evidence](RETAINED-DISK-PUBLISH.md).

Keep the existing sector entry and request layout. Its actual 512-byte transfer
buffer, callback sizes and diagnostic flags remain unchanged. A separate range
entry shares the same renderer, complete template checks and reuse predicate.
It transfers ascending contiguous ranges of 512 through 65,536 bytes through a
64-bit offset. Template reads stop at the template extent. Reused-image reads
start at the FAT boundary; no transfer mixes source views. A fixed 65,536-byte
buffer replaces the sector transfer buffer for this entry alone.

The retained bridge supplies the transaction's existing range writer behind
`cupidbuild_disk_io_compose`. Every source and candidate operation keeps its
retained identity, extent and metadata checks. Any composition failure latches
the adapter and forbids finish. Existing sector views still serve FAT16 staging
and read-only validation. Finish, complete digests, source rechecks, guarded
publication and cleanup retain their original protocols. No complete image is
allocated, and no normal CLI, recipe or installed seed changes.

## Focused evidence

All four native and Cupid-built callers pass eight new range methods,
25 bridge methods and the existing 24 publisher methods. Both original
sixteen-method composer selections also pass with native and checked programs
together. This covers 292 caller/method combinations, without skips.

Independent rereading accepts 132 range/bridge combinations in 11.399 seconds,
96 publisher combinations in 7.289 and every original native/checked complete
image scenario in 14.023. Evidence is
`disk-range-foundations-four-producer-independent-products.json`,
`disk-range-publish-four-producer-independent-products.json` and
`disk-range-legacy-four-producer-independent-products.json`.
The complete original 200 MiB composition scenario still matches on both hosts.

The new controls check complete fresh and retained images, force formatting,
invalid previous geometry, unaligned source boundaries, final short transfers,
every template region, failed and partial writes, source failures before and
after earlier writes, absent callbacks, wide invalid kernel sizes and result
clearing. Bridge controls add failure latching, poisoned sources, null results
and rejection after finish. All original 360/120/180-second build bounds and
180-second small runtime bounds stay intact.

The complete new matched publisher builds take 53.127 seconds on Windows and
50.231 on Linux, with two workers and the explicitly retained qualified
compiler. The three additional fixture images reuse byte-identical common
objects and retain their fresh body compilations and links.

## Complete retained 200 MiB images

| Caller | Runtime seconds |
| --- | ---: |
| Native Windows | 75.485 |
| Cupid-built Windows | 522.366 |
| Native Linux | 46.222 |
| Cupid-built Linux | 511.659 |

All four callers finish capture, composition, staging, independent validation,
publication and cleanup within the unchanged 600-second limit. Both Linux
children retain a 32 MiB address-space limit. They borrow the accepted bootloader,
raw kernel, three user executables and complete previous image, then add the
same 8 MiB synthetic WAD-shaped payload. Every 209,715,200-byte output equals
the separate Python oracle and the other three outputs, SHA-256
`e86ee379ae6e5d177f1a569082f92d13f1653c4c0e3299918ef4e08f1f2c565c`.
All source bytes and namespace cleanup checks pass. Independent evidence is
`disk-range-real-os-four-producer-independent-products.json`; its checker takes
20.417 seconds. This synthetic payload does not establish Doom runtime acceptance.

The first Linux full replay stops before native execution because its retained
native copy lacks an executable bit. Restoring permission leaves every program
byte unchanged; a fresh complete replay passes with the same bounds. The failed
launch and permission repair remain recorded. Required-file publication still
needs paired seed and template-producer authority, optional discovery, source
integration and complete producer qualification before normal recipe ownership.

Both review axes identify the same header wording error: an unrestricted
template-end promise would also cover transfers from a reused image. Correct
that one comment to describe each source's actual boundary. Preserve the exact
accepted source in `disk-range-accepted-source-windows1` and
`disk-range-accepted-source-linux1` before the correction. Fresh compilation of
the seven dependent objects on each host passes in 17.208 seconds on Windows
and 15.682 on Linux. Every complete object equals its accepted predecessor.
The changed prototype source has its own 102-input observation and
does not replace the installed 99-input qualified cohort.

## Runtime fixture placement

The first strict ls boot fails on both complete images before the desktop
appears. Both images already equal their Python oracles. The fixture places
the 8 MiB WAD-shaped payload in `/TEST/`, where HomeFS imports it and expands
`HOMEFS.SYS` from 61,493 to 8,492,218 bytes during first boot. Its flush remains
in progress at the original 150-second deadline. The subsequent feature 17
checks do not run after these failures.

The normal recipe places WAD files in `/wads/`; HomeFS deliberately leaves
that directory on FAT16 for direct access. A control changes only the fixture
destination to `/wads/wad0.wad`, preserving its complete 8 MiB payload, three
user executables, image geometry and runtime bounds. The independently rendered
Python image passes the original strict Linux ls/SMP boot in 54.526 seconds.
This supports the placement diagnosis. Fresh complete publication passes through
all four callers under the unchanged 600-second bounds:

| Caller | Corrected fixture seconds |
| --- | ---: |
| Native Windows | 59.366 |
| Cupid-built Windows | 528.049 |
| Native Linux | 45.945 |
| Cupid-built Linux | 537.251 |

Every complete image equals its Python oracle and the other three images,
SHA-256 `6a3d86a0f988a56248a11cdc14a3542431d3aac52944816fdc4622d90a24b5d7`.
The independent full-image reread passes in 10.343 seconds. Both original strict
profiles pass on the matched Windows and Linux images: ls/SMP takes 47.276 and
56.649 seconds; feature 17 takes 54.354 and 61.729. Their independent reread
passes in 17.606 seconds and confirms all runtime markers, four complete images
and unchanged borrowed OS inputs. Evidence is
`disk-range-wad-real-os-four-producer-independent-products.json` and
`disk-range-wad-strict-boots-independent-products.json`.
The OS source and its HomeFS import behavior remain unchanged. Preserve the
complete accepted 102-input producer observations under
`disk-range-wad-accepted-source-windows1` and
`disk-range-wad-accepted-source-linux1` before further prototype changes.
