# OS manual acceptance for Windows full-path output storage

The manual records the Windows adapter's full-path output allocation. The API
fills the same bounded buffer before conversion. Allocation failures, API
errors, malformed UTF-16, small outputs, file-part offsets and cleanup keep
their contracts. The [source acceptance](NATIVE-FULLPATH-STORAGE.md) contains
the dirty-storage tests and the private before/after measurements.

Both complete host builds and normal-root acceptance pass. Independent review
compares all 429 whole object pairs, four linked products, six user products
and all 209,715,200 image bytes. All six strict four-CPU boots, sixteen exact
artifact gates per build and seventeen root artifact-policy methods pass.
Only the embedded manual object changes; the other 428 objects retain their preceding bytes.

The [evidence index](evidence/fullpath-storage-manual-20261011.json) binds
the host pair and normal-root adoption. The
[paired archive](evidence/fullpath-storage-manual-originals-20261011.json.gz)
contains the candidate, controlled-file and complete product maps, all twelve
measurement commands, build and serial logs, and review recipes. The
[root archive](evidence/fullpath-storage-manual-root-20261011.json.gz)
preserves the two-file application and complete root verification.

## Measured kernel sizes

The preceding manual has 83,721 bytes. The
candidate has 84,889 bytes, growing by 1,168 bytes.
Each host runs six canonical CupidBuild, CupidObj and CupidLD measurement
commands against accepted baseline objects before its full build. The
preceding exact policy rejects the changed kernel products as expected.

| Artifact | Previous bytes | Accepted bytes | Growth |
| --- | ---: | ---: | ---: |
| kernel/kernel.bin | 9,312,968 | 9,314,136 | 1,168 |
| kernel/kernel.elf | 9,540,028 | 9,540,028 | 0 |
| kernel/kernel.elf.pass1 | 9,408,956 | 9,413,052 | 4,096 |

Raw-kernel and manual growth agree exactly. Both hosts independently measure all three products.
The raw-kernel and pass-one ELF exact policy rows change. Every other exact row retains its previous value. Both
complete normal builds reproduce the measured kernel bytes.

## Complete acceptance

Both unchanged Make graphs use four workers and block all nine conventional
producer variables. Each host rebuilds all 429 production objects with Cupid
tools, repeats the user build and checks 103 ABI fields with 101 providers.
Each checks sixteen exact artifacts and runs strict `ls` and
`/bin/feature17_iso.cc` boots. Every boot uses four CPUs, `max`, `e1000`, a
private image, command completion, full SMP validation and a 150-second bound.

| Host | Measurement and complete acceptance, seconds |
| --- | ---: |
| Windows | 2433.896 |
| Linux | 1950.366 |

Independent paired review takes 97.834 seconds.
It rereads all 2,202 controlled files per consumer,
complete products, every command result, strict serial logs and all 99 producer
inputs at the allocation commit. Both complete images agree byte for byte.
Their 2,560-byte boot region and all 199,229,440 bytes from the FAT start at
LBA 20,480 preserve the accepted baseline. The raw kernel matches its image
range starting at LBA 5. The manual occurs once in each raw and linked kernel.

Normal-root acceptance takes 1740.600 seconds;
independent review takes 56.039 seconds.
All root products equal the accepted pair, including the complete image.
Both strict root boots and all seventeen artifact-policy methods pass.

The manual SHA-256 is `421419136c32492dd2278ca409d84a7c4f0aac0219e44329b0bd3959620aeb88`.
The raw-kernel SHA-256 is `b03e824fcc060e8dc15c3d05130af4b1ea90fe9c52ad8585a453b228e6f01a5f`.
The image SHA-256 is `9a5a8d1729b509d177e1c634bbe7d238fe638e292b9e0dc631bfac037115978d`.

The first root collector cannot package its completed checks because the
preparation index is missing. The corrected index reconstructs the recorded
recipe identities, and the repeated independent review passes. Its archive
preserves the failure and original collector. Sources, acceptance guards,
resource bounds and completed build and boot results remain unchanged.

## Baseline and ownership

Current root products are reread against the completed allocation acceptance
and the preceding paired manual products. Consumers copy those products
before controlled source files, restore Linux seed executable modes and
rebuild all production objects. This reread claims no additional baseline
build or boot.

Root adoption changes the manual and measured exact artifact policy. The 99
producer inputs and fifteen installed seed files retain their allocation
acceptance bytes. Normal ownership remains 449 CupidBuild actions and three
Python coordinators across 452 transforms. TempleOS remains read-only reference
material and is excluded from builds and progress counts.

The private complete input owner retains its earlier UNC timeouts and variable
waiting. Four before/after checked calls pass the original capacity contract,
but those measurements do not qualify complete native input ownership or
publication. This manual step records the accepted allocation; it adds no
owner qualification or cached source authority.
