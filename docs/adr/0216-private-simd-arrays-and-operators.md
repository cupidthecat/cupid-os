# ADR 0216: Keep private SIMD arrays and operators typed

## Status

Accepted on 2026-08-03.

## Context

Private CupidC already parsed `float4` and `double2` variables and lowered a
set of SSE intrinsics. Direct binary expressions still fell through the scalar
integer path, and fixed arrays of either vector type were rejected. That left
ordinary packed storage and arithmetic unavailable to active programs even
though the compiler already had the register and lane machinery.

The x86 operand order is observable for MIN and MAX, which select the second
operand for NaN and equal signed-zero cases. The first ADD and MUL review
assumed a both-NaN result would always keep the machine destination payload.
The physical WSL runner did so, while the normal QEMU guest selected the other
input from the same emitted instruction. The compiler can promise a stable
source-to-instruction mapping without promising an ADD or MUL payload that the
execution environment does not keep stable.

Automatic objects also lack a guaranteed 16-byte address in the private stack
layout. Reusing aligned packed moves for an indexed array could fault even when
the source declaration was valid.

## Decision

Accept direct `+`, `-`, `*`, and `/` only when both operands have the same
`float4` or `double2` type. Spill the written left value, evaluate the right
value, restore the left into the machine destination, perform the packed
operation, and move the result back to the normal XMM0 expression register.
Use that order for ADD and MUL as well as SUB and DIV. Remove the commutative
lowering flag from the matching ADD and MUL intrinsics. Pin the mapping with an
exact machine-code contract.

Support one-dimensional fixed arrays of either vector type in global,
automatic, block-static, and persistent REPL storage. Each element occupies 16
bytes. Use `MOVUPS` for indirect reads and writes because it is safe for the
private stack layout and has the same bit result at aligned addresses. Plain
assignment and `+=`, `-=`, `*=`, and `/=` retain the vector type, evaluate the
base and index once, and leave the stored value available to the expression.
A following lane access uses the existing vector lane rules.

Check positive bounds, count-by-stride multiplication, and static data
capacity before reserving storage. Implicitly initialized arrays remain zeroed.
`sizeof(*array)` returns 16.

Keep the written source order for MIN and MAX intrinsics. This preserves the
x86 second-operand result for NaN and equal signed-zero inputs.

Reject mixed vector widths, scalar and vector mixing, unsupported binary or
compound operators, multidimensional SIMD arrays, pointers and address
expressions, allocation with `new`, array parameters, record or class field
arrays, and SIMD values crossing the private call ABI. Each rejected family
has a focused diagnostic.

## Evidence

Private i386 execution contracts cover all four direct operators for both
vector widths, direct and intrinsic ADD or MUL with two known NaN inputs, every
supported array storage class, all four indexed compound assignments, REPL
persistence, single index evaluation, lane extraction, zero initialization,
`sizeof`, and neighboring-object integrity. An exact byte contract requires
the written left operand in the destination register for all eight ADD and MUL
forms. Negative contracts cover each unsupported family and recovery after a
failed compile.

The first operand-order review found that direct ADD and MUL and their
intrinsic forms reversed the written operands. Finite values hid the defect. A
new payload test failed before the lowering was corrected, then passed on the
physical host. The first real guest run compiled and completed but failed its
left-payload-only marker because QEMU selected the other written NaN. The final
runtime contract accepts either known input payload, rejects any other bits,
and reports the four binary32 and four binary64 choices. The feature-14
frontier requires separate operator, array, MIN/MAX edge, and NaN markers
before accepting guest completion.

The final combined private CupidC suite passes 67 tests, and all 99 GUI
frontier contracts pass. A checked-seed build compiles the active private
parser in 53.3 seconds. The corrected exact image build passes in 503.1
seconds. A paced four-vCPU e1000 private-image run passes in 62.2 seconds with
all four focused markers, overall PASS, and clean JIT completion. QEMU reports
`float_left=0 float_right=4 double_left=0 double_right=4`, recording its payload
choice without changing the exact emitted machine order.

## Rejected alternatives

Reordering ADD and MUL because their finite arithmetic is commutative was
rejected. Stable lowering makes generated code and source mapping easier to
inspect, and it avoids a separate rule for two operators.

Defining every both-NaN ADD or MUL result as the written left payload was
rejected after the guest evidence. Enforcing that policy would require extra
lane masks and selection around instructions that should remain direct SSE
operations. The private language instead leaves that payload choice to the
processor or emulator while requiring a NaN derived from one of the inputs.

Using `MOVAPS` for arrays was rejected. The private automatic-storage layout
does not promise 16-byte alignment, and an ordinary valid array must not depend
on where the enclosing frame happens to land.

Collapsing a SIMD array into an integer or scalar floating pointer was
rejected. It would discard the 16-byte element width and vector lane type.

Adding a partial SIMD pointer or call representation was rejected for this
slice. Those forms need a complete type and ABI design rather than an untyped
escape hatch.

## Consequences

Private JIT and AOT programs can use matching packed arithmetic and ordinary
one-dimensional fixed SIMD storage without integer lowering or aligned-address
assumptions. The source-to-machine operand mapping is stable. ADD and MUL do
not promise which written NaN payload survives when both inputs are NaNs.

This change does not transfer a build owner or remove a host dependency. SIMD
pointers, multidimensional arrays, record fields, dynamic allocation, array
parameters, and call ABI transport remain open. `TempleOS/` remains untouched
reference material.
