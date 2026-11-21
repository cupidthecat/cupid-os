# ADR 0458: Read hosted bytes through fgetc and getchar

## Status

Implemented and tested privately on 2026-10-09. Normal producer source
integration, qualification and replacement-tool consumers remain open.

## Context

The required-file handoff fixture pauses for a caller edit through `getchar`.
The hosted header and runtime provide line input and stream indicators but
neither byte-input entry point. Current Cupid compilation rejects the ordinary
fixture at its undeclared function call.

## Decision

Declare `fgetc(FILE *)` and `getchar(void)` in the shared hosted stdio header.
Implement `fgetc` with a one-byte `fread`: return the captured unsigned byte
promoted to `int`, or `EOF` when no byte is read. Implement `getchar` through
`fgetc(stdin)`. Reuse the existing stream EOF/error state, errno policy and
serialized unbuffered read boundary.

The Windows runtime includes the shared implementation. No import, startup
assembly, stream representation or private assembly extension is added.

## Evidence and limits

All four native/Cupid handoff callers pass byte input, immediate EOF and
write-only stream error cases. Inputs include zero, 127, 128 and 255, followed
by EOF; a regular file exercises all 256 byte values, EOF, clearerr and rewind.
The error case checks a sticky error without EOF, indicator reset and an empty
output file. Late required-file and seed edits also pass their stdin pause.

Independent checking rereads every input, output and stream-state report within
the complete 652-call handoff/regression scope. Both changed runtime objects
pass strict CupidDis checks. The native Windows CRT preserves errno 73 on its
write-only read; its separate oracle remains exact. Actual Cupid callers
require a changed nonzero error code.

The missing declaration, initial readiness spelling failures and later
disk-full attempts remain recorded. These tests establish the represented
private byte-input contract. They do not qualify a new runtime seed or replace
normal OS, SDK and public consumer acceptance.

[The library record](../bootstrap/HOSTED-BYTE-INPUT.md) keeps source and test
bindings. All 99 normal producer inputs and fifteen installed seed files remain
unchanged. TempleOS stays read-only and excluded.
