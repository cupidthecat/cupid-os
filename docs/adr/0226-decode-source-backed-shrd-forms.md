# ADR 0226: Decode source-backed SHRD forms through the shared x86 model

## Status

Accepted on 2026-08-03.

## Context

An active host build of `toolchain/ctool.cc` contains two
`0F AD F8` instructions in `ctool_buffer_put_le64` and
`ctool_buffer_patch_le64`. GNU objdump identifies both as
`shrd eax, edi, cl`. CupidDis did not know that family. It rendered each
opcode prefix as data, then decoded the remaining `F8` byte as `clc`.

The production assembly cohort was already covered by the active-surface
oracle, so adding another assembly syntax feature without a measured byte
sequence would not improve the current frontier. The active Toolchain object
provided a specific shared encode and decode requirement.

## Decision

Add canonical `SHRD` to the private shared x86 catalogue. Four rows cover
16-bit and 32-bit destinations with either an eight-bit immediate count or the
fixed `CL` count. Each destination may be a same-width register or memory
operand, and the source must be a same-width register. Normal operand-size and
address-size overrides apply in both execution modes.

Represent the fixed third operand with one catalogue flag for operand two.
The encoder requires `CL`; the decoder reconstructs that register. CupidASM
and CupidDis continue to use the same rows, form identity, prefix policy, and
transactional public operations.

## Evidence

The shared x86 contract covers register and memory destinations in both
widths, immediate and `CL` counts, both execution modes, cross-mode operand
and address overrides, exact bytes, semantic decode, form replay, and every
truncated prefix of a complete instruction. The two address-override cases
lock exact `66 67` encodings and replay memory operands after decode. Invalid
width pairs, a memory source, a count other than `CL`, a serialized 16-bit
count, and `LOCK` all leave a zeroed encoding and permit a later valid
operation in the same job.

The focused x86, CupidASM, and CupidDis suites pass 38 tests with one existing
platform skip. Rebuilt CupidDis renders the two active `ctool.o` sites as
`shrd eax, edi, cl`. A strict scan of ten representative checked-CupidC
objects finds no true raw-data fallback. The shared model now contains 596
forms, 245 canonical mnemonics, 64 registers, and fingerprint `DA15E97F`.
The standalone checked bootstrap rebuilt identical 445,616-byte CupidASM and
379,648-byte CupidDis images in stages two and three.

## Rejected alternatives

Teaching only the CupidDis renderer about `0F AD` was rejected because it
would create a second opcode authority and leave CupidASM unable to emit the
same instruction.

Treating this byte sequence as a one-off decoder exception was rejected. SHRD
has a regular i386 operand family, and all four rows fit the existing typed
catalogue without a procedural codec.

Adding SHLD without a source requirement was deferred. The catalogue remains
driven by active inputs and observed compiler output.

## Consequences

CupidASM can encode the complete represented SHRD family, and CupidDis no
longer loses instruction boundaries at the two active sites. The checked seed
will retain its older 592-row model until a later promotion. SHLD, the rest of
the measured compiler-padding frontier, DWARF v4, and broader x86 inspection
remain open. `TempleOS/` remains untouched reference material.
