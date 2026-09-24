# ADR 0182: Complete the Doom compatibility object frontier

## Status

Accepted on 2026-07-29.

## Context

CupidC already emitted all 80 sources in the vendored Doom tree, but the
three port compatibility roots had a separate profile and no complete object
frontier.

`kernel/doom/doomgeneric_cupidos.c` already compiled. Two unchanged source
requirements blocked the other roots:

- `kernel/doom/doom_libc_stubs.c` initializes `snd_musiccmd` with
  `(char *)""`.
- `kernel/doom/dglibc.c` defines `dg_setjmp` and `dg_longjmp` in one
  file-scope GNU assembly block.

The implementation had to represent those requirements without removing the
cast, replacing the jump routines, or passing the assembly text to a host
assembler.

## Decision

Treat an explicit non-atomic pointer-to-pointer cast as a
representation-preserving layer while extracting a static string or linked
binding address. The underlying initializer keeps its string bytes, binding,
and target-byte addend. A cast through an integer type does not receive this
rule.

Recognize the exact combined `dg_setjmp` and `dg_longjmp` file-scope assembly
effect from `dglibc.c`. Require visible external declarations with the source
prototypes:

- `int dg_setjmp(uint32_t *env)`
- `void dg_longjmp(uint32_t *env, int value)`

Reject a conflicting C definition, declaration attribute, missing function,
wrong prototype, or changed assembly template.

Emit both prologue-free global functions through Cupid's shared x86 model.
`dg_setjmp` saves EBX, ESI, EDI, EBP, ESP, and the return address, then
returns zero. `dg_longjmp` changes a zero result to one, restores the saved
state, places the result in EAX, and jumps through the saved return address.
The memory forms use the encoder's shortest valid displacement.

Keep the three normal Make recipes on the host compiler in this capability
increment. Production ownership, `.cc` renames, seed promotion, link
comparison, and runtime proof belong to the next handoff.

## Evidence

The exact assembly contract emits 65 text bytes with no relocation:

| Function | Offset | Bytes |
| --- | ---: | ---: |
| `dg_setjmp` | 0 | 27 |
| `dg_longjmp` | 27 | 38 |

Both symbols are global `STT_FUNC` definitions. CupidDis decodes every
instruction and checks the branch target, compact memory displacements, and
indirect jump.

The contract rejects an altered zero-result rule, a missing declaration, a
wrong `dg_setjmp` prototype, and a constrained output buffer. It also checks
rollback, deterministic repeat emission, and same-job recovery.

The static-initializer contract covers both `(char *)""` and `(int *)&target`.
It rejects `(int *)(unsigned int)&target`, which would discard the pointer
address through an integer representation.

The complete `DOOM_COMPAT_I386` profile now produces:

| Source | Object bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/doom/dglibc.c` | 27,992 | `88e3a66488e09ee15769e666971dd34ed0fe0707a54f9962f5f7dadbe4fd4224` |
| `kernel/doom/doom_libc_stubs.c` | 14,352 | `8f667113c54fa0b0d27ce83d134242065ba5b9258324a809e11e72229752ff3b` |
| `kernel/doom/doomgeneric_cupidos.c` | 10,232 | `5274b91dfa7bac56cd83ff0f8096eb5a06fef5e61f91ebb3b80efacc8ad2a9cb` |

The host-built current compiler and a Cupid-built current compiler produce
the same bytes for all three roots. The separate exact Doom-tree gate still
emits all 80 vendored objects.

The first jump emitter reused an older helper that forces every memory
displacement to 32 bits. It produced a correct but nonmatching 57-byte
`dg_longjmp`. A separate compact load path now lets the shared encoder choose
zero- or eight-bit displacements for this source-driven assembly effect
without changing established C code generation.

The first self-hosted frontier run passed the Windows repository path to the
Linux compiler as plain text. That compiler correctly reported the source as
missing. Passing the root as a path object lets the existing WSL adapter
translate it, and the repeated gate passes.

## Rejected alternatives

Removing the explicit string cast was rejected because the source is valid C
and the frontend can retain its static address meaning.

Rewriting the jump routines in C was rejected because their register and
stack behavior is the requirement.

Sending the assembly block to GAS was rejected because it would add a hidden
host-assembler dependency.

Appending raw opcode bytes was rejected because Cupid's x86 encoder and
decoder already own the required instructions and addressing forms.

Accepting any two functions named `dg_setjmp` and `dg_longjmp` was rejected.
The exact prototypes and absence of competing C definitions are part of the
ABI boundary.

## Consequences

Compiler head now emits deterministic i386 ELF32 objects for all 83 Doom and
port roots. The current compiler can build a compiler that reproduces the
three new compatibility objects exactly.

This increment does not change production ownership. Host C still owns the
83 normal Doom and port objects, and their sources keep the `.c` suffix until
their normal recipes move to CupidC.

The next steps are to promote a seed carrying these capabilities, compare the
Cupid and host objects at the link boundary, transfer the normal recipes,
rename the owned roots to `.cc`, and run the image and Doom runtime gates.

`TempleOS/` remains untouched reference material.
