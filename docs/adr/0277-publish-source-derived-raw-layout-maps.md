# ADR 0277: Publish source-derived raw layout maps

## Status

Accepted on 2026-08-13.

## Context

CupidDis can inspect a flat image with typed 16-bit code, 32-bit code, and data
ranges. The SMP trampoline uses that boundary in production, but its ranges
are maintained by the caller. The bootloader also mixes code modes and data,
so strict inspection needs a map that stays aligned with the assembly source
as statements move.

Inferring code from bytes would repeat the ambiguity the typed map was meant
to remove. A table can contain valid opcodes, and an instruction stream can
contain bytes that look like data. CupidASM already knows the statement kind,
active `BITS` mode, output offset, and `ORG` base during layout.

## Decision

Raw CupidASM results now publish an origin and an ordered set of coalesced
ranges. An instruction is code16 or code32 according to its statement mode.
Data declarations, alignment, and reserved storage are data. Empty statements
create no range. Adjacent statements with the same kind coalesce.

The hosted assembler accepts `--map PATH` with raw output. It writes the
deterministic `cupid.raw-map.v1` schema with exact size, base, and ordered range
starts. CupidDis accepts that schema through `--raw --range-map PATH`. The
file-driven form is exclusive with manual mode, base, and range options. It
rejects a stale size, missing or repeated fields, invalid kinds, unordered
starts, oversized input, and trailing malformed records before decoding.

Hostbuild has one checked raw-image transaction shared by the SMP trampoline
and bootloader callers. The transaction owns the output lock, source and seed
freezing, drift checks, private output pins, tool execution, publication-boundary
checks, and atomic replacement. Each caller retains its image size and raw-map
policy. The bootloader caller asks CupidASM for private image and map
candidates, requires the 2,560-byte image size, and runs checked CupidDis with
`--require-known`. A failed assembly, map parse, strict decode, drift check, or
publication step preserves the previous output.

The normal Make rule continues to call the current checked CupidASM directly
until a promoted seed carries both map options. Moving the production edge
before that promotion would make the intermediate commit unbuildable.

ADR 0281 later promotes a Windows seed with both options. ADR 0283 moves the
normal boot rule onto this transaction after that prerequisite is satisfied.

## Evidence

Public tests cover mixed code16, code32, data, alignment, reserve statements,
nonzero origin, deterministic map text, option conflicts, malformed and stale
maps, unknown code, output preservation, source drift, seed drift, and
successful recovery. The active `boot/boot.asm` source produces an unchanged
2,560-byte image, and CupidDis accepts its generated map with no unknown,
invalid, or truncated instruction.

Forty-five focused CupidASM, CupidDis, source, bootloader-hostbuild, and SMP
trampoline tests pass in 11.495 seconds. The shared C contract also checks
zeroed raw metadata on failure.

After the SMP and bootloader paths moved onto the shared transaction, the
central eight-test transaction suite passed in 1.201 seconds. The tests cover
both callers while keeping their image and map policies separate.

A follow-up added direct mismatch negatives and live-output drift checks for
both callers. The expanded eleven-test suite passed in 1.708 seconds.

## Consequences

The assembler, rather than a copied offset list or a byte heuristic, becomes
the authority for flat-image layout. CupidDis remains the independent decoder
and strict policy gate. Hostbuild owns one transaction instead of duplicating
locking and publication rules in each caller. The source capability adds no
host code generator. Production bootloader inspection and its extra CupidDis
participation begin only after the checked seed promotion recorded in ADR 0281
and the production cutover recorded in ADR 0283.
