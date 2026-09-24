# ADR 0131: Finalize C11 external inline definitions

## Status

Accepted on 2026-07-26.

## Context

The next unchanged strict production root was
`kernel/audio/nuked_opl3.c`. Its header declares `OPL3_Generate4Ch` as an
ordinary external function, while the source later defines it with
`inline`. Under C11, that declaration set provides one external definition.

CupidC kept the `inline` spelling on the definition, but Linear IR rejected
every inline definition with external linkage. Removing `inline` from the
vendored source would have hidden a compiler gap instead of fixing it.

## Decision

Finalize inline meaning after the frontend has parsed the complete
translation unit. The canonical function binding records that the unit
provides an external definition when it has a function body and either of
these conditions holds:

- its compatible file-scope declarations mix inline and non-inline forms
- an inline declaration or definition uses `extern` and the function's
  effective linkage is external

Keep the exact source spelling on the function-definition record. This lets
the canonical binding describe the C11 result without rewriting the
definition's storage class or specifiers.

An earlier `static` declaration keeps internal linkage when a later
definition is spelled `extern inline`. Any external-linkage function declared
`inline` must have a definition in the same translation unit. The frontend
checks that constraint during finalization and points the diagnostic at the
canonical declaration.

A pure external inline definition, where every file-scope declaration uses
`inline` without `extern`, remains unsupported because CupidC does not yet
model a translation unit that supplies only an inline definition.

Linear IR and object emission validate the new canonical marker. It is valid
only on a file-scope function with external linkage, an inline declaration,
and a function body in the same unit. Definition records cannot carry the
canonical-only marker. Both boundaries also reject an external-linkage inline
binding without a body, even when a malformed caller omits the marker.

## Rejected alternatives

Removing `inline` from Nuked OPL3 was rejected because the active source must
drive CupidC's language support.

Treating every external inline definition as a global definition was
rejected because it would silently change the meaning of a pure C11 inline
definition.

Special-casing `OPL3_Generate4Ch` was rejected because the rule belongs to
the declaration model, not one source file.

Transferring the normal recipe now was rejected because the checked seed
predates this compiler capability.

## Evidence

The frontend contract covers an ordinary declaration before an inline
definition, the reverse declaration order, `extern inline`, static inline
functions, inherited internal linkage, and pure external inline definitions.
A block-scope ordinary declaration does not change the file-scope result.
The contract rejects external inline declarations without a definition,
duplicate definitions, and incompatible declaration sets without publishing
a partial unit.

The IR and object contracts keep the canonical result separate from the
definition's exact spelling. External inline metadata without a definition,
and malformed markers on non-inline, internal, or ordinary declaration-only
bindings, fail transactionally. A later valid operation in the same job
reproduces the original deterministic result. The pure external inline form
keeps its focused unsupported diagnostic.

Compiler head compiles the complete unchanged
`kernel/audio/nuked_opl3.c` root twice under the kernel i386 profile. Both
validated ELF32 relocatable objects are 40,424 bytes with SHA-256
`a3a04ade4029d9333902bb93376fb5eef21f349ee5a1406bd0751cc4cee9f2a1`.
CupidDis reports `OPL3_Generate4Ch` as a defined global function at text
offset `0x598C`, with size 4,934 bytes. The object has 36,082 text bytes,
120 text relocations, eight read-only-data relocations, and 52 symbols.
An i386 Clang oracle also publishes `OPL3_Generate4Ch` as a global text
symbol. The two compilers are not expected to produce identical object
bytes.

The complete hosted toolchain suite passes, including the self-host source
frontier, deterministic objects, all five Cupid-built static tools, and the
ELF32, x86, CupidDis, CupidASM, CupidObj, and CupidLD contracts.
`make verify-bootstrap-seed` also passes for the existing five checked tools.

## Consequences

Compiler head is ready to compile the Nuked OPL3 root without editing its
source. The normal recipe still uses the host compiler because the checked
seed cannot compile this declaration set. The source therefore remains
`nuked_opl3.c`.

Ownership transfer and the `.cc` rename wait for a checked seed that carries
this rule, two deterministic production-wrapper compiles, the complete
strict frontier, a clean image, and the relevant runtime gate. This change
moves no build transform and changes no production ABI.
