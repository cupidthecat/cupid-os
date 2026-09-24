# ADR 0248: Link deterministic PE32 imports and run a Cupid-built Windows command

## Status

Accepted on 2026-08-09.

## Context

ADR 0247 gave source-head CupidLD ownership of one deterministic, import-free
PE32 layout. That proved the final image format, but it could not call the
Windows loader or run useful code. The checked seed still contains static i386
Linux tools, and Windows still runs those producers through WSL.

The next useful boundary is a small executable built by CupidC and CupidASM,
linked by CupidLD, and loaded directly by Windows. The linker must own the
import table. A host linker, import library, C runtime, or host assembler would
turn the result into another format oracle instead of a Cupid-owned command.

## Decision

`ctool_ld_request_t` accepts a borrowed array of PE32 import records. Each
record names an unresolved IAT slot, a DLL, and an exported procedure. The CLI
exposes the same contract as a repeatable option:

```text
--import IAT_SYMBOL=LIBRARY:PROCEDURE
```

Imports are valid only for the fixed i386 PE32 profile. CupidLD sorts them with
an in-place heap by case-insensitive library name, then procedure and slot
name. Each resolved global records whether an import already selected it, so
duplicate slot checks stay linear after sorting even when the duplicates are
in different libraries. CupidLD rejects mixed spellings of one library,
duplicate slots, duplicate library and procedure pairs, invalid names, unused
slots, already-defined slots, and missing strong references. Reordering the
CLI options therefore produces the same bytes.

CupidLD appends one writable, non-executable `.idata` section at the next page
after the ordinary fixed-layout sections. It emits null-terminated import
descriptors, lookup tables, one contiguous IAT range, DLL strings, and aligned
hint and name records. PE directory 1 covers the descriptors and directory 12
covers the IAT. Every other directory remains zero. The fixed image still has
no base-relocation table.

An import symbol resolves to the full virtual address of its four-byte IAT
cell. An input may reference that cell only through a known, zero-addend
`R_386_32` relocation. CupidASM spells the valid call as:

```asm
call dword [__imp_WriteFile]
```

A direct `call __imp_WriteFile` produces `R_386_PC32`. CupidLD rejects it so a
successful link cannot jump into writable `.idata`. Failure rewinds the output
buffer and clears every result field.

PE32 name thunks reserve their high bit for ordinal imports. CupidLD therefore
rejects an import table at or above RVA `0x80000000` and rejects a fixed image
larger than the two-gibibyte name-RVA range. A large NOBITS section cannot push
an otherwise small import table across that boundary.

The repository-owned Windows entry code lives in
`toolchain/hosted/i386-windows/start.asm`. It clears the direction flag,
aligns the stack, calls CupidC `main` with the existing i386 cdecl convention,
and passes the result to `ExitProcess`. Its cdecl stdout bridge calls
`GetStdHandle` and `WriteFile` through the three imported IAT slots. The
headerless CupidC contract prints this exact marker and returns 37:

```text
Cupid-built Windows runtime: ok
```

The checked source-head proof assembles and compiles the two repository files
with both rebuilt stages, compares the two assembly objects, C objects, and PE
images, validates the import tables independently, then runs the stage-two
image directly when the host is Windows. Native execution requires exit 37,
the exact stdout marker, empty stderr, and a ten-second timeout. The bootstrap
report inventories both assembly objects, C objects, and PE images. It also
records the exact import selectors and the observed return code, stdout, and
stderr separately from the producer platform.

The independent validator keeps ADR 0247's strict import-free mode. Its import
mode permits at most five ordered sections and requires writable `.idata`.
Every import RVA is confined to that section. The validator reconstructs the
producer's exact cursor for descriptors, ordered lookup tables, contiguous IAT
tables, DLL strings, aligned hint and name records, and the final virtual
extent. It requires zero alignment bytes, zero hints, null terminators, and
exact directory 1 and 12 extents. Strings may not terminate in section padding
or another section.

The fixed source closure grows from 41 to 43 files. The two Windows sources are
both frozen inputs and explicit Toolchain contract-manifest prerequisites. The
source-head behavior matrix is now five help cases, seventeen successful
operations, and fifteen useful failures.

## Evidence

The hosted CupidLD suite covers one-library execution, reordered options,
multiple libraries, all five section classes, exact directory and thunk
contents, invalid selectors, duplicate and mixed-case imports, undefined and
already-defined slots, nonadjacent duplicate slots, direct IAT calls, nonzero
absolute addends, and transactional recovery. The native contracts pin
CupidASM's `FF 15` encoding and zero-addend `R_386_32` relocation as well as
CupidLD's exact import bytes and two-gibibyte name-RVA rejection.

On Windows, a source-head CupidASM object and a freestanding CupidC object were
linked by CupidLD with `GetStdHandle`, `WriteFile`, and `ExitProcess`. Windows
printed the exact marker, produced no stderr, and returned 37. No host compiler,
assembler, linker, import library, or C runtime contributed to that command.

The build-graph audit now records `cupidld.pe32_imports` and
`cupid.windows_runtime_probe`. Its mutation suite rejects missing public or CLI
threading, skipped import construction, unsafe relocation handling, incorrect
directories, omitted frozen inputs, one-stage proofs, validation after
execution, a missing timeout or output check, and lost failure sentinels.

The final command results and retained hashes are recorded in
`docs/bootstrap/LOG.md`.

## Rejected alternatives

Using a host PE linker or import library would prove that the input objects are
usable, but it would leave import-table ownership outside CupidLD.

Generating callable linker thunks was unnecessary. The IAT cell is the useful
symbol boundary, and CupidASM already emits the required indirect call and
absolute relocation.

Accepting a PC-relative call to an IAT slot was rejected because the linked
image would jump into data. Silently rewriting the relocation would hide an
ABI error in the input object.

Porting all five tool drivers and the full Linux hosted runtime in this change
would mix import-table work with filesystem, allocation, argument, and native
seed design. The small command proves the loader boundary first.

## Consequences

Source-head CupidC, CupidASM, and CupidLD can jointly produce a real imported
i386 Windows command, and Windows can execute it. Import-free PE output remains
byte-compatible with ADR 0247, and the ordinary ELF path remains the normal OS
build path.

This is not a native Windows Toolchain fixed point. The checked producers are
still i386 Linux executables, Windows still reaches them through WSL, and the
checked seed does not carry the new import path. CupidDis and CupidObj are not
yet runnable as Cupid-built Windows tools. The normal kernel, user, Doom, and
Toolchain builds still use the checked Linux seed and Python control plane.

No active C or assembly source changes production ownership in this step, so
no `.c` to `.cc` rename is due. `TempleOS/` remains untouched reference
material.
