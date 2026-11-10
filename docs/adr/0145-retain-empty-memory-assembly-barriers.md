# ADR 0145: Retain empty memory assembly barriers

## Status

Accepted on 2026-07-27.

## Context

The Doom sound driver uses this GNU C statement to keep surrounding memory
accesses ordered:

```c
__asm__ volatile("" : : : "memory");
```

The empty template is intentional. It emits no machine instruction, while the
`memory` clobber tells the compiler that memory may have changed. CupidC
previously rejected every empty assembly template before it could distinguish
this compiler barrier from an incomplete statement. That stopped
`kernel/doom/i_sound_cupidos.c` even though its target code needed no new x86
encoding.

Removing the barrier from the sound source or replacing it with an arbitrary
instruction would change the source contract to work around a compiler limit.

## Decision

CupidC accepts an empty GNU extended assembly template only when it is
volatile, has exactly one `memory` clobber, and has no input or output
operands. The usual zero-output rule also makes
`asm("" : : : "memory")` volatile, so it reaches the same representation.

The frontend keeps the empty template as an assembly statement. Linear IR
retains a real `ASSEMBLY` instruction at that source position. The i386
emitter validates the frozen flags and operand counts, then emits zero target
bytes for the instruction. This preserves the compiler ordering boundary
without inventing a CPU operation.

Basic empty assembly, an empty template without the memory clobber, and an
empty template with operands fail with a focused frontend diagnostic.
Whitespace-only templates keep the existing missing-instruction diagnostic.
Linear IR and object emission reject forged empty-template metadata before
writing output.

Frontend, IR, and object contracts pin the accepted representation,
transactional failures, repeat behavior, and recovery. The object contract
checks that the barrier function contains only its ordinary i386
prologue and epilogue. The generated exact Doom-tree frontier compiles the
unchanged sound driver through the same ordered forced-include command as the
other roots.

## Consequences

Compiler head now emits valid deterministic ELF32 objects for 72 of the 80
Doom-tree roots. Eight pinned roots remain at static floating initialization,
an implicit declaration, invalid IR, positional union initialization, and
callback or pointer conversions.

This capability is still ahead of the checked bootstrap seed. It does not
transfer a Doom object, rename a Doom source, change an OS artifact, or retire
a host dependency. Issue #29 remains open until all 83 Doom and port sources
compile under the intended compatibility policy, move to checked CupidC
ownership, validate, and pass runtime proof.
