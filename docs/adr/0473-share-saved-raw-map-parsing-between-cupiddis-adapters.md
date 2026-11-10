# ADR 0473: Share saved raw-map parsing between CupidDis adapters

## Status

Accepted on 2026-10-16. Runtime qualification is recorded separately in the
[bootstrap record](../bootstrap/RAW-FILE-DISASSEMBLY.md).

## Context

The kernel assembler writes a flat image and a source-derived v2 map. The
public disassembler accepts typed raw buffers, but the shell's saved-file
command only selected ELF or CupidC source. Its user could not inspect the
saved raw pair with the same tool.

The v1/v2 text parser lived in the hosted driver and used host heap storage.
Copying it into the kernel would create two map grammars. Inferring mode and
data from flat bytes would lose the source information retained by CupidASM.

## Decision

Move the text grammar into `ctool_dis_parse_raw_map`. It returns typed metadata
and exact arena-owned arrays. The caller matches the recorded image size to
its loaded input. Failures clear the result, rewind operation allocations,
and return a static diagnostic. The text has a 1 MiB bound. A counting pass
replaces the hosted parser's repeated heap growth; resource failure may
therefore precede a malformed-line diagnostic under a constrained arena.

The hosted adapter retains file reading and copies successful arrays into its
existing ownership. The kernel uses a separate map job with the text bound
applied at file capture, and retains it through inspection and rendering.

The shell accepts an explicit fixed mode/base or `--range-map`, with optional
`--require-known`. It checks the complete joined raw pathname before the
existing resolver can truncate it. Argument parsing is a public typed seam
so malformed commands can be tested without launching the shell.

## Consequences

The shell can inspect saved raw output without executing it. The shared core
continues to validate typed range/edge metadata and decode code. Strict-known
selection checks decode counts before a listing is rendered. It does not
select the hosted local-target or source-edge policies.

Legacy buffer, ELF, and CupidC source callers keep their existing behavior.
No encoding, object format, ABI, build ownership, or installed seed changes.
The bootstrap record carries the tests, limits, review fixes, and guest proof.
