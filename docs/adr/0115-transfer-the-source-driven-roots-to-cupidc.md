# Transfer the source-driven roots to CupidC

- Status: Accepted
- Date: 2026-07-25

## Context

ADR 0113 proved that compiler head could emit 20 strict production roots
without changing their source. ADR 0114 moved those capabilities into the
checked CupidC seed. The normal Make graph still compiled the same roots with
Clang or GCC, so the proof had not yet changed production ownership.

The repository uses `.cc` for source owned by CupidC. Keeping these files as
`.c` after the production handoff would hide their compiler boundary. The
rename must happen with the Make transfer, exact dependency closures, and
runtime proof.

The generated `kernel/cpu/ksyms_data.c` is a separate root. It still needs the
unsupported `used` attribute and remains host-built.

## Decision

Rename these 20 files from `.c` to `.cc` and compile their normal object
targets through `tools/cupidc_kernel_compile.py`:

- `drivers/serial.cc` and `drivers/timer.cc`
- `kernel/core/app_launch.cc`
- `kernel/cpu/irq.cc` and `kernel/cpu/ksyms.cc`
- `kernel/fs/fat16.cc`, `kernel/fs/iso9660.cc`, and
  `kernel/fs/loopdev.cc`
- `kernel/gfx/deflate.cc`, `kernel/gfx/gfx2d.cc`, and `kernel/gfx/png.cc`
- `kernel/gui/ed.cc`
- `kernel/lang/cupidc_parse.cc`, `kernel/lang/cupidc_string.cc`, and
  `kernel/lang/ssh_io.cc`
- `kernel/mm/memory.cc`
- `kernel/network/sshd.cc` and `kernel/network/udp.cc`
- `kernel/smp/bkl.cc`
- `kernel/tls/tls_ca_bundle.cc`

Each Make rule declares the source's recursive header closure and the common
checked-seed controls. The wrapper accepts the new paths only through an
explicit allowlist. Object-name derivation uses `Path.with_suffix`, so the
tests and audit handle C and Cupid C source extensions the same way.

A dry-run regression poisons the host compiler, preprocessor, assembler,
linker, archiver, symbol reader, and object copier. The exact-index runtime
proof forces all 20 targets and sees one checked CupidC invocation per source
with no poisoned command.

## Evidence

The strict frontier now contains 136 sources and no compiler boundary. It
compiles the cohort twice to 3,020,108 byte-identical i386 ELF32 object bytes.
The 424-input snapshot has SHA-256
`24fcfba4f006dad77a742e02b31edd889d3a62010adb352d6f57965377557cd1`.
The 79,499-byte manifest has SHA-256
`6f3a32807e2c8756b8a38041ed3b9c95a9b8778ba6f1fb490f461431f3d0b40c`.
Tests pin the size and hash of every transferred object.

The complete image builds through both CupidLD passes and CupidObj. The final
7,870,352-byte `kernel.elf` has SHA-256
`e077b5d9ef0af3de57e9f114fbd217547efc598add3afdf3a753141fb9e99845`.
The 7,676,026-byte `kernel.bin` has SHA-256
`a45689fa7303ce950f5436f15a5126e906fc97a6c6c303286d70371cddb18275`.
The 209,715,200-byte `cupidos.img` has SHA-256
`a8b60721aee8092fcd17d3ab9431ef336952e2d3a9aa9b13a7356799ae70909d`.

The 53.658-second four-vCPU QEMU gate uses the `max` CPU model and e1000. It
starts all four CPUs, passes the 62 crypto, ASN.1, and X.509 checks, reaches
the desktop and terminal, and completes CupidC execution. Its 53,819-byte
serial log has
SHA-256
`fec0d4636944ab68f2e74d4bb32b43a4d89344d5fb348892314350b05d66148c`.

The user frontier still reproduces all three objects and executables. Its
16-input snapshot has SHA-256
`eb801b5466c3a6b96b31c61561e1e14db0fa7fc6ede3a159b3d6fa50e0b2eaef`.
The generated-install frontier regenerates and reproduces all three source
and object records from the final CupidOS text. Its 194-input snapshot has
SHA-256
`5986165c00cdc96dde11cd2271a56495e088401ffd8d0dd8c7e0d979390f1cc7`.
The current 9,794-byte documentation table source has SHA-256
`cff3fc8943d4b1999869653b14a882d21a463471452e429b2d742d47107b13fc`.
Its 11,032-byte object has SHA-256
`20530c6683aeae586c7ce060c22e795efe2a6c2362a1a0c7fdc58c61d74a6073`.

The regenerated audit keeps 698 sources, 253 feature IDs, and 500 transforms.
The language split is now 248 C files, 270 C headers, 153 Cupid C files, and
27 assembly files. CupidC owns 142 transforms, the host C compiler owns 155,
and host Python owns 154. The host compiler produces 103 root objects. The
active-source digest is
`58abaec6b74a7f548c8013199c7228f2a5bd5aa7cd78dc31d9e8d248e5d4d117`.

## Consequences

The normal production cohort grows from 116 to 136 CupidC-owned objects.
Twenty source files now state that ownership through `.cc`, and their Make
rules can no longer fall back to the host toolchain.

Eighteen strict checked-in roots remain on the host compiler. Broader GNU
assembly, `used`, floating code, the remaining private compiler files, Doom,
native hosted commands, Python orchestration, and WSL execution on Windows
remain bootstrap work. This decision does not claim that the normal OS build
is host-independent.
