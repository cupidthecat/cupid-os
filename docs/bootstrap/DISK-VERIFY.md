# Independent final disk verification

The separate `disk_verify` foundation checks a stable finished private image
through retained source and sector readers. It independently reconstructs the
first five boot sectors and the exact single-partition MBR, compares every
kernel byte, and requires zero padding through the FAT partition start. It
then checks reusable FAT16 geometry and delegates expected path, chain,
complete payload and final-cluster padding checks to
[the read-only FAT16 verifier](FAT16-VERIFY.md).

The final MBR uses the composer's single type-6 partition, CHS fields, partition
extent and signature, with every unused partition byte cleared. The boot
source's original partition table and bytes after its first five sectors do
not participate. Kernel capacity arithmetic retains all 64 bits. A zero-size
kernel may omit its reader. Candidate/source contract and extent failures
precede callbacks; expected-file names are checked by the content verifier
after prefix validation and before its own filesystem reads.

Preserved volumes retain the composer's existing reuse rules: one or two FATs,
power-of-two clusters through 64 sectors, a matching complete partition size
and hidden-sector value, admitted FAT16 cluster counts and sufficient FAT
capacity. Existing OEM, label, serial and geometry metadata may differ from
the pristine template. The final verifier does not reconstruct that preserved
BPB from a freshly chosen layout.

## Evidence, 2026-10-08

All 25 methods pass through native Windows and Linux callers in 8.868 and
4.611 seconds. Matched Cupid-built callers pass the same methods in 7.886
and 3.407 seconds. Compile, assembly and link bounds remain 360/120/180 seconds
with two workers; every runtime call retains its 180-second bound. Actual
compiler, compiled inputs, headers and complete artifacts are retained in the
matched build receipts. Both Standards and Spec reviews close without findings.

Receipts under `C:/Users/admin/cp7/` are `disk-verify-native-windows2`,
`disk-verify-native-linux1`, `disk-verify-checked-build-windows1`,
`disk-verify-checked-build-linux1`, `disk-verify-checked-runtime-windows1`
and `disk-verify-checked-runtime-linux1`.

Positive controls cover complete boot/kernel/payload bytes, a null candidate
writer, zero expected files, an empty kernel without a reader, a kernel that
exactly fills the prefix, independently rendered source partition bytes,
nonpristine preserved BPB metadata and a complete one-FAT layout. A sparse
256 MiB geometry control accepts 64-sector clusters and rejects 128-sector
clusters before prefix-source reads. It requests no expected files and does
not claim a complete filesystem audit.

Rejections cover every rendered MBR region, the last boot and kernel bytes,
final kernel-sector padding and the last gap byte, malformed BPB fields,
callback failures, missing expected files, invalid projected names, final
payload corruption, cyclic file chains and physical truncation. The synthetic
high-kernel control proves full-width admission and one-byte-overlap rejection
above 4 GiB, then deliberately fails at its malformed BPB. It does not claim
complete validation of a large image. Small images and all sources retain
complete before/after digests; every case reports zero candidate writes.

The first Windows selection retains two fixture mistakes: the Python reader's
FAT-count attribute is `num_fats`, and invalid projected names return the
existing `CTOOL_ERR_PATH`. Correcting those expectations changes no verifier
behavior. The original failed receipt is `disk-verify-native-windows1`.

## Ownership remaining

The validator uses fixed sector buffers without an allocator or a complete
image buffer. Its result clears on failure and reports the complete expected
file count only after every check succeeds. It supplies a format/content veto,
while the publisher retains stable captures, paired seed and template-producer
trust, candidate finish and digest, staging orchestration and final guarded
publication. Unreferenced filesystem allocations remain outside its scope.

The [required-file publication caller](RETAINED-DISK-PUBLISH.md) connects this
validator to the retained disk bridge's finished store. All 24 methods pass
through four native and matched callers, including validation vetoes and final
source rechecks. The normal image recipe remains Python-owned. These four new
paths change none of the installed 99 producer inputs or seeds. Their headers
need an expanded qualification snapshot. Source
integration, complete producer qualification and normal image ownership remain
open.
