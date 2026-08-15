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

The exact emitter oracle fixes all four packed update sequences. The private
compiler's SIMD selection passes 23 tests, and its update selection passes 15.
The focused emitter oracle passes three tests. Recovery contracts reject a
computed vector, lane, incomplete row, record field, SIMD parameter, SIMD
pointer, and call result before compiling a valid direct update in the same
state.

The active `/bin/feature14_simd.cc` guest adds this required marker:

```text
[feature14-update] PASS direct=6 leaves=3 once=6 payload=8
```

Its private source execution contract and terminal frontier contract both
pass. The marker sits between the matrix and minimum/maximum evidence and has
a matching rejected failure marker.

The complete private compiler module passes all 146 tests, and private CupidC
discovery passes all 168 tests. The 125-test GUI terminal contract, Ruff, and
the private-slice whitespace check also pass. All 12 artifact-policy tests and
the direct nine-artifact verifier accept the reviewed kernel measurements.

The complete production build passes through the CupidC-owned object cohort,
both CupidLD links, strict CupidDis inspection, exact-size verification, and
image publication in 724.4 seconds. A focused private-image QEMU run then
compiles and executes `/bin/feature14_simd.cc` through in-OS CupidC in 67.2
seconds. It records the matrix marker, the new update marker, and JIT completion
without a panic or exception. The final generated audit and its independent
comparison both pass.

## Failed runs and corrections

The first public test stopped at `global scalar type is not supported`. The
direct static allocation paths still assumed that every non-record, non-array
object fit in eight bytes. The final rule admits only the two represented
16-byte vector types.

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

The first complete image build reached the exact-size gate after compilation,
linking, and strict inspection had passed. The implementation and embedded
manuals changed all three kernel outputs. The reviewed policy now records
9,232,096 bytes for `kernel.elf.pass1`, 9,354,976 bytes for `kernel.elf`, and
9,135,688 bytes for `kernel.bin`. The direct verifier, policy tests, and a
second complete build all pass with those values.

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

The remaining SIMD lvalue and ABI forms stay visible as focused gaps. This
change moves no production build owner, adds no host-tool dependency, and does
not justify a `.c` to `.cc` rename. `TempleOS/` remains reference-only and was
not changed or counted.
