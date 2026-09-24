# ADR 0225: Parse Cupid built-in types in the shared frontend

## Status

Accepted on 2026-08-03.

## Context

The shared declaration frontend already reserved Cupid's built-in type names
in Cupid mode, and ADR 0013 already represented every required scalar and
vector layout. The parser did not connect those two pieces. A declaration
starting with `U32`, `float4`, or another Cupid spelling stopped at the first
token even though the immutable type graph could describe it.

The unchanged `kernel/cpu/simd_intrin.h` file exposed this gap. Its 29 function
declarations use `float4` and `double2`, so the correct Cupid profile could not
publish their prototypes. Treating the names as C11 keywords would create a
different problem because a strict C translation unit may still use them as
ordinary identifiers or typedef names.

## Decision

Recognize `U0`, `U8`, `U16`, `U32`, `U64`, `I8`, `I16`, `I32`, `I64`,
`Bool`, `bool`, `float4`, and `double2` only when the parse request selects
Cupid mode.

Map the sized integer spellings to their exact signed or unsigned target
identities. `U0` names `void`. `Bool` and `bool` share the existing Cupid
32-bit signed `int` identity. `float4` is a four-lane `float` vector and
`double2` is a two-lane `double` vector. Both vector types have size and
alignment 16. Reuse one immutable graph node for each vector type within a
parse job.

Reject a Cupid built-in type combined with another base type at the second
specifier. Keep the published unit unchanged after that failure and allow a
later parse in the same job. C11 mode continues to treat every Cupid spelling
as an ordinary identifier unless the source declares it as a typedef.

## Evidence

The public frontend contract checks every scalar kind, signedness, size, and
alignment. It also checks vector element identity, lane count, layout, type
reuse, function prototypes, invalid mixed specifiers, rollback, and same-job
recovery. The unchanged SIMD intrinsic header now publishes all 29 external
function bindings in Cupid mode. The existing C11 standalone-header contract
still reports its deliberate 157 of 159 result, which proves that this change
did not broaden C11 keywords.

The focused Cupid-type contract and the unchanged C11 header contract pass.
After the generated active-source audit was refreshed, the complete frontend
suite passed all 95 tests in 22.932 seconds. The standalone checked bootstrap
then rebuilt the 2,578,244-byte CupidC image identically in stages two and
three.

## Rejected alternatives

Injecting typedefs through a profile header was rejected. It would give the
spellings C typedef behavior instead of making them native Cupid type
specifiers, and it would not represent the vector identities directly.

Recognizing the spellings in every language mode was rejected because it would
silently reserve valid C11 identifiers.

Copying the private kernel parser's type handling was rejected because the
shared frontend must continue to publish ADR 0013 graph identities and use its
transactional diagnostics.

## Consequences

The shared frontend can now represent Cupid declarations and the complete SIMD
intrinsic prototype header without source edits. Vector expressions, Cupid
class values, vector ABI transport, vector IR, and object emission remain
separate work. The checked seed does not carry this parser change until a
later fixed-point promotion. `TempleOS/` remains untouched reference material.
