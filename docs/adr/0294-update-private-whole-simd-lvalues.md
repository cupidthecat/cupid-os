# ADR 0294: Update private whole SIMD lvalues

## Status

Accepted on 2026-08-15.

## Context

Private CupidC already represented `float4` and `double2` arithmetic, direct
automatic objects, and fixed arrays with as many as three dimensions. Array
leaves supported assignment and arithmetic compound assignment. Prefix and
postfix increment and decrement still stopped at the scalar floating path.

That gap affected more than the packed opcode. File-scope and block-static
direct vectors were rejected by the eight-byte scalar allocation limit. A
final indexed vector load did not publish a modifiable lvalue, even though its
address remained in EAX. Postfix also needed the complete old 128-bit payload,
not a value reconstructed after arithmetic. Reconstructing it would lose
negative zero and could change a NaN payload.

SIMD pointers, record fields, parameters, calls, incomplete rows, lane writes,
and computed vector values do not yet have a complete private representation.
Accepting any of those forms only for updates would create an isolated and
misleading language rule.

## Decision

Allow prefix and postfix `++` and `--` on a modifiable whole `float4` or
`double2` object. The accepted direct storage classes are automatic, global,
block-static, and persistent REPL storage. Also accept a final leaf selected by
all subscripts of a one-, two-, or three-dimensional fixed vector array.

Retain the declaration's `const` qualifier on each direct symbol and fixed
array element, including when one or more typedef aliases carry it. Carry that
fact across every subscript without evaluating an index again. A const vector
remains readable, but plain and arithmetic compound assignment, plus prefix
and postfix `++` and `--`, are rejected before a store. Assignment and update
paths use focused modifiable-whole-vector diagnostics.

Allocate a direct static-duration vector as one 16-byte object. Read and write
it with the same unaligned-safe `MOVUPS` form used for automatic vectors and
array leaves. This removes the artificial eight-byte allocation rejection
without weakening the limit for unrelated types.

Form exact integer one in EAX, convert it to the vector's scalar width in XMM1,
and broadcast that lane with `SHUFPS` or `SHUFPD`. Emit packed addition or
subtraction against XMM0. Prefix leaves the stored result in XMM0.

For postfix, copy all of XMM0 to XMM2 before arithmetic. Store the new vector,
then copy XMM2 back to XMM0. This preserves every bit of the old result. An
indexed update pushes its already computed EAX address before forming one and
restores that address for the store. No index expression is parsed or emitted
again.

Publish indirect lvalue identity only after the final vector subscript. A row
keeps its rank metadata and remains rejected. Keep SIMD pointers, fields,
parameters, calls, rows, lane updates, and computed vector updates outside this
slice. Report the closest unsupported form, then let a later compilation reuse
the same compiler state.

## Evidence

The public private-compiler contracts execute direct automatic, global,
block-static, and persistent REPL updates, as well as fully indexed global and
block-static leaves. They cover both vector widths, prefix and postfix forms,
JIT, AOT, and REPL compilation. Side-effect counters prove one evaluation for
every subscript in one, two, and three dimensions. Postfix checks compare all
eight 32-bit words from old binary32 and binary64 vectors, including known NaN
and negative-zero payloads.

The exact emitter oracle fixes all four packed update sequences. Recovery
contracts reject const-qualified assignment, arithmetic compound assignment,
and direct or indexed updates. They also reject a computed vector, lane,
incomplete row, record field, SIMD parameter, SIMD pointer, and call result
before compiling a valid mutation in the same state. A separate persistent
REPL contract rejects an update to a const vector, restores the prior state,
and accepts a later mutable update.

Additional contracts cover direct, chained, and fixed-array typedef aliases
in normal and persistent REPL compilation. They confirm that const values
remain readable, then reject direct or indexed assignment, arithmetic compound
assignment, prefix update, and postfix update before same-state recovery.

The active `/bin/feature14_simd.cc` guest adds this required marker:

```text
[feature14-update] PASS direct=6 leaves=3 once=6 payload=8
```

Its private source execution contract and terminal frontier contract both
pass. The marker sits between the matrix and minimum/maximum evidence and has
a matching rejected failure marker.

