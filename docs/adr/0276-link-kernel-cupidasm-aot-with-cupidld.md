# ADR 0276: Link kernel CupidASM AOT with CupidLD

## Status

Accepted on 2026-08-13.

## Context

The in-kernel `as -o` path asked shared CupidASM for a fixed image, then wrapped
that already placed image in a small sectionless executable. This preserved
behavior while the assembler became shared, but it left final executable
placement in the adapter instead of the linker. It also made the AOT and JIT
paths look more alike than their ownership allows: JIT needs a fixed image,
while an executable file should be linked from a relocatable object.

CupidASM already emitted ELF32 relocatable objects, and the kernel already
carried the shared ELF support needed by CupidLD. What was missing was entry
metadata that survives relocatable assembly and a bounded in-kernel link
adapter.

## Decision

For ELF32 relocatable output, CupidASM now applies the caller's ordered entry
candidates and publishes the selected spelling in `entry_symbol`. It promotes
only that selected code label to a global ELF symbol. Fixed-image output keeps
its existing address result and also reports the selected spelling. Absolute
request bindings remain relocations in a relocatable object so the linker owns
their final values.

The kernel AOT command assembles one `ET_REL` object and gives it to shared
CupidLD for a fixed-text i386 link at `0x01A00000`. The `as_elf` adapter checks
the request, object, selected entry, linked image, and output capacity before
publishing bytes. The JIT command continues to request a fixed image directly
from CupidASM.

CupidLD joins the checked kernel source and object cohort. The old executable
wrapper remains as an adapter boundary until the linked path has enough guest
runtime evidence to remove it cleanly.

## Evidence

CupidASM contracts cover caller-priority entry selection, `main` and `_start`,
selected-symbol visibility, unselected local symbols, relocations for absolute
bindings, fixed-image compatibility, missing entries, malformed requests, and
rollback. Kernel ELF contracts cover linked code-only and code/data/BSS
objects, entry selection, link diagnostics, output preservation, and recovery.

The native CupidASM contract passed object-basic, object-entry, fixed-image,
and error modes. The kernel ELF contract passed linked-object, link-errors,
code-only, code-data-bss, and error modes. Checked CupidC compiled
`cupidasm.cc`, `cupidld.cc`, `as_elf.cc`, and `as.cc`. The active audit now
tracks CupidLD as the 156th checked kernel source.

The first complete image build reached the exact-size gate after CupidDis had
accepted all 431 production inputs. The gate correctly rejected the older
kernel sizes. The reviewed policy now records 9,190,860 bytes for pass one,
9,313,740 bytes for the final ELF, and 9,096,008 bytes for the flattened
kernel. These are the deterministic artifacts produced with the new
137,444-byte CupidLD object in the kernel link.

With that policy in place, the complete `cupidos.img` build passed in 653.2
seconds. A private QEMU boot then ran `ls` through the guest terminal and
reached its JIT completion marker in 46.9 seconds.

## Consequences

Kernel AOT follows the same assembler-to-object-to-linker boundary as hosted
program construction. Final placement, symbol resolution, and relocation
application belong to CupidLD. JIT latency and fixed-region behavior do not
change. A future cleanup may remove the remaining wrapper code after a full
guest AOT run proves that no compatibility caller still needs it.
