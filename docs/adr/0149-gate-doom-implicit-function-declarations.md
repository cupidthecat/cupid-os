# ADR 0149: Gate Doom implicit function declarations

## Status

Accepted on 2026-07-28.

## Context

The audited Doom tree contains five calls made before any declaration is
visible:

- `putchar` at `kernel/doom/src/i_system.c:172`, `:183`, and `:186`
- `system` at `kernel/doom/src/i_system.c:274` and `:342`

The host recipe permits these old C calls with
`-Wno-implicit-function-declaration`. CupidC rejected the first call in every
language profile. Adding declarations to the vendored source would hide the
real compatibility requirement, while accepting undeclared calls in ordinary
C or general GNU mode would weaken every other source cohort.

## Decision

CupidC has an explicit `implicit_function_declarations` parse-request flag.
The command-line spelling is `--doom-compat`. The generated build audit sets
the flag only for `DOOM_COMPAT_I386` and `DOOM_TREE_I386`, and checks that the
same two Make profiles carry the host warning policy. GNU mode alone does not
enable it.

When the flag is set, a bare undeclared identifier followed directly by `(`
introduces the C90 declaration `extern int name()` in the innermost block.
The block record owns the identifier expression that activated it. It also
points to one canonical external function binding, so calls in sibling blocks
and a later compatible declaration keep the same symbol identity.

Each call parsed before a prototype retains the no-prototype function type.
Its arguments receive the usual default promotions, including `float` to
`double`. A later prototype refines the canonical binding and governs later
calls without changing the earlier call records. A conflicting later
declaration remains an error.

Linear IR validates the activation owner, the old-style `int()` type, and the
canonical external link. The binding adds no runtime instruction. Object
emission therefore uses the existing cdecl call path and one ELF symbol per
function name. Forged ownership, activation, or linkage metadata fails
transactionally.

Ordinary C and plain `--gnu` still diagnose an undeclared call. Compatibility
mode also rejects a bare undeclared identifier, incompatible later
declarations, invalid request booleans, and implicit declarations in
unevaluated expressions that have no retained expression owner.

## Consequences

Unchanged `kernel/doom/src/i_system.c` now emits a valid deterministic ELF32
object under the exact audited profile. Its five calls create five lexical
activation records because the two `putchar` calls in `I_PrintDivider` occupy
different nested blocks. Those records still resolve to only two external
symbols, `putchar` and `system`.

The exact Doom-tree frontier advances from 73 to 74 of 80 objects. Six pinned
failures remain:

- `i_video.c:144` reaches an invalid Linear IR unit.
- `info.c:128` needs positional union active-member initialization.
- `m_menu.c:701` needs a Doom callback conversion.
- `p_ceilng.c:316`, `p_plats.c:274`, and `p_saveg.c:251` need the remaining
  audited pointer conversions.

This is a compiler-head compatibility capability. It does not move a Doom
recipe, update the checked seed, rename a `.c` file, change the OS image, or
retire a host dependency. Issue #29 remains open for the six compiler
boundaries, full 83-file ownership, optimization policy, and runtime proof.
