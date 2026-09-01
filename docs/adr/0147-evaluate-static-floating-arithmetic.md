# ADR 0147: Evaluate static floating arithmetic with target semantics

## Status

Accepted on 2026-07-27.

## Context

CupidC already decoded decimal `float` and `double` literals into exact IEEE
bits. It could place those literal bits in static objects, but it rejected
ordinary constant arithmetic such as:

```c
static const fixed_t fixed = (fixed_t)(-.867 * 65536);
static const double ratio = 7.0 / 2.0;
```

That gap stopped the unchanged Doom automap source. Delegating the work to
the host compiler or its floating-point library would make bootstrap output
depend on host arithmetic, evaluation width, and rounding behavior. Rewriting
the tables would move a compiler limitation into the operating-system source.

The existing integer constant-expression parser also covers declaration forms
that are outside the first typed scalar evaluator. Replacing it wholesale
would trade one real capability gap for regressions in previously accepted
source.

## Decision

CupidC evaluates static scalar expressions with an integer-only IEEE binary32
and binary64 engine. It decodes signs, significands, and exponents, carries
guard and sticky information, and rounds to nearest with ties to even after
each operation at the expression's C type. It never performs a host
floating-point operation and does not call a host math library.

The evaluator covers unary plus and minus; addition, subtraction,
multiplication, and division; all six comparisons; casts and assignment
conversion; logical not, short-circuit `&&` and `||`; and conditional
selection. It implements C scalar truth for both zeros, finite nonzero values,
infinities, and NaNs. Invalid special arithmetic produces a canonical quiet
NaN. Overflow produces infinity, and gradual underflow produces rounded
subnormal results.

Static conversions cover represented signed and unsigned integers through
64 bits. Integer-to-floating conversion rounds at the destination width.
Floating-to-integer conversion truncates toward zero and rejects a result
outside the destination range. Mixed integer and floating arithmetic and
conditional arms use the frontend's usual arithmetic type rules. File-scope
and block-scope enumerator constants keep their selected target integer type
and can feed the same arithmetic, comparison, truth, and conditional paths.
These wider conversion exceptions apply only while parsing a static
initializer; runtime Linear IR and object emission keep their existing
boundaries.

The parser routes an initializer through the scalar evaluator when its target
is `float` or `double`, or when its token range contains a floating literal,
the built-in `float` or `double` spelling, or a visible typedef for one of
those types. Other integer initializers continue through the established
integer constant-expression parser.

Evaluation uses transactional scratch AST storage and rewinds it before
publishing the final initializer. A public depth counter rejects more than
256 nested scalar-evaluator calls with
`static scalar expression exceeds the public nesting limit`. Failure restores
the parser state and allows a later declaration in the same job to succeed.

## Evidence

Frontend, Linear IR, and object contracts cover exact binary32 and binary64
results for ordinary arithmetic, halfway rounding, subnormal and overflow
edges, infinities, NaNs, signed zero, comparisons, truth, short-circuiting,
selected conditional arms, file and block enumerators, 32-bit and 64-bit
conversions, mixed wide arithmetic, typedef casts, useful range failures,
rollback, and recovery.
The object contract pins the complete `.rodata`, `.data`, and `.bss` bytes and
the local symbol layout.

A frozen source gate reports 360 definitions, 14,794 statements, 96,718
expressions, 2,219 block bindings, and 1,389 initializers for
`toolchain/cupidc_frontend.cc`. CupidC emits that source as a deterministic
885,004-byte object with 750,826 text bytes, 360 functions, and text
fingerprint `BBA68532`.

A differential oracle checked 500,000 deterministic operand pairs. Four
million floating arithmetic results, six million comparisons, and four
million signed or unsigned integer conversions matched Clang's SSE behavior
bit for bit. NaN results were compared by classification because C does not
fix a payload for invalid arithmetic.

The exact Doom-tree profile now emits 73 of 80 objects. The unchanged
`kernel/doom/src/am_map.c` is the new success.

The complete 73-case frontend, 61-case Linear IR, and 76-case object modules
pass. A full WSL stage-two to stage-three fixed point also matches all five
linked tools and all 19 closure objects.

## Rejected alternatives

Host floating arithmetic was rejected because it would make target object
bytes depend on the build machine.

Precomputing or rewriting Doom tables was rejected because the compiler, not
the operating-system source, lacked the required C semantics.

Routing every integer initializer through the first scalar evaluator was
tested and rejected. It regressed 20 Doom roots whose established integer
constant forms were not part of this slice. Typed routing keeps those forms on
the proven parser while floating expressions use the new evaluator.

An unbounded recursive evaluator was rejected after a 32,768-leaf macro
expression caused a native stack overflow. The explicit limit turns that case
into a deterministic source diagnostic.

## Consequences

Compiler head can represent Doom's static fixed-point table and a broader set
of ordinary C constant expressions without a host floating dependency.
Runtime floating truth, runtime mixed wide and floating arithmetic, runtime
mixed integer and floating conditional arms, hexadecimal floating literals,
`long double`, floating increment and decrement, SIMD values, floating
atomics, and over-aligned floating objects remain separate work.

The checked bootstrap seed does not yet contain this evaluator. No production
recipe changes owner, no `.c` source is renamed, and no host tool is retired
by this decision. The remaining seven Doom failures still need an implicit
declaration, IR coverage, positional union initialization, and pointer or
callback conversions before the cohort can move.
