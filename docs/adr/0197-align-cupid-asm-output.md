# ADR 0197: Align Cupid ASM output explicitly

## Status

Accepted on 2026-08-01.

## Context

The active FPU demo needs a 16-byte FXSAVE and FXRSTOR area. CupidASM had no
alignment statement, so the demo put that buffer first in `.data` and relied
on the kernel loader to align the section. That arrangement did not express
the instruction's requirement in source, and moving another object ahead of
the buffer could silently break it.

The missing statement affected more than one output format. A raw binary can
start at a nonzero `ORG`, an ELF32 object needs both padded contents and a
correct `sh_addralign`, and a fixed image may receive an unaligned region base
from its caller. NOBITS sections need logical padding without file bytes.
Treating every case as a section-relative byte count would produce a wrong
fixed address for an unaligned base.

## Decision

Cupid ASM accepts `align POWER_OF_TWO[, FILL_BYTE]`. Both operands are
constant expressions. The boundary must be a nonzero power of two within the
32-bit layout range. The optional fill must fit in one byte and defaults to
zero.

Raw output aligns `ORG + output offset`. ELF32 relocatable output aligns the
current section offset and raises the section's required alignment. Labels,
symbols, and relocations after the statement use their padded offsets. A
NOBITS section grows logically and writes no bytes, so it rejects a nonzero
fill.

Fixed-image layout aligns the absolute address `region base + bytes used`.
The aligned address is then converted back to a region offset. Statements
inside a section use the same padding rule after section placement. Overflow
in either calculation fails before publication.

The active FPU demo places `align 16` immediately before its 512-byte save
area.

## Evidence

The first focused contract run failed because `align` was still parsed as an
unknown instruction. The matching CLI test failed at the same source line.

The green native contract covers raw output at `ORG 0x101`, a nonzero fill,
aligned label values, ELF32 PROGBITS bytes, NOBITS logical size, section
alignment, relocations after padding, and fixed output with an unaligned data
base. A separate fixed-image case aligns an empty code section at an unaligned
base, uses its label as both the entry point and a data reference, and emits no
code-region bytes. Its paired overflow case proves that an empty section does
not bypass absolute-address validation. The contract also covers zero and
non-power-of-two boundaries, a fill larger than one byte, nonzero NOBITS fill,
same-job recovery, and deterministic repetition.

The focused commands passed:

```text
make -C toolchain build/cupidasm-contract.exe
toolchain/build/cupidasm-contract.exe alignment
python -m unittest -v tests.test_toolchain_cupidasm.CupidAsmCliTests.test_cli_aligns_raw_addresses_and_elf32_sections
```

The selector reported `alignment: ok`. The final fourteen-test CupidASM CLI,
demo, active-source, and kernel run passed in 8.477 seconds.

## Rejected alternatives

Keeping the FXSAVE buffer first in `.data` was rejected because source order
is not an alignment declaration.

Aligning only the section-relative offset was rejected because a fixed image
may start its region at an unaligned absolute address.

Writing fill bytes into NOBITS was rejected because it would turn logical
zero-filled memory into file-backed data.

Passing the demo through NASM was rejected because active source requirements
belong in Cupid ASM.

## Consequences

Cupid ASM source can state address and section alignment without ordering
workarounds or a host assembler. Raw, ELF32, NOBITS, and fixed-image output
share one validated boundary rule. The change moves no source owner, build
transform, or host dependency.

General macros and unrestricted NASM directive compatibility remain open.
