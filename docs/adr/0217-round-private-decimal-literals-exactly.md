# ADR 0217: Round private decimal literals exactly

## Status

Accepted on 2026-08-03.

## Context

Private CupidC converted decimal floating tokens by multiplying a `double`
accumulator and a running fractional scale. The algorithm was short, but it
rounded after each arithmetic step. The ordinary literal `0.75` therefore
entered a binary64 object as `0x3fe8000000000001`, while the equivalent `75e-2`
spelling happened to produce the correct payload. An `f` suffix also passed
through binary64 before narrowing to binary32.

Calling a hosted `strtod` routine is not an option for the in-kernel compiler.
It would add a runtime dependency and would still leave target-width selection
outside CupidC's control. The private compiler needs deterministic conversion
in JIT and AOT mode.

The lexer stores at most 95 characters of a numeric token. Its old loops
stopped when that buffer filled and left the rest of the token in the input,
which caused a second, misleading parse failure. During review, function-body
errors exposed another problem: top-level parser recovery temporarily cleared
the error flag and allowed a later generic expression error to replace the
first lexer diagnostic.

## Decision

Convert a decimal floating token with a fixed 48-limb unsigned integer
workspace. This gives 1536 bits, enough for the accepted 95-character token at
the binary64 subnormal boundary and for the shift used during final rounding.
Build the exact numerator and denominator, determine the binary exponent, and
divide only after applying the target binary scale. Compare twice the
remainder with the denominator and round to nearest with ties to even.

Select binary32 before conversion when the token has an `f` or `F` suffix.
Tokens without that suffix select binary64. Publish the resulting bits through
the existing token value so static and runtime literals use the same payload.
Decimal underflow produces zero, overflow produces infinity, and the existing
unary sign path supplies negative zero.

Count the complete numeric spelling, including its suffix, against the
95-character limit. Consume every digit, decimal point, exponent character,
exponent sign, and suffix before reporting an overlong token. Leave the next
delimiter for the following lexer call. Report an exponent without digits
separately.

Keep the first public error message while parser recovery logs later failures.
Recovery may clear the active error flag so it can continue scanning, but a
nonempty message prevents a later call from replacing that first diagnostic.

Keep hexadecimal floating and `long double` literals outside this boundary.

## Evidence

Tests inspect exact little-endian binary32 and binary64 bytes for `0.75` and
`75e-2`, both directions around halfway values, long binary64 halfway
spellings, minimum subnormals, largest finite values, overflow, underflow,
signed zero, and extreme exponents. A runtime i386 fixture checks that rounded
payloads survive assignment and SSE access. An independent differential review
compared 5,200 generated decimal spellings with the host IEEE conversion and
found no mismatch.

Negative tests cover missing exponent digits, the accepted 95-character
boundary, a 96-character body, a suffix that crosses the same boundary,
delimiter preservation after a rejected token, function-body diagnostics, and
first-message preservation across parser recovery. The review tests failed
with `expected expression` before the diagnostic fix and passed afterward.

The focused compiler module passes 65 tests, the combined private compiler
discovery passes 80, and all 101 GUI and frontier contracts pass. A
checked-seed build compiles both changed private compiler objects in 61.0
seconds. The normal image build passes in 514.0 seconds. The feature-13 guest
contract requires two binary64, two binary32, and three exponent-edge payload
checks before the existing call, libm, overall PASS, and clean JIT markers.

The first guest invocation exposed a host harness bug after the guest had
printed every required marker. The imported pattern used line anchors, but the
single-command matcher enabled dot-all mode without multiline mode. A focused
test failed before both completion helpers were corrected. The unchanged
four-vCPU e1000 command then passed in 67.9 seconds. Its 36,666-byte serial log
has SHA-256
`6a0328d70ac32c57a2859c266e63b054793f3ea8e23753020032e6b43fb09047`.

## Rejected alternatives

Keeping the repeated floating arithmetic converter was rejected because its
result depends on intermediate rounding and already miscompiled `0.75`.

Parsing every token as binary64 and then narrowing an `f` literal was rejected.
Two successive rounding steps can select a different binary32 value from one
direct rounding step.

Calling `strtod`, compiler builtins, or a host math library was rejected. The
private compiler must run inside Cupid OS and produce the same bits without a
host conversion service.

Using an unbounded allocator-backed integer was rejected for this slice. The
source token already has a fixed accepted length, so a checked workspace gives
a simpler memory and failure boundary.

Truncating an overlong token and letting the parser report the remainder was
rejected. It splits one source token into unrelated errors and can hide the
real limit.

## Consequences

Private CupidC now gives decimal `float` and `double` literals deterministic,
target-width IEEE payloads without a host conversion routine. JIT and AOT
programs can rely on halfway, subnormal, finite-limit, overflow, underflow, and
signed-zero behavior. Lexer failures keep a useful public message even when
the parser continues recovery.

This change does not move a build owner or remove a host dependency. The
95-character numeric-token limit, hexadecimal floating literals, and `long
double` literals remain open. `TempleOS/` remains untouched reference
material.
