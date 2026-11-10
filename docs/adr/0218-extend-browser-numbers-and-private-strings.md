# ADR 0218: Extend Browser numbers and private strings

## Status

Accepted on 2026-08-03.

## Context

ADR 0210 moved the Browser's JavaScript number tables to binary64 and covered
decimal fractions, exponents, comparisons, truth, division, and a small set of
non-finite cases. Common literal and primitive operations were still missing.
Hexadecimal, binary, and octal tokens were split or skipped, separators were
not checked, string-to-number conversion accepted prefixes of invalid text,
and loose equality treated many same-tag values as equal. Remainder converted
the quotient to a 32-bit integer, which failed for large finite operands.
`%=` was not parsed, and string `+=` took the numeric path.

String addition had another silent limit. It formatted each operand into 256
bytes and joined them in a 512-byte stack buffer, so otherwise valid strings
were cut without an error. Numeric trimming recognized only ASCII whitespace,
and relational comparison ordered UTF-8 storage bytes instead of JavaScript's
UTF-16 code units.

The expanded asset-free self-test exposed a separate compiler boundary. Its
script is written as neighboring C string tokens. Private CupidC decoded each
token into a 1,024-byte field, then copied every adjacent token through another
1,024-byte combined buffer. A valid joined string was silently truncated even
though the compiler's data section had room for it. Making the active test
smaller would have hidden a general C string defect.

## Decision

Give the Browser lexer one strict numeric scanner. Accept decimal integer,
fraction, and exponent forms plus `0x`, `0b`, and `0o` radix forms in either
letter case. Accept `_` only between two valid digits. Require at least one
radix digit, reject digits outside a binary or octal base, reject a decimal
exponent without digits, and reject an identifier immediately after a numeric
token. Keep the existing 400-step decimal exponent cap.

Keep every parsed value in the existing binary64 token and AST fields. Preserve
unary negative zero by negating the converted operand directly.

Implement the represented primitive conversion rules as one shared runtime
path. Undefined converts to NaN; null, false, true, and the empty trimmed string
convert to zero or one as appropriate. String conversion decodes UTF-8 while
trimming the ECMAScript whitespace set, then consumes the complete input. It
accepts decimal fractions and exponents, unsigned radix strings, and signed
`Infinity`. Invalid or partially consumed text returns NaN. Numeric separators
are source syntax and remain invalid inside converted strings.

Use same-type primitive equality for strict comparison. Loose comparison also
accepts null with undefined, Boolean-to-number conversion, and number/string
conversion. Decode represented UTF-8 strings and compare their UTF-16 code
units for equality and relational operators. Malformed UTF-8 advances one byte
at a time, which keeps comparison bounded and deterministic. Keep
object-to-primitive conversion outside this slice.

Use `fmod` for `%` and `%=` so large operands, infinity, NaN, and signed zero
follow the represented floating path. Reuse one concatenation helper for `+`
and `+=` whenever either operand is a string. String operands keep their pool
slices, non-string primitives use small formatting buffers, and the result is
written straight into the remaining 64 KiB string pool. Report pool exhaustion
without publishing a partial value. Format NaN and signed infinity explicitly.

Resolve an assignment target once before evaluating its right side. Keep the
binding or the member receiver and computed key in a small reference record,
load through that record for compound assignment, then store through the same
identity. The store consumes its internal value copy, so repeated assignment
statements do not grow the interpreter value stack. Recheck a previously
unresolved name after the right side in case a nested assignment created it.
Computed-key conversion reports string-pool exhaustion before a failed intern
can become an invalid offset.

Record the owning scope on every binding. Lookups walk the scope chain and
search bindings in reverse allocation order, filtering by that owner. A `var`
or function declaration searches only its current scope before allocating a
binding, while ordinary expression lookup may still find a parent. This avoids
the old assumption that one scope's bindings occupy a contiguous range; a
nested call can allocate its parameter and local bindings while its caller is
still evaluating a right side.

Make value-stack capacity a checked runtime boundary. Every push reserves a
slot or reports `js: value stack overflow`. Expression, call, assignment,
condition, loop, initializer, and return paths restore their entry depth when
an evaluation fails. A completely full stack is rejected before expression
side effects begin. Native and DOM values use the same checked slot helper.

Treat the string pool as a checked boundary at every entry point, not only
concatenation. Interning validates the source slice and reserves the complete
string before moving the pool cursor. Lexer, `typeof`, DOM conversion, global
installation, and self-test lookups stop when interning fails. They do not
publish a token, binding, property, or value with an invalid offset. If global
installation fails, the HTML path reports that error and does not run queued
scripts.

Carry a native function's ID beside its value tag through the value stack,
bindings, object properties, and call returns. A user function may accept and
return a native function without turning it into native ID zero or a user
function. Strict equality compares both the native tag and ID.

Give arrays an explicit index boundary. A successful canonical index write
grows `length` to one past the index. The runtime length lane is a signed
integer, so canonical ECMAScript indices from 2,147,483,647 through
4,294,967,294 fail with `js: array index exceeds runtime limit`. The key
4,294,967,295 is not an ECMAScript array index and remains an ordinary
property. Direct assignment to the synthetic `length` property fails with
`js: array length assignment unsupported` without changing the array.

