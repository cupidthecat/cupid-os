# Type CupidDis raw ranges as code or data

- Status: Accepted
- Date: 2026-08-01
- Extends: ADR 0080

## Context

ADR 0080 let one raw inspection request change between 16-bit and 32-bit
decoding. That model still treated every byte as an instruction. The active
SMP trampoline does not have that shape. Code occupies `[0x000, 0x01f)`, data
occupies `[0x01f, 0x210)`, code occupies `[0x210, 0x254)`, and data occupies
`[0x254, 0x1000)`.

With only a mode map, CupidDis decoded all 3,997 non-code bytes in that image.
Zero-filled regions appeared as long runs of `add byte` instructions, and a
table byte could consume the first byte of later code if a caller placed only
the mode transition. The output looked authoritative even though the caller
already knew those intervals were data.

## Decision

The public raw map now uses `ctool_dis_raw_range_kind_t`. Each ordered range
is one of `CTOOL_DIS_RAW_RANGE_CODE16`, `CTOOL_DIS_RAW_RANGE_CODE32`, or
`CTOOL_DIS_RAW_RANGE_DATA`. `CTOOL_DIS_RAW_RANGE_MAP` is the canonical map
selector. `CTOOL_DIS_RAW_MODE_MAP` remains as a compatibility alias for the
former selector.

Range starts still partition the complete input. The first start is zero,
later starts increase strictly, and every start is inside the source. This
keeps the borrowed map compact and leaves no gaps with an unstated meaning.
Code ranges use the shared x86 decoder. Data ranges go straight to bounded
`db` rows and never enter the decoder. A data row stops at a raw label so the
label keeps its exact address.

The hosted CLI adds `--range-at OFFSET:16|32|data`. The initial `--mode 16|32`
still supplies the first code range. The older `--mode-at OFFSET:16|32`
spelling remains available for code-only transitions.

Inspection validates the complete borrowed map before publishing a report.
Rendering validates it again before writing. Invalid kinds, starts outside
the input, and duplicate or decreasing starts fail with distinct diagnostics.
A changed report produces no output, and the same job can render a later
valid report.

## Evidence

The public C contract renders a code16, data, code32, code16 fixture twice and
gets identical text. A label in the middle of the data interval starts a new
row at its exact address. The data bytes are valid-looking x86 opcodes, but the
output contains only literal `db` rows and no invented `add byte` instruction.
The contract also rejects an invalid range kind during inspection and after a
published report has been changed. Existing zero-report and same-job recovery
checks remain in place.

The CLI contract assembles unchanged `kernel/smp/smp_trampoline.S` with
CupidASM. The result is 4,096 bytes with SHA-256
`b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90`.
It then applies this map:

```text
0x000 code16
0x01f data
0x210 code32
0x254 data
```

Two renders match byte for byte. The 16-bit startup begins at `0x8000`, the
32-bit entry begins at `0x8210`, and the data rows begin at `0x801f` and
`0x8254`. Neither zero-filled interval is presented as an `add` instruction.
CLI negatives keep the duplicate-start diagnostic and add an exact end-of-file
boundary failure.

## Rejected alternatives

Teaching the x86 decoder about tables was rejected because those bytes do not
have instruction semantics.

Using one input file per region was rejected because it would split labels,
base addresses, and output order across several requests.

Giving each range an explicit end was rejected because adjacent ordered
starts already describe a gap-free partition. A second boundary would add
overlap and gap states without serving the active artifacts.

Automatically finding code and data was rejected because a flat binary does
not carry enough information to make that choice reliably. The caller owns
the map, and CupidDis owns its validation and presentation.

## Consequences

CupidDis can now present the active boot and trampoline layouts without
inventing instructions for their tables or padding. This changes the shared
report contract and hosted CLI, so both checked compiler stages must rebuild
and match the updated CupidDis closure and contract.

No production owner or host dependency changes. The normal build still uses
CupidDis for ELF symbol inspection, while raw maps remain an explicit
inspection interface. Dynamic ELF, DWARF, source-derived map generation, and
native Windows self-hosting remain separate work.
