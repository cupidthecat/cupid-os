# ADR 0288: Apply runtime integer and long-double usual conversions

## Status

Accepted on 2026-08-14.

## Context

CupidC could already cast, assign, pass, and return values between `long
double` and every represented integer width. The x87 emitter had the signed
and unsigned conversion sequences, including the unsigned 64-bit correction.
Static expressions also mixed integer and long-double values. Runtime
arithmetic, comparisons, and conditional arms still stopped at a frontend
feature diagnostic.

That rejection did not reflect a C language boundary. The existing source
model and emitter could represent the conversion, but Linear IR and object
validation admitted it only for casts and assignments.

## Decision

Runtime `+`, `-`, `*`, `/`, all six comparisons, and conditional selection
apply the usual arithmetic conversions when one operand is a non-atomic
`long double` and the other is a represented value integer. The integer set
includes `_Bool`, plain `char`, signed and unsigned `char`, `short`, `int`,
`long`, and `long long`, plus enums with a represented compatible integer
type. Both operand orders and both conditional-arm orders use the same rule.

The frontend promotes the expression to `long double` and records a
`USUAL_ARITHMETIC` conversion on the integer value. Linear IR preserves that
conversion instead of treating it as a cast or assignment. Conditional
lowering stays lazy, so only the selected arm is evaluated and converted.

The i386 emitter reuses the x87 conversion path established for explicit
casts. It packs each integer value into the required signed input, uses
`FILD`, and stores the resulting 80-bit value in the existing twelve-byte
snapshot. Unsigned 64-bit input keeps the existing correction above `2^63`
and restores the caller's x87 control word. Arithmetic and comparison then
use the established long-double operations. No new instruction form,
relocation, object format, calling convention, or runtime helper is needed.

Atomic integer input remains invalid. Operations that mix an eight-byte
integer with `float` or `double` still have no represented conversion path.
Compound assignment and prefix or postfix update of a `long double` lvalue
also remain outside this decision.

ADR 0289 later removed that `float` and `double` limit. It admits the existing
wide x87 conversion at the frontend, Linear IR, and emitter validation seams.

## Evidence

The arithmetic frontend contract first failed at the old integer and
long-double feature diagnostic. The comparison and conditional fixtures
failed at their corresponding focused diagnostics. After the frontend
change, the same source reached Linear IR, where the first run failed on the
new usual-arithmetic conversion. After IR accepted the conversion, the first
object run stopped in emitter validation. Each public seam was changed only
after its red case was captured.

The final arithmetic fixture contains 29 functions. It covers every standard
integer kind and an enum, both operand orders, and all four operators. Linear
IR publishes 199 instructions with fingerprint `C26BE4E541B45681`. The
comparison fixture contains 38 functions and 248 instructions with
fingerprint `A2406DE3452F7DB7`; its new cases cover every signed and unsigned
i386 width across all six predicates. The conditional fixture covers every
signed and unsigned width in both arm orders and checks its branch-local
conversion. An `_Atomic int` arithmetic case keeps a precise negative
diagnostic.

The decoder-driven object proof checks seventeen focused functions. Every
case contains a 64-bit `FILD` input and an 80-bit load. Arithmetic cases also
contain the expected x87 operation, comparison cases contain `FUCOMIP`, and
conditional cases contain the expected control-flow join. The complete
long-double object has 4,478 text bytes with fingerprint `94B88BF9`, 42
symbols, and 11 relocations. Repeated emission remains deterministic and
same-job recovery still succeeds after a constrained-output failure.

All focused frontend, IR, and object modes pass. The production wrapper also
builds source-head CupidC through the hosted and checked Cupid-built drivers.
Both drivers accept a source that combines integer and long-double
arithmetic, comparison, and conditional selection, and their emitted objects
match byte for byte. That end-to-end check passed in 42.8 seconds.

The first combined frontend, IR, and object sweep passed 288 of 296 tests.
Its eight failures were exact drift guards: five active-source counters, one
frontend source inventory, one hosted-adapter object inventory, and one
self-host object inventory. Every failed object had already parsed, emitted
twice identically, and passed its structural checks. The locks were refreshed
from those measured results. The five audit guards then passed together, and
the three contract failures passed together in 48.536 seconds.

## Rejected alternatives

Requiring an explicit cast was rejected. It would move a standard C
conversion into source and leave the compiler's ordinary arithmetic rules
incomplete.

Converting both conditional arms before branching was rejected because the
conditional operator evaluates only its selected arm.

Routing values through host `long double` was rejected. The host layout and
rounding environment are not part of the i386 target contract, and the
existing Cupid-owned x87 path already provides deterministic target behavior.

## Consequences

Source-head CupidC now has one runtime rule for integer and `long double`
across arithmetic, comparisons, and conditional selection. The static and
runtime language boundaries agree for these operators.

The checked execution seed predates this source-head extension. A later
fixed-point promotion must carry the compiler and contract changes before the
seed can claim the capability. This change moves no production source owner,
adds no host dependency, changes no ABI, and creates no honest `.c` to `.cc`
rename.

ADR 0289 follows this decision with the matching source-head rule for wide
integer and `float` or `double` expressions.
