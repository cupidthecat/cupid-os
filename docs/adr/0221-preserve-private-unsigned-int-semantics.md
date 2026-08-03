# ADR 0221: Preserve private unsigned 32-bit semantics

## Status

Accepted on 2026-08-03.

## Context

The Browser stores an array length in one 32-bit lane. ECMAScript permits
array indices through 4,294,967,294 and derives a length as high as
4,294,967,295. The earlier signed lane rejected every canonical index above
2,147,483,646 even though the source already used `unsigned int` values and
`u`-suffixed constants.

Private CupidC preserved unsigned constants during compile-time evaluation,
but runtime declarations collapsed `unsigned int` and `uint32_t` into signed
`int`. Loads through parameters, arrays, fields, and pointers lost their
signedness. Comparisons, division, remainder, right shift, compound forms,
unary operators, enum constants, and conversion to floating point could then
use signed i386 behavior above `INT_MAX`.

Browser-only comparison or conversion helpers would hide the compiler gap and
leave every other private CupidC program with the same incorrect semantics.

## Decision

Give private CupidC distinct four-byte unsigned value and pointer types. Carry
them through typedefs, declarations, parameters, returns, arrays, record
fields, pointer dereference, indexing, assignment, calls, and expression
results. The usual conversion for two represented four-byte integer operands
produces unsigned when either operand is unsigned.

Use unsigned condition codes for relations, `DIV` for division and remainder,
and logical right shift for unsigned values. `/=` and `>>=` use the same
operation type. A shift assignment takes its signedness from the promoted left
operand, not the count. Unary `+`, `-`, and `~` preserve unsigned type, and
enum symbols retain the type of an unsigned enumerator.

A conditional expression computes its represented integer type from both
arms, independent of source order. `sizeof` produces the i386 unsigned
`size_t` lane. Return parsing retains the declared scalar result type for an
ordinary function or class method, converts unsigned values to the declared
floating lane, and rejects a floating expression returned as unsigned with
the existing focused diagnostic.

Convert unsigned 32-bit values to `double` exactly by splitting the value into
its upper 31 bits and low bit, converting both signed-safe pieces, then
reconstructing the result. The `float` path performs the same exact conversion
to `double` before rounding to binary32. This covers constants, global and
automatic initialization, assignment, casts, arguments, returns, and mixed
arithmetic.

Conversion from `float` or `double` to unsigned 32-bit remains outside this
private boundary. Casts, initializers, assignments, arguments, pointer stores,
array stores, and returns fail with one focused diagnostic and leave the
compiler able to process a later valid request.

Publish `TYPE_UINT` for each kernel binding whose local function-pointer result
is `uint32_t`, `size_t`, or `swap_handle_t`. Narrow `uint8_t` and `uint16_t`
results retain C integer promotion. The complete table therefore contains 205
promoted integer, 40 unsigned-word, and 191 `void` results alongside its
floating and pointer groups.

Store Browser array lengths as `unsigned int`. Parse canonical decimal index
keys with the ECMAScript upper bound of 4,294,967,294, grow length with
unsigned comparison and addition, and convert length directly to `double`
when publishing a JavaScript number. The key 4,294,967,295 remains an ordinary
property and does not change length. Direct assignment to `length` remains
unsupported and transactional.

## Evidence

Private execution contracts cross the sign bit through locals, parameters,
returns, arrays, fields, pointers, aliases, and enums. They check all six
relations, division, remainder, right shift, `/=`, `>>=`, unary operations,
mixed arithmetic, and exact conversions of zero, `0x7fffffff`, `0x80000000`,
and `0xffffffff` to both floating widths. Conditional tests place the unsigned
arm on either side. A wrap-sensitive `sizeof` expression checks the unsigned
result. Ordinary and method returns cross the sign bit into `float` and
`double`, while a floating-to-unsigned return fails and a later request
recovers. Signed division and shift cases stay unchanged.

The binding contract checks all 510 declarations, pins the 205/40/191 integer,
unsigned, and void split, and names `htonl` as a high-bit unsigned result.
Separate guards keep narrow unsigned results in the promoted integer lane.

Browser contracts pin the unsigned length declaration, direct conversions,
the complete canonical-index range, the non-index upper value, failed direct
length assignment, and source free of compatibility helpers. The exact hosted,
checked-object, and guest results are recorded in `docs/bootstrap/LOG.md`.

## Rejected alternatives

Keeping a signed array length and rejecting the upper half of the ECMAScript
range was rejected because it changes JavaScript behavior to match the
compiler.

Comparing decimal keys through a signed accumulator was rejected because the
conversion itself is invalid above `INT_MAX`. The parser first validates the
bounded decimal spelling and only then accumulates an unsigned value.

Adding Browser helpers for unsigned comparison or conversion was rejected.
The language already expresses those operations, so CupidC must preserve
their meaning.

## Consequences

Private CupidC now represents ordinary unsigned 32-bit runtime operations that
the active Browser needs. Browser arrays can grow across the sign boundary and
through the ECMAScript maximum length without narrowing their source.

Floating-to-unsigned conversion, wider private unsigned arithmetic, and the
remaining JavaScript coercion work stay open. No build owner moves, no host
dependency is removed, and `TempleOS/` remains untouched reference material.
