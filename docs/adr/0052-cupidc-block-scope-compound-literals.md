# Lower block-scope compound literals in hosted CupidC

## Context

CupidC already parses automatic initializer lists, lowers supported scalar and structure values, and assigns target-sized frame storage to named automatic objects. It did not recognize a compound literal, even when its type and initializer were already inside those supported boundaries.

The unchanged preprocessor helper in `toolchain/cupidc_pp.c` contains the active requirement:

```c
return pp_string_equal(value, (ctool_string_t){literal, size});
```

Changing this call to declare a temporary first would make valid C source accommodate a parser gap. C11 gives the expression a stronger identity than a disposable temporary. At block scope, it names one unnamed automatic object for that source site. Its initializer runs whenever execution reaches the expression, and the result is an lvalue.

## Decision

The shared frontend publishes `CTOOL_C_EXPRESSION_COMPOUND_LITERAL`. Its `reference` owns the root of the existing initializer forest, while the expression's absolute index identifies the unnamed object. This keeps initializer ownership separate from the declaration tables and avoids a synthetic block binding that would disturb a declaration statement's contiguous binding slice.

The parser accepts block-scope scalar, complete array, and complete structure literals through the existing automatic-initializer rules. An array with an omitted bound is completed from its initializer without changing a shared incomplete typedef. The resulting lvalue continues through ordinary postfix parsing, so member access, subscripting, calls, and address-taking use the same expression machinery as named objects.

Initializer expressions are published before their owning compound-literal expression. Freeze validates that postorder relationship and gives every initializer root exactly one owner. Unevaluated layout queries rewind any speculative expressions, initializer roots, and initializer edges together. A successful `sizeof((pair_t){...})` therefore leaves no hidden object or initializer records behind. The same rule permits a non-VLA `sizeof` operand inside a static constant initializer because no compound-literal object or initializer is evaluated or retained there.

Linear IR adds `CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS`. Its `reference` is the absolute frontend expression index. Scalar and whole-structure expression roots evaluate before they store, then leave the same address as the lvalue result.

An aggregate list uses a second, target-private address named by `CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS`. `ZERO_OBJECT`, checked member or element paths, and source-ordered explicit stores build the complete value there. Only then does `COPY_OBJECT` replace the persistent compound-literal object. This ordering matters when an initializer reads through a pointer that still names the object from an earlier evaluation. Those reads must finish before any byte of the new value becomes visible.

The i386 emitter maps each referenced expression index to one target-sized EBP-relative object slot. Aggregate list roots also receive a separate staging slot of the same size and alignment. Repeated evaluation reuses both slots and runs the initializer again, while recursion receives fresh slots in the new frame. Named automatic objects come first, followed by persistent compound-literal objects, aggregate staging objects, and instruction-owned structure snapshots. All offsets remain private to target emission.

The hosted path supports represented scalars, fixed arrays, and structures with power-of-two alignment no greater than four bytes. It keeps the established limits for unions, Cupid classes, variable-length or incomplete objects, stored `volatile` or `_Atomic` subobjects, explicit bit-field leaves, direct wide or floating scalar values, and over-aligned objects. At this decision boundary, string-initialized array literals parsed but stopped at a focused IR diagnostic. ADR 0053 later closes that runtime string case. A variable-length type name stops earlier at the frontend's existing integer-constant-expression boundary. File-scope and other static-duration compound literals remain outside this decision.

The public operations reject malformed roots, duplicate ownership, forward initializer-expression references, nonempty fields unused by the expression kind, invalid layouts, frame overflow, and insufficient output storage. Nested compound initializers carry the public IR recursion budget instead of restarting it at each unnamed object. Each failure is transactional: it publishes no partial unit, IR, or object, preserves its immutable input, rewinds job storage, and permits a same-job retry.

## Consequences and evidence

The frontend contract pins the exact active preprocessor call. It also covers scalar literals, inferred array bounds, member reads and writes, address-taking across a loop, postfix use, ownership, rollback, a block-scope unevaluated `sizeof`, and a static integer initialized from such a `sizeof`. Useful negatives cover non-object and incomplete types, variable-length type names, evaluated static initializers, string lowering, and malformed ownership.

The IR proof checks one stable expression identity per source site, initialization on a backward `goto`, scalar initialization, final lvalue load and address behavior, exact stack depths, repeat lowering, and input-unit immutability. Its fixed-array case checks two staged `ELEMENT_ADDRESS` stores, one complete commit, and a final load through the persistent array. Its two-member alias case places a read of the prior object between the two staged stores and commits only after both. Mutated roots and expression order fail without leaving output behind.

The object proof emits the unchanged `pp_string_equal` call shape and a focused fixed-array function. A decoder distinguishes the persistent eight-byte structure from its staging storage. It checks full staging zeroing, two staged member stores, one complete commit, a later persistent-object read, structure copies, and sixteen-byte call alignment. The array decoder independently checks full zeroing of an eight-byte staging slot, two staged element stores, one commit, and a later persistent load. The object carries one `R_386_PC32` relocation to `pp_string_equal` with addend `-4`. Repeated emission is byte-identical.

The first aggregate lowering wrote directly into the persistent object. A focused backward-`goto` contract showed the bug: zeroing happened before an initializer read through an escaped pointer, so the read could observe the new zero instead of the prior object. The staging instruction and full-object commit are the correction. This follows the C11 source-site lifetime model in [WG14 N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) and the read-before-replacement compound-literal example in [WG14 N716](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n716.htm).

Named automatic aggregate declarations still use the older direct zero-and-store path. If control jumps backward over such a declaration and its initializer reads the prior object through an escaped pointer, that named-object case can expose the same aliasing problem. The wider declaration case remains separate work under issue #25 and needs its own staging policy.

This is hosted bootstrap evidence. GCC or Clang still builds the shared compiler and its contracts, and the host compiler still produces normal Cupid OS C objects. This changes no production ownership, boot path, runtime artifact, or host dependency. Issue #25 remains open for static-duration and variable-length compound literals, broader C semantics, production integration, staged self-hosting, and the fixed-point bootstrap. `TempleOS/` remains reference-only and untouched.

## Extension: narrow string roots

ADR 0053 lowers a `STRING` root for a block-scope character-array compound literal. The persistent object is zeroed and receives the exact retained bytes through `COPY_STRING`. No staging slot is needed because the initializer source is immutable literal storage and cannot read through an escaped pointer to the prior object. The expression still returns the same persistent address and retains the lifetime rules in this decision.
