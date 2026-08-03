# ADR 0219: Preserve private record identity and checked layout

## Status

Accepted on 2026-08-03.

## Context

The Browser assignment fix uses an ordinary named record:

```c
typedef struct js_target_ref {
    int kind;
    int binding;
} js_target_ref_t;
```

Private CupidC already parsed standalone tagged structures and anonymous
`typedef struct { ... } Alias` declarations. It did not parse a body after the
tag inside `typedef struct Tag { ... } Alias`. The type parser returned after
`Tag`, so the typedef parser read `{` where it expected the alias name.

There was a second gap behind that syntax error. The typedef table retained the
coarse `TYPE_STRUCT` or `TYPE_STRUCT_PTR` category but not the structure-table
index. An alias could therefore lose the layout needed by `.` and `->`, even
when an anonymous declaration had parsed successfully.

Reviewing the shared body parser exposed arithmetic that had been safe only for
small records. A field array could have a zero or negative count, its
count-by-stride product could wrap, and cumulative field size or final record
alignment could cross the signed parser range. The same unchecked arithmetic
appeared in global, REPL, and local allocations. Several large locals could
also wrap the negative frame offset back toward zero.

The constant-expression path caused some of those failures earlier. Signed
addition, subtraction, multiplication, division, and unary negation used host
arithmetic before checking the result. Integer literals accumulated into a
signed value, so the supported `uint32_t` range and an explicit `u` suffix were
not carried into enum expressions. Empty hexadecimal literals and a suffix at
the 95-character token boundary could also split into misleading follow-on
errors.

Persistent REPL rollback restored only the number of known structures. A
failed line could complete an existing forward tag in place, leaving fields
from rejected source visible to the next line.

The first complete guest run found one more record bug. The Browser passes
`&ref->key_off` and `&ref->key_len` to its computed-key helper. Private CupidC
formed those expressions from the address of the pointer parameter and then
loaded the field value. The helper wrote outside the saved reference, its key
fields stayed at `-1` and zero, and the later property store wedged the guest.

## Decision

Let the private type parser consume a structure body when `{` follows a named
tag. Anonymous and tagged typedef declarations use one field parser. That
parser owns field names, fixed array counts, alignment, offsets, completion,
the existing SIMD-array restriction, and the diagnostic for an incomplete
structure stored by value. A self-referential pointer remains valid because it
does not require the record to be complete while its fields are being read.

Store the structure-table index beside every private typedef entry. Restore it
whenever an alias is parsed, including aliases of aliases and pointer aliases.
The normal file parser and persistent REPL record the same metadata.

Keep the address operation active through a member designator. For
`&record.field`, start with the record object's address. For
`&pointer->field`, load the pointed-to record instead of taking the address of
the pointer slot. Add the declared field offset and leave that address in EAX;
do not perform the scalar field load used by an ordinary member expression.

Require every fixed array count and stride to be positive. Check their product
against `0x7ffffffc` before storing it. Lay out every record field with checked
padding and addition, keeping the unrounded record size at or below
`0x7ffffffc` so the compiler's final four-byte allocation alignment cannot
overflow. Tagged structures, anonymous structures, standalone structures, and
classes use the same checks and diagnostics.

Reserve automatic storage through one cumulative frame helper. It aligns the
next object, caps the frame at `0x7ffffff0` so the final 16-byte prologue
alignment remains safe, updates the deepest offset once, and preserves a
zero-sized empty class. Arrays, records, SIMD values, scalars, multi-declarator
statements, and local function pointers all use this helper. Global and REPL
record or enum declarations check capacity before bytes and addresses are
committed. A failed REPL declaration does not escape its transaction.

Represent a private integer constant as a 32-bit value plus an unsigned flag.
Signed `+`, `-`, `*`, `/`, and unary `-` reject overflow before performing the
operation; division by zero has its own diagnostic. If either operand is
unsigned, the represented operation wraps modulo `2^32`. Enum symbols retain
that flag. `INT_MAX` is accepted as the last explicit enumerator, while an
implicit successor fails.