Seven focused const, recovery, and emitter contracts pass in 7.119 seconds.
Private CupidC discovery passes all 172 tests in 24.861 seconds, and the hosted
frontend passes all 97 tests in 12.735 seconds. The 125-test GUI terminal
contract passes in 0.617 seconds, and all 12 artifact-policy tests pass in
1.593 seconds. Ruff and the whitespace check also pass.

The complete production build passes through the CupidC-owned object cohort,
both CupidLD links, strict CupidDis inspection, exact-size verification, and
image publication in 634.5 seconds. A focused private-image QEMU run then
compiles and executes `/bin/feature14_simd.cc` through in-OS CupidC in 63.2
seconds. It records the matrix marker, the update marker, and JIT completion
without a panic or exception. Audit regeneration passes in 61.817 seconds, and
the final independent comparison passes in 63.638 seconds. The contract,
production, and audit sweep passes all 204 tests in 766.982 seconds.

## Failed runs and corrections

The first public test stopped at `global scalar type is not supported`. The
direct static allocation paths still assumed that every non-record, non-array
object fit in eight bytes. The final rule admits only `float4` and `double2`,
the two represented 16-byte vector types.

The first REPL execution test compiled each source unit but ran only the final
entry, so an earlier standalone call never initialized the persistent objects.
The corrected contract defines the update function in one unit and calls it
from the final verification unit. This tests persistent declarations and code
without assuming that the host harness executes intermediate entries.

The first incomplete-row statement produced the older assignment diagnostic.
The parser now recognizes a pending update and reports that row values are not
supported. Prefix row updates use the same rank check.

The existing emitter oracle encoded the old design decision by requiring
vector rejection. It now accepts whole vectors, still rejects aggregates, and
checks the exact packed bytes instead of preserving a stale limitation.

The first review fixture showed that `const float4 value; ++value;` still
compiled and changed the object. Type parsing had consumed the qualifier
without saving it. Symbol metadata now records const on direct objects and
fixed-array elements. Direct assignment, expression updates, and the
statement-specialized indexed path check that metadata before emission.

The original feature build reached the exact-size gate after compilation,
linking, and strict inspection had passed. Its reviewed policy and second build
were green. During review, two direct Make attempts timed out while rebuilding
stale prerequisites serially. The second left Make and compiler child processes
running; they were identified and stopped before the normal parallel build.

The integrated review then added the missing const and typedef metadata,
diagnostics, tests, and embedded manual updates. A 641.1-second measurement
build reached the exact-size gate after all preceding production stages passed.
It reported 9,236,336 bytes for `kernel.elf.pass1`, 9,359,216 bytes for
`kernel.elf`, and 9,139,028 bytes for `kernel.bin`. The policy was updated. The
final parallel build passed all nine policy checks and published the image.

## Rejected alternatives

Lowering four scalar updates was rejected. It would duplicate lane extraction
and storage logic, create more address evaluations, and fail to express the
packed language operation.

Recomputing an indexed address for the store was rejected because a subscript
may call a function or advance a counter. The update must keep the destination
selected by its first evaluation.

Recovering a postfix value with inverse arithmetic was rejected because
floating arithmetic is not reversible and does not preserve raw payloads.

Treating a row as its first vector was rejected because it would silently drop
rank and stride. Adding partial SIMD pointer, field, parameter, or call support
was also rejected. Those forms need their own complete type, storage, and ABI
work.

## Consequences

Private JIT, AOT, and persistent REPL code can use ordinary whole-vector
increment and decrement without flattening arrays or rewriting vectors lane by
lane. Direct static-duration vectors now have complete storage and zero
initialization. Prefix returns the new vector, postfix returns the exact old
payload, and each indexed destination is selected once.

Const-qualified vectors remain readable. Plain and arithmetic compound
assignment, plus prefix and postfix `++` and `--`, are rejected before a store.
This rule covers direct automatic, global, block-static, and persistent REPL
objects plus fully indexed fixed-array leaves. Const enforcement outside
represented direct and fixed-array SIMD lvalues remains separate work.

The remaining SIMD lvalue and ABI forms stay visible as focused gaps. This
change moves no production build owner, adds no host-tool dependency, and does
not justify a `.c` to `.cc` rename. `TempleOS/` remains reference-only and was
not changed or counted.
