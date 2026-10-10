# ADR 0159: Normalize GNU named assembly operands

## Status

Accepted on 2026-07-28.

## Context

GNU extended assembly lets an output or input carry a source label and lets
the template refer to that operand by name:

```c
__asm__ volatile(
    "fldl %[input]\n\t"
    "fsin\n\t"
    "fstpl %[output]"
    : [output] "=m" (result)
    : [input] "m" (value));
```

CupidC already represented the equivalent `%1` and `%0` template. Its public
frontend assembly record stored the decoded template and a packed operand
slice, while Linear IR and the i386 emitter validated that canonical numeric
form independently. Unchanged `kernel/cpu/libm.c` uses named memory operands
in `libm_pow_impl`. The full-source probe stopped when it reached the first
`[out]` label at line 782.

Changing the active source to numeric operands would hide a real GNU C
requirement. Adding operand names to every later compiler structure would
duplicate source spelling that no represented emitter path needs.

## Decision

Compiler-head CupidC accepts an optional `[identifier]` before each GNU
extended-assembly output or input constraint. The identifier follows the
same ASCII C identifier spelling as the frontend lexer. Labels share one
output-then-input namespace, must be unique, and may remain unused.

Before ordinary operand parsing, a read-only collection pass walks both
operand clauses and records one parser-private name slot per operand. It does
not advance the parser or publish a partially parsed assembly statement.
The collection pass and the consuming parser call the same cursor-based label
routine, so their token checks and diagnostics cannot drift.
After that namespace is complete, a normalization pass rewrites each true
`%[identifier]` reference to `%N`, where `N` is the operand's packed numeric
index. A doubled percent pair remains unchanged, so `%%[name]` is literal and
does not require a matching operand.

The ordinary output and input parsers then consume the original operand
clauses and receive the canonical numeric template. Existing validation
therefore applies without a second named path. Output modifiability,
addressability, atomic and bit-field exclusions, value width and type,
constraints, fixed-register collisions, matching inputs, and exact template
selection all keep the same rules for numeric and named source.

Malformed, unterminated, duplicate, and unknown names produce statement
diagnostics and leave no published assembly or operand records. A constraint
such as `"[output]"` is recognized as a named matching constraint and rejected
as unsupported. Numeric matching constraints keep their existing behavior.

## Evidence

The frontend contract covers named RDTSC and RDRAND outputs, an unused label,
the escaped-percent case, and exact named variants of pointer output and x87
sine memory assembly. Negative cases cover empty, malformed, numeric,
unterminated, duplicate, and unknown labels or references. They also repeat
the existing non-lvalue, register-object, atomic, bit-field, wrong-width,
unsupported-constraint, fixed-register collision, and matching-constraint
boundaries with named operands. Rollback and same-job recovery remain part of
the selector.

The Linear IR x87 selector contains numeric and named call-produced and
parameter operands. It requires five assembly records, ten operands, and five
functions while checking that both source spellings publish the same numeric
templates and preserve output-before-input evaluation.

The object selector emits numeric and named local and indirect x87 sine
functions. Each named function is byte-identical to its numeric counterpart.
The deterministic object is 584 bytes with 140 text bytes, five sections,
five symbols, and no relocations.

The unchanged-source `libm.c` probe now gets through the labels that formerly
stopped it at line 782. Its next diagnostic is:

```text
/kernel/cpu/libm.c:764:5: error CTB00000F: GNU inline assembly m input template is outside this slice
```

The location is the start of the `libm_pow_impl` assembly statement. That
statement's broader x87 template remains outside the represented emitter set,
so no object is published.

The hosted source lock for `toolchain/cupidc_frontend.cc` is 392 definitions,
15,763 statements, 103,683 expressions, 2,359 block bindings, and 1,455
initializers. Its deterministic self-host object lock is 392 functions,
800,159 text bytes, 946,436 object bytes, and text fingerprint `B36A23AC`.

The complete frontend module passes all 84 tests in 18.008 seconds, and the
complete Linear IR module passes all 72 tests in 18.641 seconds. The exact
x87 object, unchanged `libm.c`, and active self-host frontier selectors pass
together in 33.716 seconds. The strict hosted Toolchain build completes with
the self-host link gate intact.

Before the review refactor, an isolated stage-two to stage-three run reached
the full static fixed point in 609.183 seconds. It compared 19 C objects, the
startup object, and five tool images across the two Cupid-built generations.
The shared cursor routine changes the exact compiler object after that run.
The final source passes the active self-host frontier and strict self-host
link gates; the complete fixed point was not repeated for that code-only
cleanup. Earlier runs that ended without a unit-test summary were discarded
rather than counted as evidence.

The regenerated build graph still contains 698 active sources, 253 feature
IDs, 504 transforms, and 42 accounted unreachable files. Its active-source
digest is
`9380c351fe18d3a7aaa09b857efb0ad92565c8a395d60a12aeb679327ca2c5af`.
The 1,526,996-byte JSON has SHA-256
`8e0ff3bae274cdcc46d486e68dbcf71802607163f933dabb91b1b83b964484f8`.

## Rejected alternatives

Publishing source operand labels in frontend, Linear IR, or emitter records
was rejected. The labels identify existing packed operands and add no
downstream semantics after normalization.

Validating the original named template through a parallel path was rejected.
It could drift from the exact numeric template checks and accidentally admit
an operand type, constraint, or register combination that numeric source
rejects.

Treating every `%[` sequence as a reference was rejected. GNU templates use
`%%` for a literal register percent, and the pair must remain opaque to named
substitution.

Rewriting `libm.c` to numeric operands was rejected because the compiler
should accept the active GNU source rather than narrow it around a frontend
limitation.

Resolving named matching constraints in this increment was rejected. Tying an
input constraint to an output name needs its own constraint rule and has no
active requirement in this source frontier.

## Consequences

Compiler head now accepts the GNU named operand syntax used by
`libm_pow_impl`, while all public assembly metadata remains canonical and
numeric. The next unchanged-source blocker is the broader memory-input
template in that statement, not its operand labels.

The checked seed predates this capability. `kernel/cpu/libm.c` remains
host-owned with its `.c` suffix, and no normal recipe, host dependency, ABI,
kernel image, or runtime behavior changes in this increment. `TempleOS/`
remains untouched reference material.
