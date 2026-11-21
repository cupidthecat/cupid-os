# ADR 0338: Inspect Cupid-built PE32 tools with CupidDis

## Status

Accepted on 2026-08-24.

## Context

CupidLD emits one deterministic i386 PE32 profile for the checked Windows
tools. The hosted CupidDis CLI recognized raw and ELF32 input only, so Python
was the sole structural reader for those PE images. That left the Windows
execution cohort outside CupidDis's typed inspection path.

The required reader is narrower than a general PE loader. Current artifacts
are static console images with the repository DOS stub, i386 COFF header,
PE32 optional header, canonical section order, and optional named imports in
one `.idata` section. Their loaded span does not exceed the producer's 2 GiB
RVA limit. Dynamic linking, base relocations, ordinal imports, and arbitrary
PE layouts are outside this profile.

## Decision

Add a typed, transactional PE32 reader behind `pe32.h`. It accepts only the
CupidLD profile and reports exact headers, sections, import libraries, and
named procedures. It rejects truncated headers, unsupported COFF or optional
headers, overlapping or out-of-file sections, invalid entry placement, and
malformed import tables with format-specific diagnostics. `SizeOfImage` must
stay at or below `0x80000000`, even when most of the span belongs to a
memory-only section.

Hosted CupidDis detects `MZ` input, exposes header, section, import, and
disassembly views, and sends each executable section through the shared x86
decoder. The strict policies validate the PE entry against decoded instruction
starts and check constant direct relative targets across executable sections.
Symbols and relocations remain unsupported for PE32 because this static profile
does not carry those tables.

Keep the Python PE validator as an independent parity oracle. The tests run
both readers over every checked Windows seed image and use CupidLD to build a
small import-free fixture. Malformed and transactional cases stay at the typed
reader, report, and CLI seams.

The checked Linux build plan still has 19 C objects and no separate PE reader
object. `pe32_impl.h` therefore carries the implementation inside CupidDis's
existing translation unit. The public `pe32.h` seam is independent of that
temporary carriage. Moving it to its own `.cc` object requires a later
fixed-point plan and seed promotion.

## Evidence

The hosted CupidDis module passes 31 tests, with one platform skip, in 22.640
seconds. The typed `pe32` contract mode passes against checked
`cupidld.exe`. The checked Windows and Linux seed manifests both verify, and
checked-seed CupidC compiles the source-current CupidDis object.

The positive CLI suite inspects `cupidasm.exe`, `cupidc.exe`, `cupiddis.exe`,
`cupidld.exe`, and `cupidobj.exe`. The Python validator checks the same bytes,
entries, and import sets. A separate Python parse now reconstructs every
rendered header, directory, section, import-library group, procedure, lookup
cell, and IAT cell for an exact comparison with CupidDis. Negative coverage
includes truncated and malformed DOS, COFF, and optional headers; a small file
with a memory-only section beyond the 2 GiB image limit; overlapping and
out-of-bounds sections; ordinal thunks; bad import-directory and IAT extents;
misordered lookup tables; unterminated lookup tables and procedure names;
unknown opcodes; invalid local targets and entry anchors; report rollback;
failed-output preservation; and same-process recovery after a post-allocation
failure.

`make bootstrap-audit` and `make check-bootstrap-audit` pass after the
required baseline follow-up. The generated graph has 741 active inputs, 452
transforms, and the unchanged 19-object fixed-point plan. The Toolchain
manifest source inventories now contain 72 publication inputs and 52
bootstrap inputs. A complete Toolchain publication and seed promotion are not
part of this decision. A full static fixed-point test entered its stage build
but was stopped before a result to leave the combined seed-promotion lane an
uncontended compiler. This decision therefore makes no fixed-point carriage
claim.

## Rejected alternatives

Duplicating the Python parser inside CupidDis was rejected because the typed
reader is useful below the CLI and keeps one C representation of the accepted
profile.

Accepting arbitrary PE32 files was rejected because the implementation and
tests cover only CupidLD's deterministic static layout.

Weakening the existing PE producer or its import model was rejected. The
reader follows current artifacts instead of narrowing them.

Adding a twentieth Linux object without a new fixed-point proof was rejected.
The implementation header preserves the checked plan until a later promotion
can add a separate object coherently.

## Consequences

Source-head hosted CupidDis can inspect the five checked PE tools and new
CupidLD outputs without a host disassembler. The checked seed executables do
not gain this CLI feature until a later promotion, and no production recipe
selects PE inspection yet.

The accepted PE surface is intentionally small. Dynamic PE files, ordinal
imports, base relocations, noncanonical section layouts, PE symbols, and PE
relocations receive unsupported or malformed-input diagnostics rather than a
partial claim.

The new files use `.h`; no `.c` source was added or renamed. `TempleOS/`
remains untouched reference material.
