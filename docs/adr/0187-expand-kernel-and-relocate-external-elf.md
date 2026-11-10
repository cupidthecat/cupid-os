# ADR 0187: Expand the kernel map and relocate external ELF programs

## Status

Accepted on 2026-07-30.

## Context

The complete CupidC Doom cohort emits larger, unoptimized objects than the
host compiler. Before this change, a diagnostic link placed `_loaded_end` at
`0x00916B24`, `_bss_start` at `0x00917000`, and `_kernel_end` at
`0x00D3BD5C`. The file-backed image exceeded the old boot reservation by
95,524 bytes, and BSS extended 245,084 bytes into the old kernel stack.

The host-built comparison reached `_loaded_end` at `0x008888C4`,
`_bss_start` at `0x00889000`, and `_kernel_end` at `0x00CADA30`. CupidC's
Doom objects added 470,524 file-backed bytes and 842 BSS bytes. That growth is
a toolchain property, not a reason to remove Doom code or distort its source.

The checked seed can now compile the kernel-entry statement with a nonzero,
page-aligned stack top. The memory map, boot read boundary, linker assertions,
external-program link address, and disk partition therefore had to move as
one change.

## Decision

Reserve the following identity-mapped ranges:

| Owner | Range |
| --- | --- |
| Kernel image and BSS | `[0x00100000, 0x00F00000)` |
| Kernel stack | `[0x00F00000, 0x01100000)` |
| CupidC JIT and AOT | `[0x01100000, 0x01A00000)` |
| CupidASM JIT and AOT | `[0x01A00000, 0x01C00000)` |
| Ordinary external ELF | `[0x01C00000, 0x01E00000)` |

The stack remains two MiB and grows down from `0x01100000`. CupidC and
CupidASM keep their existing ranges. Ordinary external executables retain a
two-MiB exclusive lease, but their fixed link and load base moves to
`0x01C00000`.

Move the FAT16 partition from LBA 16384 to LBA 20480. The BIOS loader now
reads the kernel from LBA 5 through LBA 20479, a 20,475-sector reservation.
The linker limits file-backed kernel bytes to physical address `0x00AFF600`
and all allocated kernel bytes to `0x00F00000`.

These constants are checked together in the linker script, boot source,
memory header, image builder, user linker wrapper, Makefiles, and tests.

## Evidence

The updated normal image links with `_loaded_end` at `0x008A2844`,
`_bss_start` at `0x008A3000`, and `_kernel_end` at `0x00CC7A30`. Its
8,005,700-byte raw kernel ends at LBA 15642, leaving 4,838 sectors before the
FAT partition. The kernel bytes in the image match `kernel/kernel.bin` at
LBA 5. A clean 209,715,200-byte image has SHA-256
`026cf8fca46c04202cd78939efed68d26f2c1077a767cfc316a0d57ce4592df5`.

The 2,560-byte CupidASM boot image has SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The MBR declares a bootable FAT16 partition at LBA 20480.

The memory-layout, production wrapper, external loader, user runtime, and
boot-source contracts pass 95 focused tests. Checked-seed CupidC emits the
31,174-byte active `kernel/core/kernel.cc` twice as the same 25,920-byte
object with SHA-256
`ed42676ad0d7f16b1fb83442ead1b0082781324dca719104922099cee34b5ab0`.
The normal `make -j8 all` build also passes.

The broader memory, production, runtime, process, CupidASM-source, baseline,
host-build, active-audit, and CupidLD selection passes all 199 tests in
555.897 seconds. It includes the negative link that crosses the new stack
boundary and the updated active-source inventory.

The strict frontier compiles all 155 checked-in kernel roots twice. Both
object sets total 3,708,988 bytes and are byte-identical. The 444-file input
snapshot has SHA-256
`4e153fdf4446128916bb10c0e51b3d1f815ed16bd57d6b1b85527355a0db190d`.
The relocated constants change the deterministic hashes of `process.cc`,
the in-kernel `cupidc.cc`, and `memory.cc` without changing their object
sizes.

Three private-image boots load `hello`, `ls`, and `cat` at `0x01C00000`.
Each program produces its PID-bound syscall evidence, exits as PID 4, and is
reaped. The cat case also completes its CupidC JIT setup and rejects a
marker-shaped line as serial evidence.

The four-CPU frontier runtime passes with e1000 and RTL8139. Both runs cover
RDRAND, TLS, network traffic, the fixed port-I/O cohort, USB removal and
reattachment, post-reattachment survival, framebuffer changes, AC97 output,
and PC-speaker output. After the embedded manuals were rebuilt, the final
e1000 image repeated that contract in 224.5 seconds.

## Rejected alternatives

Removing Doom behavior, pruning vendored source, or rewriting the program to
reduce object size was rejected because the active source defines the
toolchain requirement.

Depending on optimization was rejected because the normal build needs a safe
address-space contract before CupidC optimization becomes production-ready.

Shrinking the stack or either Toolchain arena was rejected because each range
has an active runtime contract. Moving the external arena after CupidASM
preserves those contracts and keeps the external lease contiguous.

Moving the in-memory boundary without moving FAT16 was rejected because the
BIOS loader also needs enough disk sectors for the larger file-backed kernel.

## Consequences

External ELF programs linked for the old base must be rebuilt. The supported
user build does this through CupidLD, and its guest gate checks the new load
address.

Tools that stage or mount the FAT partition must use LBA 20480, an offset of
10,485,760 bytes. Existing files remain available when the image helper
updates an image with the current partition layout; images created for the
old layout should be rebuilt.

The new limits leave 1,852,068 bytes of address-space headroom, about
1.77 MiB, and 2,001,628 bytes of file-backed boot headroom, about 1.91 MiB,
over the measured CupidC Doom image. This decision does not claim Doom
gameplay or complete production ownership; those require the separate Doom
handoff and runtime gates.
