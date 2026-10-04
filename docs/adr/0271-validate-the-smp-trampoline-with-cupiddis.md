# ADR 0271: Validate the SMP trampoline with CupidDis

## Status

Accepted on 2026-08-13.

## Context

CupidASM already owned the raw SMP trampoline, but the normal recipe published
its output without checking the instruction map. The 4 KiB image mixes 16-bit
startup code, tables and reserved bytes, a 32-bit entry section, and trailing
data. Treating every byte as one code mode would either miss bad instructions
or misclassify deliberate data.

CupidDis already supports ordered raw ranges and strict known-instruction
validation. The remaining work was to place that capability on the production
edge without weakening output preservation.

## Decision

Replace the direct trampoline recipe with one guarded hostbuild operation. It
freezes the source and the complete selected seed, assembles a private
candidate with CupidASM, and requires an exact size of 4,096 bytes. CupidDis
then validates this map at base `0x8000`:

- 16-bit code from `0x000` through `0x01e`;
- data from `0x01f` through `0x20f`;
- 32-bit code from `0x210` through `0x253`;
- data from `0x254` through the end of the image.

The disassembler runs with `--require-known`. Source drift, seed drift,
candidate drift, unexpected output, or a validation error stops publication.
An adjacent lock and atomic replacement protect the previous trampoline.

## Evidence

Positive tests bind the exact CupidASM and CupidDis argument vectors. Negative
tests cover a strict disassembly failure, successful tools that write either
output stream, a wrong-sized candidate, and source, seed, or candidate drift.
Each case leaves the existing trampoline unchanged. Hostbuild and build-graph
suites pass with both tools recorded on the transform.

The dedicated transaction module passes all three tests in 0.523 seconds. The
source-current poisoned-host build then assembled and checked the trampoline
through the native Windows execution seed before linking both kernel passes.

The real production rule produces 4,096 bytes with SHA-256
`b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90`.
The hash matches the preceding assembly output.

The exact image built from that trampoline passed the four-vCPU RTL8139
frontier in 820.7 seconds, including all four processors online and the full
SMP runtime contract.

## Consequences

The normal boot path now checks the mixed-mode trampoline with CupidDis before
publishing it. CupidASM remains the sole assembler, and the source layout is
unchanged.

Host Python still coordinates the transaction. Replacing that control plane
belongs to CupidBuild work rather than this binary-format gate.
