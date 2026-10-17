# ADR 0257: Descend private multidimensional SIMD arrays

## Status

Accepted on 2026-08-10.

## Context

Private CupidC already stored one-dimensional `float4` and `double2` arrays in
global, automatic, block-static, and persistent REPL storage. The ordinary
array layout code could also calculate checked two-dimensional row sizes and
three-dimensional middle strides for a 16-byte element. Four declaration
paths rejected those shapes before the existing layout machinery could use
them.

Subscript lowering had a second gap. A read loaded XMM0 after the first index,
even when that index selected a complete row. Assignment lowering likewise
treated the first computed address as a vector leaf. Removing only the
declaration checks would therefore read or write the start of a row and lose
the remaining dimension metadata.

## Decision

Allow two-dimensional and three-dimensional fixed arrays whose leaf type is
`float4` or `double2`. Use the same checked byte layout as the other fixed
array types. Every vector leaf keeps its 16-byte size, while each outer index
scales by the complete remaining row or middle-slice size.

Record the declared array rank on every global, automatic, block-static, and
persistent REPL symbol. Rank, not byte size, decides whether a subscript names
a row or the final vector. This distinction matters when an inner extent is
one because that row and its vector leaf are both 16 bytes.

When a read subscript still names a row, retain its base type, next stride,
complete remaining object size, and vector leaf type. Load with `MOVUPS` only
after the final subscript selects a vector. Lane access then continues through
the existing SIMD rules.

Use the same row descent for plain and arithmetic compound assignment. A
two-dimensional destination requires two subscripts, and a three-dimensional
destination requires three. Reject an incomplete row store with
`SIMD array assignment requires every subscript` instead of silently writing
the first vector in that row.

Reject an incomplete SIMD row used as a value. Private CupidC has no typed
row-pointer decay yet, so publishing that address as an ordinary pointer would
lose its stride and make pointer arithmetic silently wrong. A row remains
available only while parsing another subscript or an unevaluated `sizeof`.
Grouping parentheses may preserve it on the way to either context, but a
grouped row used as a value is still rejected.

Keep the existing declaration limits. Every dimension must be positive, each
count and stride multiplication must fit the private signed allocation range,
and global, static, local-frame, and REPL capacity checks still run before
storage is committed. `sizeof` reports the selected row or vector size without
evaluating an index expression.

## Evidence

Private execution contracts cover global, automatic, block-static, and
persistent REPL matrices and cubes for both vector widths. They check static
zero initialization, plain assignment, all four arithmetic compound
assignments, lane reads, row and vector `sizeof`, neighboring canaries, and one
evaluation per index on both read and write paths. Unevaluated `sizeof`
indexes remain at zero calls.

Negative contracts cover zero or negative inner bounds, overflowing
two-dimensional and three-dimensional layouts, incomplete row assignment, and
attempts to use a row in truth, arithmetic, or another value context. Unit
inner extents exercise global, automatic, block-static, and persistent REPL
rank storage. Parenthesized two-dimensional and three-dimensional subscript
chains prove that grouping does not change the array lvalue.
The active feature-14 guest adds this required marker:

```text
[feature14-matrix] PASS global=2 local=2 static=2 sizes=8 index=6 unevaluated=2 canary=4
```

The private ABI suite passes all 133 tests. All 116 GUI smoke contracts and all
155 discovered private CupidC tests pass. Ruff and the scoped diff check also
pass. The final embedded-document image rebuild completes in 696.600 seconds.
The corrected private image boots through the ordered matrix, overall feature,
and JIT-completion markers in 79.227 seconds.

## Failed runs and corrections

The first positive test stopped at the old declaration diagnostic,
`SIMD arrays support one dimension`. Removing the four declaration guards made
the source parse but exposed the row-lowering gaps.

The first incomplete-row negative then compiled successfully. Assignment had
accepted `values[1] = vector` for a two-dimensional array and targeted the
first vector in the row. The final lowering tracks whether a SIMD row remains
and rejects the store before parsing its value.

The first correction inferred row state from the remaining byte size. That
failed for declarations such as `float4 values[2][1]`, where a row and a leaf
are both 16 bytes. It also let an incomplete row escape as an ordinary pointer,
which discarded the retained stride during pointer arithmetic. Explicit rank
now controls every read, store, and `sizeof` descent, and incomplete row values
receive a focused diagnostic.

The first value-boundary correction also rejected a row inside grouping
parentheses before a following subscript could consume it. Grouping now carries
the row through its nested expression wrapper. The enclosing wrapper still
requires another subscript or `sizeof`, so the unsupported row-value boundary
does not widen.

An initial full build encountered Doom profile drift while the hosted
toolchain sources were changing in the shared worktree. A stable-tree retry
was used for final build evidence; no source or profile rule was weakened.

The first private-image run reported `[feature14-matrix] FAIL` because its
guest check expected an untouched automatic cube to contain zero. Automatic
storage has indeterminate initial contents. The corrected guest checks an
untouched block-static cube instead, then rebuilds and passes the same runtime
gate.

A first final-image attempt used the four-CPU SMP contract and stopped in the
unchanged EHCI cleanup path before feature-14 compilation. The focused retry
used the same rebuilt image, one CPU, and a shorter held-key pause. It completed
the ordered feature-14 sequence without a panic.

## Rejected alternatives

Flattening a matrix into a one-dimensional vector array was rejected. It would
discard row types, row-sized `sizeof`, and the source's checked dimension
layout.

Loading the first vector of a row and carrying only its address was rejected.
That would hide an incomplete subscript and make later indexing depend on an
untyped pointer convention.

Allowing incomplete row assignment as a copy was not selected. Private CupidC
does not yet have a typed SIMD row-value or aggregate-copy model, so such a
store would claim semantics that the compiler cannot represent consistently.

## Consequences

Private JIT, AOT, and persistent REPL programs can use ordinary two-dimensional
and three-dimensional fixed SIMD storage without flattening the source. This
changes no production build owner and adds no host dependency. SIMD pointers,
record fields, allocation with `new`, array parameters, row values, and call
ABI transport remain open. No `.c` to `.cc` rename is due, and `TempleOS/`
remains read-only reference material.