Format finite numbers without first narrowing their integer part to a signed
32-bit value. Plain notation covers represented values below `1e21`, including
4,294,967,295 and `1e20`. Scientific notation covers magnitudes at or above
`1e21` and nonzero magnitudes below `1e-6`, including `1e-7`. The current
formatter keeps at most six fractional decimal places. Shortest round-trip
formatting remains separate work.

Expand `browser --selftest` to 26 computed fields. Require ten malformed-input
diagnostics, then run one valid radix and separator script to prove recovery in
the same Browser invocation.

Keep each private C string token within its existing 1,024-byte token field,
which permits 1,023 decoded bytes plus a terminator. Consume an overlong token
through its closing quote before reporting a focused diagnostic. Emit adjacent
tokens directly into one null-terminated data object for automatic
expressions, file-scope pointer initializers, and persistent REPL declarations.
Let the joined value use the remaining 8 MiB private data section and report
data exhaustion instead of publishing a partial string.

## Evidence

The Browser source contracts require every positive script, each malformed
form, all 26 PASS fields, and the recovery lookup. They pin the shared scanner,
primitive conversion, equality, UTF-16 relation, remainder, saved assignment
reference, scope ownership, checked stack pushes, pool-backed concatenation,
and finite and non-finite formatting paths. The focused Browser module passes
20 tests.

The compound checks call side-effecting member receivers and computed keys for
both `+=` and `%=`. Their right sides replace the receiver, and their keys
advance, proving that the store keeps the original reference. Parsed scripts
also run with a deliberately full string pool. They require the target value,
array length, property count, property-list head, right-side call count, and
value-stack depth to remain unchanged before successful recovery operations.

The same full-pool run covers lexer identifiers, computed keys, `typeof`, DOM
strings, global installation, and array properties. Failed interning publishes
no invalid offset, and failed global installation blocks queued scripts. Array
checks grow length through index two, preserve 4,294,967,295 as a property,
reject both string and numeric indices outside the signed length lane, and
reject direct `length` assignment. A native function passes into and back out
of a user function before it is called successfully. Finite formatting checks
4,294,967,295, `1e20`, and `1e-7` without an out-of-range integer cast.

A nested-call check keeps a caller's late declaration in the root scope while
the callee's parameter and local stay hidden there. A closure keeps its inner
shadow after the outer binding changes. A 1,100-write loop proves that ordinary
assignment statements leave the value stack balanced. Separate full and
near-full stack cases reject assignment copying, a binary right operand, a
call argument, and a variable initializer with the exact overflow diagnostic.
The failed call never enters its body, every case returns to its original
depth, and a final script completes all four operations normally.

Private i386 contracts join two 700-byte tokens in an automatic expression, a
file-scope initializer, and a persistent REPL declaration. Each checks the
complete 1,400 bytes and terminator. Negative contracts reject one 1,024-byte
token and a joined value that crosses the data-section limit. The private ABI
module passes 90 tests, its complete discovery passes 105, and all 101 GUI and
frontier harness contracts pass. Checked-seed CupidC rebuilds the private lexer
and parser, while CupidObj rebuilds the four changed Browser source wrappers.

The normal image, four-vCPU e1000 guest run, and regenerated active-source
audit pass. Exact artifact sizes, hashes, durations, and the retained failed
approaches are recorded in `docs/bootstrap/LOG.md`.

## Rejected alternatives

Keeping decimal-only tokenization was rejected because radix literals and
separators are ordinary JavaScript source forms used by real pages.

Skipping an invalid digit, separator, or suffix was rejected. It can turn one
bad token into a different valid program and makes recovery impossible to
reason about.

Keeping prefix-only string conversion was rejected because text such as
`12x` must not become the number 12.

Computing remainder through a 32-bit quotient was rejected after `1e20 % 3`
exposed the range loss. The Browser already binds `fmod`, which also keeps the
required signed-zero and non-finite behavior.

Increasing the 512-byte concatenation buffer was rejected because another
fixed stack size would still truncate a valid pair later. The string pool is
the runtime's actual storage boundary, so the concatenation path now checks
that capacity directly.

Spelling unary minus as subtraction from positive zero was rejected because it
does not preserve every negative-zero case.

Increasing one compiler token buffer until the active script fit was rejected.
It would leave adjacent literals capped by an unrelated parser scratch buffer
and would spend the larger allocation on every token. Streaming bounded tokens
into the existing data section follows C concatenation without that fixed
joined limit.

Splitting or shortening the Browser self-test was rejected. The larger script
is valid source and exposed the compiler defect that should be fixed.

## Consequences

The Browser now handles a larger, internally consistent primitive-number slice
without integer-scaled comparisons or 32-bit remainder shortcuts. Its
asset-free self-test proves positive behavior, useful failures, and recovery in
one guest command. Private JIT and AOT programs can use adjacent ordinary
strings up to the available data-section capacity without silent truncation.

This is not a complete ECMAScript implementation. Object-to-primitive
conversion, BigInt, legacy octal syntax, signed radix strings in primitive
conversion, shortest decimal formatting, and full coercion remain open. The
Browser's string pool is still a fixed 64 KiB page resource, but exhaustion is
now explicit. Direct array-length updates and the canonical indices that do
not fit the signed runtime length lane are rejected instead of partly
implemented. Private wide strings, decoded tokens over 1,023 bytes, literal
pooling, and joined values larger than the data section also remain open.

Neither change moves a build owner or removes a host dependency. `TempleOS/`
remains untouched reference material.
