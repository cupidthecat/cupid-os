# Private CupidC signatures for VFS and swap calls

Eight VFS bindings and all five swap bindings publish fixed native signatures
to private CupidC. Each call converts its arguments to the declared lanes
before building the four-byte native stack slots. A `double` descriptor,
flag, byte count, pool size or handle can no longer shift later arguments.
The change uses the existing typed native-call boundary from
[ADR 0332](../adr/0332-publish-fixed-signatures-for-private-kernel-bindings.md).

The VFS cohort is `vfs_open`, `vfs_close`, `vfs_read`, `vfs_write`, `vfs_seek`,
`vfs_mkdir`, `vfs_unlink` and `vfs_rename`. Read and write retain a signed
descriptor, object buffer and unsigned count. Open retains a character
pathname and unsigned flags. Seek retains three signed words. Close and
the pathname calls retain their native declarations.

`swap_init` retains a character pathname and unsigned pool size.
`swap_kmalloc` accepts an unsigned size and returns an unsigned handle.
`swap_pin` accepts that handle and returns an object pointer.
`swap_unpin` and `swap_free` accept the handle and return `void`.
Native implementations, parser rules, binding names and result channels
retain their existing contracts.

## Argument and failure tests

The JIT and AOT driver extracts the thirteen real declarations and
registration macros from `kernel/lang/cupidc.cc`. Independent fixed-address
i386 oracles inspect the native stack words and result lanes. Both storage
cohorts fail at their first call with the preceding result-only registrations.
The fixed registrations pass with defined unsigned conversions above
`INT_MAX`, negative seek offsets, object buffers, returned pointers and
repeated read/write calls.

Every binding rejects too few and too many arguments in JIT and AOT mode.
Floating pointer arguments and incompatible SIMD scalar arguments retain
the typed-call error. The same compiler state accepts and runs a valid call
after two failures. These recovery cases exercise JIT compilation; they do
not claim a separate AOT recovery loop.

The first Windows runtime attempt stops before executing its oracle because
the WSL login shell sources a missing Cargo environment file. The harness
now uses `wsl --exec sh -c`; the repeated red run reaches compiled code and
exposes the JIT and AOT argument defect. A proposed pointer-to-integer
negative conflicts with the established Cupid address convention in
ADR 0230. Its replacement uses an incompatible SIMD argument.

Both hosts pass 494 focused private compiler, binding census and GUI methods.
A later GUI run passes 142 methods, including a new check that rejects either
missing storage binding marker. The
[focused archive](evidence/private-storage-binding-focused-20261011.json.gz)
contains twelve original test attempts, including failed setup and assertion
attempts. It does not turn those attempts into successful regressions.

## Guest storage probes

Feature 17 creates a RamFS file, exercises all eight VFS calls with converted
scalar arguments and checks an exact eight-byte round trip. It reports
`PASS feature17_binding calls=8 bytes=8` before its existing ISO success.
Feature 18 converts a size and checks five handle calls across unpin and
repin, preserving eight bytes. It reports
`PASS feature18_binding sizes=1 handles=5 bytes=8` before its existing swap
success. Both programs retain their complete preceding tests.

The runtime frontier requires each new marker before overall program success
and JIT completion. Complete 28-command frontiers pass on Windows and Linux,
including graphics, audio, USB replug, networking and both storage programs.
They use four CPUs, `max`, `e1000`, private system and USB images and the
original 300-second phase bound. The
[frontier archive](evidence/private-storage-binding-frontier-20261011.json.gz)
preserves complete command and serial logs and reported framebuffer and
audio measurements. The framework removes its temporary framebuffer and
audio files; this evidence does not claim a later reread of those files.

## Paired build and root acceptance

Both unchanged Make graphs use four workers and block all nine conventional
producer commands. Each host rebuilds all 429 production objects with Cupid
tools, repeats the user build, checks 103 ABI fields and 101 providers, and
passes sixteen exact artifact checks. Each then passes strict `ls`, ISO and
swap boots with four CPUs, `max`, `e1000`, private images, command completion,
full SMP validation and the original 150-second bound.

All 429 whole object pairs, four linked products, six user products and every
byte of the 209,715,200-byte image agree. Exactly five objects change:
the private binding table, two embedded storage programs, manual and
generated kernel symbols. The other 424 objects, the 2,560-byte boot region,
all 199,229,440 bytes from FAT start at LBA 20,480, and all six user products
retain their preceding bytes.

| Acceptance | Seconds |
| --- | ---: |
| Windows measurement, full build and three focused boots | 2302.458 |
| Linux measurement, full build and three focused boots | 1825.900 |
| Windows full runtime frontier | 876.208 |
| Linux full runtime frontier | 821.108 |
| Normal root build and three focused boots | 1860.593 |

The nine canonical measurement commands establish the kernel products
before each full build. The preceding exact policy rejects them as expected.
Only the three kernel rows take the measured sizes; all other rows retain
their values. Both full builds reproduce the independently measured bytes.

| Artifact | Previous bytes | Accepted bytes | Growth |
| --- | ---: | ---: | ---: |
| kernel/kernel.bin | 9,314,136 | 9,322,944 | 8,808 |
| kernel/kernel.elf | 9,540,028 | 9,552,316 | 12,288 |
| kernel/kernel.elf.pass1 | 9,413,052 | 9,421,244 | 8,192 |

The manual grows by 1,586 bytes to 86,475 bytes. It occurs once in each raw
and linked kernel, and the raw kernel agrees with its image range at LBA 5.
Its SHA-256 is `be73714afa4ad70971aef31b226397f497446a35a54f0fa7dda7f82ad578a933`.
The raw-kernel SHA-256 is `b3c7c5a14b1801c201b4636821580d2793d5f4bc04e72e32015690ed916960dd`.
The image SHA-256 is `7bab2d2a8f5f647675bc4cb01a174c5a770ed6d1866cc822bc8b03c1f78fb45d`.

Normal root acceptance repeats the complete build, user build, sixteen
artifact checks, seventeen policy methods and three strict boots.
Independent review compares all root products and the complete image with
the accepted host pair. The
[paired archive](evidence/private-storage-binding-originals-20261011.json.gz)
and [root archive](evidence/private-storage-binding-root-20261011.json.gz)
contain controlled-file maps, source changes, command recipes and original
build and serial logs. The
[evidence index](evidence/private-storage-bindings-20261011.json)
binds all four archives. Copies also exist outside `build`.

## Ownership and remaining scope

This storage acceptance keeps all 99 hosted producer inputs and installed
seeds unchanged. Normal ownership remains 449 CupidBuild actions and three
Python coordinators across 452 transforms. TempleOS remains read-only
reference material and is excluded from builds and progress counts.
The separate verification repair changes one hosted producer source; its
evidence records that later source projection separately.

`vfs_stat` and `vfs_readdir` retain their legacy record-pointer contracts.
The thirteen scalar and object-pointer signatures do not introduce native
record identities. Other native bindings, complete Cupid language behavior,
and native image input and publication ownership remain open.
Issue 31 remains open.
