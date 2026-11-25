# ADR 0443: Fold validated immediate rotate-call spans

## Status

Implemented privately on 2026-10-06. Strict native compilation and all ten
frame and rotate-call controls pass on both hosts. Current Cupid compilation
and full large-publication controls are running. Full compiler qualification,
source-frontier measurement and OS acceptance remain open.

## Context

ADR 0442 removes cdecl calls to proven local rotate helpers. SHA still pushes
each literal count, pops it into ECX and uses CL. The ordinary integer handler
already validates and emits that count, and the direct-call handler validates
both argument types and the exact pure helper body.

## Decision

After ordinary emission, recognize one adjacent integer instruction and
selected direct-call instruction. Require their complete ten-byte span:
`PUSH imm32`, `POP ECX`, `POP EAX`, `ROR EAX, CL` or `ROL EAX, CL`, and
`PUSH EAX`. Only counts from 1 through 31 qualify. Rewind that private span
and emit `POP EAX`, the shared encoder's immediate rotate and `PUSH EAX`.
The represented eight-bit immediate produces the existing C1 encoding even
for count one.

An incoming branch to the call prohibits the fold. An incoming branch to the
integer retains the combined start offset. The pair keeps the original
abstract-stack effect and the value expression's evaluation. Other literal
encodings, dynamic counts, reversed arguments, external calls and counts
outside the defined source range keep their existing emission. There are no
new IR fields, syntax or publisher exceptions.

## Evidence and limits

The positive controls check immediate right rotates by seven and thirty-one,
and a left rotate by one. Counts zero and thirty-two keep CL emission in
object inspection; runtime tests execute only defined source expressions.
An external constant-count call retains its exact call relocation. Actual
i386 execution checks six values, dynamic counts, immediate counts, argument
side effects and ordinary-call boundaries on both hosts. Four existing frame
controls also pass. Evidence is `rotate-immediate-emitter1-native3-*`.

The first matcher assumed a MOV-and-PUSH literal sequence; retained output
shows the ordinary handler emits PUSH imm32. The corrected matcher uses that
complete span. The next assertion assumed D1 for count one, while the shared
encoder's explicitly sized immediate uses C1. Keep that canonical encoding
and update the positive assertion. Both failed attempts remain retained;
runtime semantics passed during each attempt.

The native prototype links the new emitter with earlier native compiler
components. This evidence does not qualify a Cupid producer cohort or install
new tools. Full behavior controls, reviewed code-shape expectations, capture
and ownership updates, the source frontier and normal OS/runtime acceptance
are required before adoption. Existing file-identity checks, complete digest
rereads and the original publication deadlines remain in force.
