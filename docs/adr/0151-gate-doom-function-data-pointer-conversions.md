# ADR 0151: Gate Doom function and data pointer conversions

## Status

Accepted on 2026-07-28.

## Context

Four Doom translation units contain eleven conversions between function
pointers and `void *`:

| Source | Lines | Use |
| --- | --- | --- |
| `kernel/doom/src/m_menu.c` | 701, 733, 948, 1060, 1173 | Pass five `void (*)(int)` callbacks through the `void *` parameter of `M_StartMessage`. |
| `kernel/doom/src/m_menu.c` | 1297 | Restore the `void *` parameter to the global callback pointer. |
| `kernel/doom/src/p_saveg.c` | 251, 257 | Read and write an `actionf_p1` through the save format's `void *` helper. |
| `kernel/doom/src/p_saveg.c` | 1712 | Cast the `void *` null macro to `actionf_v`. |
| `kernel/doom/src/p_ceilng.c` | 316 | Cast the `void *` null macro to `actionf_v`. |
| `kernel/doom/src/p_plats.c` | 274 | Cast the `void *` null macro to `actionf_v`. |

The host Doom recipe permits these conversions. CupidC could parse the
explicit casts, but Linear IR rejected them. It rejected the implicit
assignment and argument conversions in the frontend. Rewriting the callbacks
or save helpers would conceal a source requirement that the compatibility
profile needs to state directly.

## Decision

CupidC has a `compatibility_pointer_conversions` parse-request flag. The
`--doom-compat` driver option enables it alongside Doom's implicit-function
rule. The generated build audit enables both settings only for
`DOOM_COMPAT_I386` and `DOOM_TREE_I386`. Plain `--gnu` does not enable either
one.

The rule accepts a conversion only when exactly one pointer refers to a
function and the other refers to `void` or an object. The outer pointers and
their referents must be unqualified and non-atomic. Linear IR also requires
both pointer values to have complete four-byte i386 object representations.
Function-to-function and data-to-data cases continue through the existing C
rules instead of this compatibility path.

The frontend records
`CTOOL_C_CONVERSION_COMPATIBILITY_POINTER` on implicit conversions used by
assignment, automatic initialization, fixed call arguments, and returns. It
records the same semantic on a qualifying explicit cast. Freeze validation
checks the mode, expression kind, child ownership, postorder, and exact type
pair.

Linear IR rechecks the types before it publishes a `CONVERT` instruction. The
i386 emitter uses the same validator and emits no instruction bytes because
both representations are the same four-byte value. The distinct semantic
marker keeps this exception visible through every compiler stage.

Malformed request booleans, qualifiers, atomic types, nonpointer operands,
wrong conversion markers, and forged eight-byte pointer layouts fail
transactionally. Strict C and plain GNU mode still reject the implicit
conversions. Their explicit casts remain outside the supported IR boundary.

## Consequences

The four unchanged Doom sources now emit deterministic ELF32 objects under
the exact audited profile. A focused execution oracle stores a callback in
`void *`, restores it, performs an aligned indirect cdecl call, checks its
argument, and observes the callback return value while preserving the
callee-saved registers and caller stack.

The exact Doom-tree frontier advances from 74 to 78 of 80 objects. Two pinned
failures remain:

- `kernel/doom/src/i_video.c:144` reaches an invalid Linear IR unit.
- `kernel/doom/src/info.c:128` needs positional union active-member
  initialization.

This is a compiler-head capability. It does not update the checked seed, move
a Doom recipe, rename a `.c` file, change the OS image, or retire a host
dependency. Issue #29 remains open for the last two compiler boundaries, the
83-file ownership handoff, optimization policy, and runtime proof.
