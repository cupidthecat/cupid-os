# Compile the hermetic Toolchain source frontier with hosted CupidC

- Status: Accepted
- Date: 2026-07-22

## Context

Issue #27 starts the compiler-on-compiler bootstrap with a fourteen-file CupidC, CupidASM, and CupidDis cohort. Eleven of those files have no external hosted headers or runtime calls once they enter the existing `HOSTED_TOOLCHAIN_64` preprocessing profile. The same profile also contains the CupidLD and CupidObj cores. CupidC could parse nine of these implementation files, but several ordinary C forms still stopped IR lowering or object emission.

The blockers came from unchanged Toolchain source. Structures contain nested unions. Code reads scalar members from returned structures. Callback code casts a literal zero to a function pointer. Host-facing helpers convert object pointers to and from eight-byte integer types. `x86.c` initializes static name tables through a macro that parenthesizes a string literal.

Changing those sources to avoid the language forms would hide the requirements from the compiler and make the shared code harder to maintain.

## Decision

Hosted CupidC accepts a union inside a supported structure value. The structure snapshot copies every target byte, including union storage. A union used directly as a parameter or result remains outside the represented cdecl value ABI.

A scalar member can be read from an owned structure-value snapshot. Linear IR lets `MEMBER_ADDRESS` consume that snapshot, then uses the ordinary typed `LOAD`. An aggregate member selected from a structure rvalue remains unsupported because the current stack model cannot keep the parent snapshot alive while publishing another aggregate value.

An explicit cast of a direct four-byte integer literal zero to a represented function-pointer type records `NULL_POINTER` conversion in Linear IR. Computed zero, nonzero integers, wide integer literals, and conversions between function pointers and eight-byte integers remain rejected. This keeps the accepted rule narrow enough to preserve useful diagnostics.

Explicit casts between a represented object pointer and a signed or unsigned eight-byte integer use the existing wide snapshot path. Pointer widening copies the low i386 word and writes a zero high word. Converting back keeps the low word. These are object-pointer conversions only. Function pointers do not enter this path.

The static initializer parser recognizes an ordinary string literal through array-to-pointer decay, pointer qualification, and compatible implicit pointer conversion even when parentheses or a macro preserve those expression nodes. It publishes the existing string-address initializer. Arithmetic on the address and explicit casts remain outside this boundary.

The exact hosted source gate now covers all twelve hermetic `HOSTED_TOOLCHAIN_64` implementation files:

- `ctool.c`
- `cupidasm.c`
- `cupidc_emit.c`
- `cupidc_frontend.c`
- `cupidc_ir.c`
- `cupidc_pp.c`
- `cupidc_type.c`
- `cupiddis.c`
- `cupidld.c`
- `cupidobj.c`
- `elf32.c`
- `x86.c`

The object gate compiles those files plus `kernel/lang/as_elf.c` twice. It reads every result through Cupid's ELF32 reader and requires byte-identical output. The Toolchain files use the one-root `HOSTED_TOOLCHAIN_64` profile, while the kernel bridge adds `/kernel/lang` as its second root. Both profiles define `__SIZEOF_POINTER__` as eight, but the emitter still produces i386 ELF32 objects. These objects prove preprocessing, parsing, semantic lowering, and deterministic target emission. They are not host-runnable binaries.

## Rejected alternatives

Replacing unions with integer arrays was rejected because the active structures use union layout intentionally. Byte copies already preserve the correct representation.

Rewriting returned-member expressions through a named temporary was rejected because it would move a compiler limitation into normal C source.

Treating every integer zero expression as a function-pointer null was rejected. The current lowering rule can identify a direct literal without inventing constant-expression equivalence in Linear IR.

Sign-extending an object pointer into a signed eight-byte integer was rejected. The source conversion starts from an unsigned target address representation, so the upper word is zero for both signed and unsigned destinations.

Building special static tables for `x86.c` was rejected. Parentheses around an ordinary string literal do not change the C address constant, and the frontend can retain that meaning directly.

## Consequences and evidence

The focused IR fixture has eight functions and 43 instructions with fingerprint `8DAA29582B417001`. The object fixture has 871 text bytes with fingerprint `2CCA0718`, eleven symbols including the null symbol, and two relocations. Its decoder-driven oracle checks nested-union copies, a scalar member read from a returned structure, null and non-null function-pointer comparisons, zero extension in both pointer-to-wide forms, and low-word recovery in both wide-to-pointer forms.

Negative contracts cover a nonzero function-pointer cast, computed zero, both function-pointer and wide-integer directions, a top-level union function ABI, an aggregate member selected from a returned structure, an incompatible parenthesized string target, and string-address arithmetic. Failure remains transactional, and the same job reproduces the successful object afterward.

The active object gate now emits thirteen unchanged implementation files. Eleven belong to issue #27's fourteen-file CupidC, CupidASM, and CupidDis cohort. The remaining three are `ctool_host.c`, `cupidasm_main.c`, and `cupiddis_main.c`; they still depend on external host headers, a hosted runtime, and a host-runnable ABI. The gate does not link tools, execute them on the host, compare staged compiler generations, or transfer a production build object. GCC or Clang still builds the compiler and every normal C object. Issues #25 and #27 remain open.
