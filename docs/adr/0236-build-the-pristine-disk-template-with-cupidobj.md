# ADR 0236: Build the pristine disk template with CupidObj

## Status

Accepted on 2026-08-04.

## Context

Python currently creates the MBR, boot reserve, kernel lane, FAT16 boot
sector, two empty FATs, and root directory for a new `cupidos.img`. It also
updates existing filesystems and stages files. The first group is a
deterministic binary transform. The second group changes persistent state and
needs filesystem-aware orchestration.

The complete 200 MiB image is larger than CupidObj's 64 MiB command-output
limit. Its pristine prefix is much smaller. For the active geometry, that
prefix ends immediately before FAT cluster 2 and occupies 10,697,216 bytes.

## Decision

Add `cupidobj disk-template`. It takes a five-sector boot image, a separate
raw kernel, the image size in sectors, and the FAT16 partition LBA. It writes
the active type-0x06 MBR entry, stage two, the kernel at LBA 5, the zeroed
kernel reserve, the canonical CUPIDOS FAT16 boot sector, two pristine FATs,
and an empty 512-entry root directory. The result stops before cluster 2.

The shared operation uses checked 64-bit arithmetic before narrowing any file
offset. It validates the boot size, partition bounds, kernel reservation,
FAT16 cluster range, and caller output limit. Core failures preserve the
caller's buffer, clear the result, and rewind temporary arena storage. The
standalone command does not begin a host write after a parse, input, or
semantic rejection, so those failures leave an existing output untouched.
Like the other thin hosted tool adapters, it does not promise atomic recovery
from an operating-system short write or a full disk. The later production
publisher must provide that publication boundary.

CupidObj and the Python layout oracle now detect a repeated FAT-size state.
They abandon that sectors-per-cluster candidate and try the next legal FAT16
geometry. This closes an old host-side infinite loop for a partition of 8,288
sectors while retaining a valid two-sector cluster layout.

## Evidence

The freestanding contract checks the exact 38,400-byte compact layout and the
10,697,216-byte active layout. It also covers the FAT-size cycle, deterministic
repeat output, a kernel ending exactly at the FAT boundary, a short boot
image, invalid geometry, kernel overlap, an i386 size overflow, constrained
output, rollback, and same-job recovery. The overlap diagnostic is attributed
to the kernel input.

The hosted command tests compare both ordinary layouts with the existing
Python author, cover the cycle and exact-boundary policies, reject malformed
numeric arguments and missing inputs, and preserve an existing output after
every semantic rejection. The compact behavior fixture has SHA-256
`a1784fde1833c6cd24f49dff105ff8a70de5b9e619dd8883b4d92d597f241501`.
The next fixed-point transition requires both new CupidObj stages to produce
that exact template, reject an overlapping kernel, and preserve the two
sentinels. Its behavior matrix grows from five help, twelve success, and eight
failure cases to five, thirteen, and nine. The current checked seed keeps the
older counts until promotion.

The checked Toolchain cohort rebuilt in 3,247.6 seconds. All stage-two and
stage-three objects and executables match, the hosted runtime passes, and the
complete 20-artifact cohort verifies. Its 18,232-byte manifest has SHA-256
`d2de5422db6e22cd5bb5317980b1a4a7557b06803100ca3c0d71cf40d789c2d2`.
The Cupid-built i386 contract then passed the disk selector.

An isolated poisoned-host fixed point passed in 714.7 seconds with the new
5/13/9 behavior matrix. Its 15,057-byte report has SHA-256
`9b13bc6b98075ed872e48470334fea412914ed71be92fb2aa61070b73858413d`.
Only CupidObj differs from the current seed. The candidate is 295,712 bytes
with SHA-256
`be5385d8666a625844cb1be5611bd307fa865ca6cf1d50b4e836dfdb3ba45efc`.
The complete checked-seed module repeated that transition and passed all 42
tests in 799.104 seconds.

The final four-job OS and partitioned-USB build passed in 626.2 seconds. A
separately named, force-formatted 200 MiB image was then built from those boot
and kernel bytes. Its four-vCPU private QEMU smoke passed in 63.0 seconds:
RDRAND seeded the CSPRNG, the desktop and e1000 initialized, and `/bin/ls.cc`
completed through the in-OS CupidC JIT. The serial log contains no panic,
fatal, assertion, exception, or triple-fault marker.

## Rejected alternatives

Writing the complete disk in one CupidObj result was rejected because it
would raise a narrow transformation limit solely to carry a sparse 200 MiB
file. It would also mix deterministic template creation with mutable FAT
staging.

Moving FAT updates into CupidObj in this step was rejected. Reusing an
existing image, preserving user files, staging new files, checking live-input
drift, and replacing the final image safely need a separate ownership
transfer.

Cutting the normal recipe over before seed promotion was rejected because the
checked CupidObj image does not yet recognize `disk-template`.

## Consequences

Source-head CupidObj owns the deterministic disk-template semantics, while
Python remains the parity oracle and publication coordinator. Python still
creates the normal image until the five-tool seed is promoted and the
publisher learns to consume the template without losing an existing FAT
filesystem. Updating the embedded manuals refreshes normal kernel and image
bytes, but the image construction path, transform count, and source ownership
do not change in this step.
