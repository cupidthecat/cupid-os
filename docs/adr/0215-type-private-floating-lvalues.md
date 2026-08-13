# ADR 0215: Type private floating lvalues

## Status

Accepted on 2026-08-03.

## Context

Private CupidC first retained floating width only on one-dimensional fixed
array symbols. A `float *` or `double *` still collapsed into the generic
pointer type, so dereference and subscript returned the integer lane. A second
or third array dimension was rejected even though active C uses ordinary
multidimensional objects. Floating fields in structures and classes followed
the same integer path.

Splitting the pointer types exposed three older assumptions. Direct pointer
updates accepted the generic type but advanced it by one byte. Function and
method array parameters decayed every non-integer element to an untyped
pointer. General `sizeof(expression)` kept the scalar expression type but
discarded the remaining array-row size. Small runtime programs reproduced all
three failures before the final implementation.

## Decision

Represent depth-one `float *` and `double *` as distinct private compiler
types. Carry that type through declarations, address expressions, pointer
casts, direct returns, function and method array-parameter decay, assignments,
and calls. Dereference and subscript use `MOVSS` or `MOVSD`; plain and
arithmetic compound stores apply the existing scalar conversion rules. Direct
pointer `++` and `--` advance by the complete pointed-to object size. This also
repairs the existing steps for character, integer, and complete structure
pointers.

Keep every fixed floating dimension in symbol and expression metadata. Global,
automatic, block-static, and persistent REPL storage accepts one, two, or three
dimensions after checking each bound, dimension product, and storage limit.
Each subscript evaluates its base and index once, then publishes the next row
stride or materializes the scalar leaf.

For address-of on a fully indexed array element, keep the computed address
instead of loading the selected object. This covers scalar floating and record
elements. Reject an address that still denotes an array row because the
private type system cannot yet represent pointer-to-array types.

Let structure and class layouts contain scalar `float` and `double` fields and
one-dimensional fixed arrays of either type. The same typed load and store
path serves direct objects, object arrays, and object pointers. Pointer-valued
floating fields retain their pointee type when read.

Parse the operand of general `sizeof(expression)` with normal type rules, then
restore emitted code, data, symbols, stack accounting, call metadata, lvalue
metadata, and expression state. Preserve a real operand diagnostic. When the
operand is an indexed multidimensional array, use its remaining row size.
Otherwise use the represented object type. The operand is never executed.

Reject floating pointer depth greater than one, pointer-to-array types,
indirect floating increment or decrement, bitwise compound assignment on
floating lvalues, fixed SIMD arrays, and assignment through a pointer-valued
floating field subscript with focused diagnostics. A failed REPL expression
must leave the next expression usable.

## Evidence

Private i386 execution tests cover typed pointer reads, stores, compound
updates, function-returned subscripts, address expressions, correctly scaled
pointer updates, and function and method array parameters. Array tests cover
global, automatic, block-static, and persistent REPL matrices and cubes,
checked strides, scalar conversions, single evaluation, and row-sized
unevaluated `sizeof`. Record tests cover scalar and fixed-array fields through
objects, arrays, and pointers. Negative tests cover every boundary listed
above, storage overflow, invalid bounds, and recovery.

The combined private CupidC discovery suite passes all 57 tests in 8.315
seconds after the pointer-decay, pointer-step, row-size, and selected-address
regressions were added as tests. All 98 GUI frontier contracts pass in 1.014
seconds. A strict checked-seed build compiles the final active private parser
in 52.0 seconds.

The first feature-13 guest run reached every earlier marker but reported
`record=14` instead of 26. The value isolated `&records[1]` loading the first
word of the selected record instead of keeping its address. The reduced host
case failed with a segmentation fault. After the address path was corrected,
the 533.0-second image build passed and a four-vCPU e1000 private-image run
finished in 73.2 seconds. It reported
`[feature13-lvalue] PASS array=42 pointer=13 record=26 sizes=56 unevaluated=1`,
the ten-call marker, the overall feature PASS, and clean JIT completion.

## Rejected alternatives

Keeping floating pointers as the generic pointer type was rejected. Width is
needed before the compiler can select a load, store, conversion, or pointer
step.

Flattening multidimensional source into one-dimensional arrays was rejected.
It would make active source accommodate a compiler gap and would lose normal C
row semantics.

Special-casing only the Browser tables was rejected. The same lvalue rules are
needed by ordinary programs, records, parameters, and later self-hosting work.

Treating every `sizeof` subscript as a scalar was rejected. `sizeof(matrix[0])`
names a row object, and evaluating the index would violate C semantics.

## Consequences

Private JIT and AOT programs can use ordinary scalar floating lvalues across
the supported array, pointer, parameter, and record forms without narrowing
through integers. No build owner or host-tool dependency changes.

At this decision point, indirect floating updates were still open. ADR 0273
supersedes that part by covering dereference, index, and record-field updates.
Deeper floating pointers, pointer-to-array types, fixed SIMD arrays, and the
pointer-valued field store boundary remain open. `TempleOS/` remains untouched
reference material.
