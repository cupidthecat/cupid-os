# ADR 0222: Generate kernel-symbol source with CupidObj

## Status

Accepted on 2026-08-03.

## Context

The normal image uses two kernel links. CupidDis reads the pass-one executable
and prints its symbols. Python then parses that text, selects kernel text
symbols, builds the `KSYM` blob, and renders `kernel/cpu/ksyms_data.cc` as
packed i386 words. Checked-seed CupidC compiles the generated source before the
second link.

CupidDis already owns symbol extraction, but Python still owns a production C
source format and its binary payload. That ownership cannot move until a Cupid
tool can reproduce the exact source, reject malformed symbol text, and preserve
the old output when generation fails.

## Decision

Add `CTOOL_OBJ_GENERATE_KSYMS_SOURCE` to the transactional CupidObj API and
expose it as:

```text
cupidobj ksyms-source SYMBOLS -o OUTPUT
```

The input is canonical CupidDis or `nm`-style symbol text. CupidObj accepts
ASCII whitespace, ignores blank lines, undefined symbols, nontext symbol
classes, and private `.L` names, and retains `t`, `T`, `w`, and `W` symbols.
Every retained row must contain an i386 hexadecimal address, one symbol-class
character, and one name. A malformed row reports its source line. An omitted,
invalid, or wider-than-i386 address and an inventory with no text symbols have
separate diagnostics.

Sort retained symbols by address and source order. When several names share an
address, keep the first. Emit the existing little-endian blob header, address
and string-offset rows, terminated names, four-byte tail padding, and exact C
source spelling. The source banner stays byte-compatible with the Python
generator until the production transfer is complete.

All symbol inventory and blob storage comes from the job arena. Validation,
allocation, and output failures rewind that storage, zero the result, and
preserve an existing CLI destination. A later request in the same job can
succeed.

Keep the Python generator as an independent byte oracle. This capability does
not change the normal Make recipe. The checked seed must first carry the new
command.

## Evidence

Public and hosted contracts cover canonical text, local and global text,
weak text, ignored symbols, private labels, input order, equal-address
deduplication, exact blob bytes, exact source bytes, deterministic repeats,
malformed second-line locations, invalid and omitted addresses, i386 overflow,
an empty text inventory, output exhaustion, arena exhaustion, rollback, and
same-job recovery.

A producer-to-consumer test assembles a real ELF32 object with CupidASM, reads
it through CupidDis `-n`, and passes that exact text to CupidObj. Both a local
`t` symbol and a global `T` symbol survive, and the generated source matches
the Python oracle.

The source compiler builds the updated CupidObj contract and implementation.
The fixed-point bootstrap compiles all nineteen C objects and startup twice,
links all five tools twice, and preserves stage-two/stage-three byte identity.
Both stages pass eleven successful operations, seven failures, and five help
paths. The candidate CupidObj image is 270,700 bytes with SHA-256
`a8de7de19d1ffbec90f0603f0f796f4a03fa74b8181c62f0f395b22a52423d1d`.
The checked seed remains deliberately different until a separate promotion.
Exact command results are recorded in `docs/bootstrap/LOG.md`.

## Rejected alternatives

Leaving Python as the permanent source generator was rejected because the
format is a deterministic object-packaging transform that fits CupidObj.

Teaching CupidDis to emit C source was rejected. CupidDis owns inspection;
CupidObj already owns binary wrapping and generated installation source.

Parsing the live pass-one ELF independently inside CupidObj was rejected. The
production pipeline already has a typed CupidDis boundary, and retaining its
text output keeps each tool's responsibility narrow and testable.

Publishing output before the pass-one executable and seed manifest are
rechecked was rejected for the later production transfer. A valid result from
stale inputs must not replace the current source.

## Consequences

Source-head CupidObj can reproduce the production kernel-symbol source from
CupidDis text without Python constructing either the blob or C initializer.
The command is part of the fixed-point behavior matrix and has transactional
failure behavior.

The checked seed and normal image recipe do not change in this decision. A
separate promotion must install the new CupidObj image, followed by a separate
ownership transfer that keeps Python only as checked-seed orchestration and an
optional parity oracle. `TempleOS/` remains untouched reference material.
