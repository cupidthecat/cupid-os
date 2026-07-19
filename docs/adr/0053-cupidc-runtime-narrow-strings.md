# Lower runtime narrow strings in hosted CupidC

## Context

The shared frontend already retained narrow string bytes in two forms. A string expression used at runtime was a typed `char[N]` expression, while a character-array initializer was a semantic `STRING` record. Static object emission consumed the initializer form, but hosted Linear IR rejected both forms inside function bodies.

That boundary blocked ordinary active source such as this unchanged declaration in `drivers/serial.c`:

```c
const char hex[] = "0123456789abcdef";
```

The same gap affected string arguments and pointer initializers such as `const char *value = "Cupid"`, nested character arrays in automatic structures, and `(char[]){"Cupid"}`. Replacing these expressions with integer stores or handwritten temporaries would make valid C source accommodate the compiler.

## Decision

Linear IR now has two string instructions. `STRING_LITERAL_ADDRESS` uses an absolute frontend expression index and pushes the address of immutable literal bytes. `COPY_STRING` uses an absolute semantic initializer index, consumes one character-array address, and copies the exact bytes retained by the frontend.

The lowerer accepts complete fixed arrays of plain, signed, or unsigned character elements with one-byte target representation. A runtime string expression must contain exactly as many retained bytes as its completed array type. A string initializer may contain fewer bytes than its destination array, which is the normal case when an explicit bound leaves trailing elements.

A named automatic character array starts with `LOCAL_ADDRESS` and `ZERO_OBJECT`, then rebuilds the same address path and emits `COPY_STRING`. A nested string leaf uses the member and element path already established for initializer lists. The enclosing object was zeroed before any explicit leaf ran, so bytes beyond the retained string stay zero. A string-initialized compound literal follows the same rule with its persistent `COMPOUND_LITERAL_ADDRESS`. It does not need aggregate staging because its immutable source bytes cannot observe or alias the prior automatic object.

A runtime string expression does not allocate a frame object. The i386 emitter appends its bytes to `.rodata`, assigns a deterministic local `.LCn` object symbol, and emits an `R_386_32` text relocation with addend zero. `COPY_STRING` places the semantic initializer bytes in the same section, loads their symbol address, and uses the existing checked byte-copy helper. That helper preserves ESI and EDI, issues `CLD`, and copies the retained length with `REP MOVSB`.

The emitter allocates symbol capacity from file bindings, semantic initializers, runtime expressions, and block-static objects. Text-relocation capacity counts each literal address and string copy directly. Static data keeps the earlier initializer encoder, while runtime strings use the two new IR instructions. Literal pooling is not part of this decision, so each runtime use receives its own deterministic symbol.

Malformed expression and initializer payloads fail at the IR boundary. The checks cover missing bytes, inconsistent character-array layouts, unused payload fields, a runtime expression whose byte count differs from its type, and an initializer whose byte count exceeds its destination. Failure publishes no partial IR or object, preserves the frozen translation unit, rewinds job storage, and leaves the caller's output empty.

This decision covers narrow runtime literals, automatic character-array initialization, nested string leaves, pointer initialization and arguments through normal array decay, and string-initialized block-scope compound literals. Wide string literals, static-duration compound literals, variable-length objects, and the existing volatile, atomic, union, Cupid class, over-alignment, wide scalar, floating, and explicit bit-field boundaries remain outside the hosted path.

## Consequences and evidence

The active-source guard reads the unchanged hexadecimal array in `drivers/serial.c`. The simple automatic-array IR fixture now publishes `LOCAL_ADDRESS`, `ZERO_OBJECT`, another `LOCAL_ADDRESS`, and `COPY_STRING` before its return. The compound-literal contract separately checks three persistent object addresses, one zero, one exact string copy, array decay, and return. A second function checks one `STRING_LITERAL_ADDRESS`, array decay, and return.

The deterministic ELF32 proof contains four functions, four local literal symbols, nine symbols in total, four `R_386_32` text relocations, and 19 exact `.rodata` bytes. Its decoder checks these operations:

- an eight-byte record is zeroed, receives a four-byte nested `"abc"` copy, and retains its scalar leaf;
- a six-byte automatic array is zeroed, receives a three-byte `"hi"` copy, and keeps three implicit zero bytes;
- a normal `"Cupid"` expression produces a literal address without a frame copy;
- a six-byte string compound literal is zeroed and receives a six-byte copy.

The IR and object contracts mutate both retained-byte forms. An oversized initializer and an undersized runtime expression each return the invalid-unit diagnostic transactionally. The first complete strict hosted run then found stale self-source inventory values, not a compiler failure. Updating those exact measured tuples made the complete Windows Clang, WSL GCC, and WSL Clang suites pass.

Both full hosted suites pass under GCC and Clang address and undefined-behavior sanitizers with leak detection and halt-on-error enabled. GCC `-fanalyzer` and Clang static analysis report no diagnostics for the lowerer, emitter, or the two changed runtime contracts. The focused Python IR and object run passes all 42 tests.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler and normal Cupid OS C objects, so this increment transfers no production object and retires no host dependency. The normal image and private in-kernel compiler keep their existing owners. `TempleOS/` remains read-only reference material and is not part of the evidence.

Issue #25 remains open for the other C11 and i386 ABI gaps, production integration, staged self-hosting, and the fixed-point bootstrap.
