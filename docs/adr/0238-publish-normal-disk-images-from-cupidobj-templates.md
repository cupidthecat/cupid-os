# ADR 0238: Publish normal disk images from CupidObj templates

## Status

Accepted on 2026-08-05.

## Context

Checked-seed CupidObj can author the deterministic disk prefix from the MBR
through the empty FAT16 root directory. The normal build still asked Python to
construct those bytes, then changed the image in place. That left a production
owner behind the checked capability and exposed an existing image if template
generation, staging, or publication failed.

A fresh image and a persistent image need different composition. The complete
10,697,216-byte template is correct for a fresh disk. Applying that complete
template to a reused disk would replace the FAT boot sector, both FATs, the
root directory, and every existing file. A reused disk may accept only the
prefix before the FAT partition, currently 10,485,760 bytes.

## Decision

The normal `cupidos.img` recipe depends on `tools/hostbuild.py`, the checked
seed manifest, and all five seed images. It passes the manifest to
`hostbuild.py image`, which runs checked CupidObj as the first disk author:

```text
disk-template FROZEN_BOOT --kernel FROZEN_KERNEL
  --image-sectors IMAGE_SECTORS --fat-start-lba FAT_START_LBA
  -o PRIVATE_TEMPLATE
```

The publisher freezes the bootloader, kernel, and every present stage input.
It records missing optional inputs, snapshots the live output with streaming
SHA-256, and validates the complete five-tool seed trust unit. Symbolic links,
junctions, nonregular files, and output aliases to any input are rejected.
A cross-process lock rejects an overlapping hostbuild publisher for the same
resolved output.

Checked CupidObj must succeed and emit a regular file with the exact active
template length. The private Python layout author then builds an independent
oracle from the same frozen inputs. A byte difference stops the transaction.
Python does not become the fallback template author.

Composition follows the state of the frozen output:

| Output state | Private candidate | Template bytes applied |
| --- | --- | --- |
| Missing, invalid, or force-formatted | New image extended to the requested size | Complete template |
| Valid existing image | Streaming copy of the existing image | Bytes before `fat_start_lba * 512` only |

Python stages frozen files into the private candidate. It then flushes the
candidate, rechecks the seed, every present and missing input, and the live
output, and publishes with `os.replace`. A failed checked command, oracle
comparison, stage operation, drift check, or replacement leaves the previous
image untouched. A successful byte-identical rebuild still replaces the file
so Make receives a current timestamp.

Python remains responsible for preserving persistent FAT state, staging guest
files, extending the active prefix to the full disk size, parity checks, drift
detection, and final publication. The standalone `stage`, `stage-wads`, and
`sync-*` paths remain in-place mutations and are not part of this transaction.

## Evidence

The focused image class covers fresh construction, reuse, shorter-kernel
zeroing, force-format and invalid-image recovery, missing optional inputs,
timestamp refresh, checked-command failure, unsafe candidates, oracle drift,
stage and replacement failure, input and output drift, aliases, seed trust,
and CLI diagnostics. It also runs one compact image through the real promoted
seed. The complete host-build and hosted CupidObj modules pass with the shared
Python oracle extracted from the production publisher.

The active-source audit classifies the normal image as
`package_disk_image` with `cupid_object` and `host_python`. CupidObj
participation rises from 186 to 187 while the 719-input, 449-transform,
255-feature, and 25-unreachable totals remain unchanged.

All 86 host-build tests pass with one expected filesystem skip, and all 26
hosted CupidObj tests pass. A fresh normal build completed in 672.0 seconds
and produced a 209,715,200-byte image with SHA-256
`8ad90a91103bf48d1e8d1e20b1b3dee48122ed1e4059b3f94cce7d750c262f16`.
A private four-vCPU `/bin/ls.cc` JIT boot passed in 61.9 seconds. The audit
regeneration and reproducibility check pass with source digest
`cfb0e1dcd276154a4db5c2747ed092581874a54cd4c9fb379f204e3c10f8253e`.
The source-current rebuild then exercised the persistent path in 616.648
seconds and reported that it reused the image while preserving FAT data. Its
final 209,715,200-byte image has SHA-256
`d1bfab4aed1f2116768ceed3e301fb14ffe2a36418eb4d4ebdf1108097cb2b05`,
and a second private four-vCPU JIT boot passed in 66.8 seconds.

The source-current Toolchain retry passed in 3,363.6 seconds after the first
attempt reached a one-hour outer watchdog. Stage two and stage three match,
the hosted runtime passes, and all 20 artifacts verify. The 18,232-byte
manifest has SHA-256
`edca1f86f063c5b8b967508a06ddf19f97ea79da674e08d9c952eabe68485568`.
The published checked CupidObj then ran its `disk-template` selector directly
in 0.975 seconds.

## Rejected alternatives

Applying the complete template to every image was rejected because it erases
persistent FAT data on reuse. Running the Python oracle first was rejected
because the checked tool must be the production author, not a validator for
Python-owned bytes. Editing the live image before all checks complete was
rejected because a later failure could leave a mixed boot, kernel, or FAT
state.

The first reuse gate checked only a small BPB subset. It was rejected after a
malformed image passed admission, and the final gate proves the partition
size, active FAT16 geometry, and FAT capacity. Private candidates also use a
fixed internal basename because deriving it from the public output caused a
collision for one valid output name.

## Consequences

CupidObj owns the pristine bytes in the normal disk-image transform. Python
still owns the mutable filesystem and guarded publication work around those
bytes. The remaining Python-only root outputs are the ISO image and the Doom
input manifest. No ordinary C or assembly source changes ownership, so no
`.c` to `.cc` rename is due. `TempleOS/` remains untouched reference material.
