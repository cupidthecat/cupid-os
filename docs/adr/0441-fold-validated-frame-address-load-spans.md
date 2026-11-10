# ADR 0441: Fold validated frame address and load spans

## Status

Implemented privately on 2026-10-06. Both native producers pass selection and
runtime controls. Compilation by current Cupid seeds and full guarded
publication controls are in progress. Compiler qualification, source-frontier
measurement and OS acceptance for this emitter remain open.

## Context

The unsigned rotate fold reduces SHA compression time but the Linux large
publication controls still exceed their existing deadline. Ordinary parameter
and local reads also use an address, push, pop, memory load and another push.
The repeated guards must keep reading each complete file.

## Decision

Let the ordinary emitter handlers validate and emit both IR instructions.
For adjacent parameter or local address and load instructions, inspect their
complete emitted span. Fold only an EBP-relative `LEA`, `PUSH EAX`, `POP EAX`,
`MOV EAX, [EAX]`, `PUSH EAX` sequence. Emit the direct frame `MOV` and final
`PUSH` through the shared x86 encoder. Preserve both short and full-width frame
displacements, including negative offsets.

Record branch targets before emission. An incoming branch to the load blocks
the fold. An incoming branch to the address keeps its existing start offset.
The folded pair has the same stack result, one memory read, EAX value and
flags. Naked functions keep ordinary emission. Narrow, wide and floating load
protocols, relocations, snapshot work and other byte sequences do not match.

This is an emitter optimization. It adds no public IR fields or syntax and
leaves C source, guarded publication checks and test deadlines intact. The
handlers retain their argument, type and metadata validation. Conservative
output-limit failures can occur while emitting the original span before it is
shortened.

## Evidence and limits

The original checked compilers fail the direct-frame-read selector on both
hosts. Four controls pass with the native emitter prototypes in less than a
second per host. They cover signed and unsigned parameters, a volatile
parameter read, aliases, negative offsets beyond a short displacement,
branches, dereferenced pointers, narrow signed and unsigned loads and full
wide values. Actual i386 execution passes on Windows and Linux. Both runtime
objects are byte-identical, SHA-256
`3d00e418a42b31ff24f06b9dbdeb082417f591dcfd2c3184ffcb1da50592112f`.

The repeated-block SHA benchmark takes 32.386 seconds with the old object and
18.912 seconds with the combined rotate and frame-load prototype. Both return
the same independent block digest. This is a compression experiment, not a
complete file or publication acceptance result. The prototype compiler still
links a native emitter with earlier native components.

A Windows text write initially changed the edited source from LF to CRLF.
The source now retains LF. Recompilation produces an identical Linux native
object; Windows native objects differ only in their COFF timestamp. The exact
raw facts and timestamp difference remain retained. Current Cupid compilation
and the new compiler fixed point must supply their own evidence.
