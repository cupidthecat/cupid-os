# ADR 0153: Represent one active union initializer

## Status

Accepted on 2026-07-28.

## Context

The unchanged Doom state table initializes a function-pointer union with
ordinary C initializer lists:

```c
typedef union {
  actionf_v acv;
  actionf_p1 acp1;
  actionf_p2 acp2;
} actionf_t;

static state_t states[] = {
    {SPR_TNT1, 0, -1, {NULL}, S_NULL, 0, 0},
};
```

The first positional clause selects the union's first named member. A member
designator may select another direct member instead. CupidC already laid out
unions and carried their bytes when they were nested inside supported
structures, but it rejected every union initializer list before selecting an
active member. That stopped `kernel/doom/src/info.c` even though its data uses
the ordinary one-member form.

Rewriting the table as casts or splitting the union into separate fields would
move a compiler limitation into vendored game code. Treating a union like a
structure would be worse: later positional clauses would appear to initialize
different members even though they occupy the same storage.

## Decision

CupidC accepts one explicit initializer clause for a complete C union. A
positional clause selects the first eligible named member. A direct
`.member` designator selects that member. The selected clause may itself be a
nested initializer list, so union objects work inside arrays, structures, and
block-scope compound literals.

The public initializer forest keeps exactly one edge for a union list. The
edge's subobject index names the selected direct member. A second positional
or designated clause receives the existing excess-element diagnostic. An
unknown member keeps the normal member-designator diagnostic. Cupid class
lists, chained designators, promoted anonymous-member designators, and
multiple clauses that override an earlier union member remain separate work.

Linear IR zeros the complete union object before it computes the selected
member address and stores that member. This preserves C's implicit zero
initialization for padding and for any bytes beyond a narrow selected member.
Static emission uses the selected member's layout offset and writes only that
member over a zero-filled object. Frontend freezing, IR validation, and object
emission each reject a forged union list that owns anything other than one
edge.

The existing aggregate-safety walk still examines every member of the union.
A union that can store a `volatile` or `_Atomic` member is therefore rejected
from the bulk-zero runtime path even when the chosen member is ordinary.

## Evidence

Focused frontend coverage checks positional and designated members, a nested
array of structures, an automatic union, a compound literal, excess
positional and designated clauses, an unknown member, rollback, and recovery.
The Linear IR proof fixes two full-object zero operations, four selected
member addresses, two stores, one staged compound-literal copy, and identical
repeated instruction streams. A union containing a volatile member fails
before IR is published.

The deterministic object proof fixes these bytes:

| Section | Bytes |
| --- | --- |
| `.data` | `12 34 56 78 11 22 33 44` |
| `.rodata` | `01 00 00 00 00 00 00 00 02 00 00 00 EF BE AD DE` |

It also checks exact symbol placement, no relocations, contiguous nonempty
runtime functions, and byte-identical repeat emission.

The complete frontend suite passes 76 tests, the Linear IR suite passes 64,
and the object suite passes 80. The object run includes the complete
19-source, five-tool fixed point. All 62 build-graph audit tests also pass. An
exact `DOOM_TREE_I386` command compiles unchanged
`kernel/doom/src/info.c` into a valid 51,268-byte ELF32 object. The whole
Doom-tree frontier therefore moves from 73 to 74 emitted objects without
changing the source.

## Rejected alternatives

A Doom-only `{NULL}` exception was rejected because the same union rule is
part of ordinary C and is useful throughout the operating system.

Rewriting Doom's state table or compatibility headers was rejected because
the source already expresses its active member clearly.

Walking union members as consecutive structure fields was rejected because
all members share storage and only one member may be selected by this slice.

Accepting repeated designators was deferred because correct C override
semantics must replace the earlier initializer forest edge and its owned
subtree, not silently publish two active members.

## Consequences

Compiler head now represents Doom's state-table union initializers and exact
static bytes. The remaining Doom sources still need implicit-call, IR,
bit-field-promotion, and pointer-conversion work before the complete cohort
can move.

The checked bootstrap seed does not carry this capability. No production
translation unit changes owner, no `.c` source is renamed, and no host
dependency is retired by this decision. `TempleOS/` remains untouched.
