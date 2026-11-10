# Read-only FAT16 content verification

The separate `fat16_verify` foundation verifies expected files in a stable
private candidate using only the store's sector reader. The caller supplies
already projected names and retained expected-payload readers. The verifier
allocates no memory and retains fixed sector buffers, rather than a complete
image, FAT, directory, file or cluster bitmap. A null store writer is valid;
the verifier never calls a writer.

Geometry admission follows the existing FAT16 stage contract: 512-byte sectors,
power-of-two clusters through 128 sectors, bounded FAT/root/data regions,
FAT16 cluster counts and enough entries for the admitted data clusters.
Relevant FAT sectors must agree across every copy. Reserved header entries
must match the media byte and FAT16 convention.

Every expected argument and projected-name check precedes sector reads.
Directory traversal validates the complete chain before trusting a matching
entry, using Floyd cycle detection without a cluster bitmap. Directories may
span multiple clusters. Root traversal honors its declared entry count and
skips deleted, volume and long-name entries. File checks require the exact
declared length, complete payload bytes, exact chain termination and zero
padding through the end of the final allocated cluster. Empty files retain
the stage writer's zero-cluster convention. Any failure clears the verified
count; success reports the complete expected-file count, including zero.

## Evidence, 2026-10-08

All 31 methods pass through native Windows and Linux callers in 8.364 and
3.455 seconds. The same 31 methods pass through matched Cupid-built callers
in 7.283 and 2.620 seconds. Both matched builds use the explicitly selected
qualified wide-pointer compiler and record its bytes, actual compiled inputs
and complete artifacts. Compile, assembly and link bounds remain
360/120/180 seconds with two workers; each runtime invocation retains its
180-second bound. The Standards and Spec reviews close without findings.

Receipts under `C:/Users/admin/cp7/` are `fat16-verify-native-windows1`,
`fat16-verify-native-linux1`, `fat16-verify-checked-build-windows1`,
`fat16-verify-checked-build-linux1`, `fat16-verify-checked-runtime-windows1`
and `fat16-verify-checked-runtime-linux1`.

Positive controls cover root and nested paths, empty files, fragmented file
chains, multicluster directories and a sparse FAT16 partition above 4 GiB.
The high-partition case reads its complete expected payload and records sector
addresses above the 32-bit byte boundary. Small images and source payloads
retain complete before/after digests. The sparse high image retains its size,
identity and modification time, with zero writer calls; that case does not
claim a complete digest of the intervening sparse image.

Useful rejections cover invalid arguments and projected names before I/O,
missing paths, malformed geometry, inconsistent FAT copies, free/reserved/bad
or out-of-range links, short and extra file chains, file and directory cycles,
wrong file sizes, directory/volume replacements, a corrupt final payload byte,
nonzero padding, invalid empty-file clusters and physical truncation. Store
and expected-payload failures propagate. A second expected-file mismatch
clears the count after the first file passed. Partial root-sector slots cannot
supply an entry beyond the declared root count.

## Ownership remaining

The separate [final disk validator](DISK-VERIFY.md) now supplies boot-prefix,
kernel-gap, MBR and BPB checks. Both foundations still need connection to the
retained disk bridge and staging orchestration. The publisher must retain stable inputs,
validate the paired seeds and template producer, finish and hash the owned
candidate, and guard final publication. This foundation does not audit every
unreferenced filesystem allocation or establish template trust.

The normal image recipe remains Python-owned. These four prototype paths add
no producer input to the installed 99-input cohort and change no installed
seed. Source integration and complete producer qualification remain open.
