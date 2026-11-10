# Retained disk publication caller

The private caller connects retained source capture, disk composition, Unicode
name projection, FAT16 staging, candidate finish and independent validation to
the existing guarded publisher. It exercises those boundaries in one process
without changing the normal disk recipe or installed seeds.

Capture every required payload and UTF-8 destination before opening the
candidate store. Copy each returned frozen pathname before the host's input
table can grow. Project every destination under an explicit Unicode 15 or 16
profile. An invalid later destination or missing payload therefore fails before
the candidate is opened.

The retained disk bridge captures the boot prefix, kernel, checked template and
previous public image. Compose the complete private image, then stage each file
in request order. Close the FAT writer before finishing the candidate store.
Finish flushes file data and captures the complete candidate digest. The
independent disk validator then reads that finished store and compares the boot
prefix, rendered MBR, kernel, zero gap, filesystem geometry and final expected
file contents. Its writer callback is never called.

Projected name collisions retain sequential replacement. Every input is staged,
but the expected final file table includes only the last payload for each
projected destination. Earlier payload and destination captures remain subject
to the publisher's source rechecks. A source change after successful validation,
including a change to an overwritten payload, preserves the previous public
image.

## Evidence

All 24 methods pass through each of four actual callers, without skips:

| Caller | Closed selection | Seconds |
| --- | --- | ---: |
| Native Windows | `disk-publish-native-windows3` | 64.098 |
| Native Linux | `disk-publish-native-linux2` | 24.020 |
| Cupid-built Windows | `disk-publish-checked-windows1` | 212.448 |
| Cupid-built Linux | `disk-publish-checked-linux1` | 249.346 |

Both matched builds retain the exact checked compiler, complete linked objects
and source bindings. Compilation, assembly and linking retain 360-, 120- and
180-second process limits with two workers; each runtime invocation retains
180 seconds. Windows build takes 53.857 seconds and Linux 59.065.

Independent rereading checks all 96 method executions, complete positive images,
native and matched artifacts, source bindings and original command limits. Every
corresponding image agrees across all four callers, including preserved failure
sentinels. Evidence is `disk-publish-four-producer-independent-products.json`;
the closed checker `disk-publish-four-producer-independent1` takes 6.070 seconds.

The complete 8 MiB image cases compare every byte with the separate Python
oracle. They cover fresh and preserved FAT images, force formatting, empty
files and kernels, seventy directory components, UTF-8 projection, table
growth, equal-image identity preservation, repeated grow/shrink replacement
and short-name collisions. Rejections cover invalid later destinations,
invalid UTF-8 and profiles, missing later sources, corrupt templates,
directory replacement and a later invalid parent after an earlier stage.
Five finished-store validation controls reject boot, kernel, gap and payload
mismatches or a read failure, clear the verified count and preserve the public
sentinel. The two source-mutation cases check the final guarded boundary.

The first Windows run reports 23 passes and one fixture setup error: the empty
kernel case tries to reuse the template helper's exclusive output name.
The corrected case gives its new template a distinct pathname. The next native
controls on both hosts stop before tests because the evidence wrapper has
already created the products directory. The harness accepts that directory
while continuing to reject an existing retained executable or case. All failed
logs remain. Neither correction changes C behavior or a rejection predicate.

## Full 200 MiB timing control

The separate real-OS control borrows the accepted bootloader, raw kernel,
three user executables and complete previous image. It stages those executables
and an 8 MiB synthetic WAD-shaped payload in a private copy. The native Windows
caller completes in 145.470 seconds within the recorded 600-second limit. Its
complete 209,715,200-byte output matches the Python oracle, SHA-256
`e86ee379ae6e5d177f1a569082f92d13f1653c4c0e3299918ef4e08f1f2c565c`.
The Cupid-built Windows caller exceeds that same limit. Independent rereading
finds the correct installed private image and the complete old-image backup,
but transaction cleanup has not finished. All borrowed OS inputs remain
unchanged. A killed process does not establish ordinary rollback or successful
publication. Evidence is `disk-publish-real-os-windows1` and
`disk-publish-real-os-windows1-timeout-independent.json`.

A separate diagnostic caller adds ten tagged phase lines while retaining every
other original object byte. Linux completes in 542.611 seconds under the same
600-second limit and a 32 MiB address-space limit. Windows again times out.
Windows source/store opening takes 147.206 seconds, composition 117.271,
and final checks/publication 252.273. Publication finishes at 578.764 seconds;
cleanup remains incomplete at the limit. Linux composition takes 46.885 seconds,
publication 226.090 and cleanup 116.929. Both private output images match the
oracle. These are diagnostic measurements, not acceptance of the original
uninstrumented full-image callers. Evidence is `disk-publish-phase-windows1`
and `disk-publish-phase-linux1`. The original limits and failed attempts remain.

## Remaining ownership

This is a private required-file caller. Paired seed and checked-template
producer authority, optional-file discovery and absence observations, the
normal CLI and Make handoff remain separate work. The caller retains the
existing FAT writer's directory capacity. It does not audit unreferenced
filesystem allocations or promise crash-durable directory publication.
The subsequent [bounded range path](BOUNDED-DISK-RANGES.md) closes the complete
200 MiB caller control on all four producers without changing its 600-second
limit or Linux memory bound. Its source/archive and qualification scope remain
separate from this earlier sector-only cohort.
The full-width adapter and candidate-store source cohort still needs source
integration and complete producer qualification. See
[the retained bridge](RETAINED-DISK-IO.md) and
[independent disk verification](DISK-VERIFY.md).

The subsequent [checked template capture](CHECKED-DISK-TEMPLATES.md) supplies
paired producer capture and registered template/kernel inputs. Its separate
four-caller evidence remains separate from this earlier caller. The
[combined checked publisher](CHECKED-DISK-PUBLISH.md) now connects both paths
and passes all 132 executions with independently reconstructed images. The
original 24-method caller also passes a fresh replay through all four current
adapters. Full normal-image acceptance, optional discovery and CLI ownership
remain open.