Accumulate decimal and hexadecimal integer literals in `uint32_t`, reject a
value above `UINT32_MAX`, and retain an explicit `u` or `U`. Require at least
one hexadecimal digit. The suffix counts toward the 95-character token limit
and is consumed even when it crosses that limit, so the lexer reports one
focused length error instead of exposing the suffix as another token.

Checkpoint every committed structure record in persistent REPL state. Restore
the records as well as the table count after parse, patch, capacity, or JIT
setup failure. Completing a pre-existing forward tag is therefore part of the
line transaction.

## Evidence

The private compiler tests execute tagged and anonymous aliases, an alias of a
tagged alias, a structure-pointer alias, access through both the tag and alias,
and a self-referential pointer field. A separate REPL check declares the tagged
alias in one unit and allocates it in the next. Another commits `struct Node;`,
rejects a body with no typedef alias, restores the incomplete tag, then accepts
a clean definition and allocation. Negative checks require a name after a
complete typedef body and reject an incomplete by-value field with the specific
structure diagnostic.

Layout contracts cover nonpositive counts, overflowing products, cumulative
field growth, final alignment, data-section exhaustion, and cumulative local
frames across arrays, records, scalars, and SIMD alignment. The same invalid
record shapes are tested through tagged, anonymous, standalone, class, local,
and REPL source. Diagnostics stay focused and do not publish a successful
definition after failure.

Constant-expression contracts reject overflow in each signed arithmetic
operator and division by zero across record fields, globals, locals, and REPL
source. Positive cases retain `UINT32_MAX`, unsigned wraparound, and unsigned
enum symbols. Decimal and hexadecimal literals above `UINT32_MAX`, empty hex
bodies, and 96-character tokens fail; 95-character tokens with their suffix
remain valid. Recovery compiles a valid declaration in the same process.

Member-address execution writes two fields through `&record.field`, repeats
the writes through `&pointer->field`, and checks the fields on either side for
damage. A negative contract names a missing pointer member and requires
`unknown struct field`. The test first failed with an i386 segmentation fault,
then passed after the compiler kept the selected address.

The private ABI module passes 90 tests, and complete private CupidC discovery
passes 105. Checked-seed CupidC rebuilds both private compiler objects. Exact
artifact and integration evidence is recorded in `docs/bootstrap/LOG.md`.

The Browser guest test is the active-source proof. Its tagged reference record
must compile, and its member addresses must point into that record, before the
JavaScript self-test can run through saved member and index references.

## Rejected alternatives

Changing the Browser record to an anonymous typedef was rejected. That spelling
would avoid the failing token without adding the ordinary tagged form to
CupidC.

Using `struct js_target_ref` at every Browser use site was also rejected. It
would make the source less natural solely to fit the current compiler.

Keeping separate anonymous and tagged field parsers was rejected because their
layout checks had already drifted. One parser now gives both forms the same
offsets and errors.

Letting signed host arithmetic wrap and checking only the final allocation was
rejected. A wrapped count or offset can look small again, and the compiler can
then reserve the wrong object before it notices anything unusual.

Restoring only `struct_count` after a failed REPL line was rejected because a
body can modify an older forward declaration without changing that count.

Changing the Browser helper to return a packed key or copying through two
unrelated scalar locals was rejected. Both would conceal an ordinary C member
address that active source already needs.

## Consequences

Private JIT, AOT, and persistent REPL source can use named or anonymous
structure typedefs, alias chains, and structure-pointer aliases without losing
member layout. Direct field addresses work through record objects and
structure pointers. Standalone tagged definitions keep their established
path. Record, array, enum, and frame arithmetic now fails before signed
overflow or a partial REPL declaration can escape.

The private typedef table still has sixteen entries and accepts one simple
alias declarator per declaration. Array typedef declarators, function-pointer
typedef declarators, multiple aliases in one declaration, and broader C tag
scope rules remain outside this change. No build owner moves, no host
dependency is removed, and `TempleOS/` remains untouched reference material.
