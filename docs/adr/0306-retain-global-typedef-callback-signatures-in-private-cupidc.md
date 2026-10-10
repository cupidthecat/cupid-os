# ADR 0306: Retain global typedef callback signatures in private CupidC

## Status

Accepted on 2026-08-21.

## Context

ADR 0303 retains a file-scope function-pointer typedef signature when a
free-function parameter uses that alias directly. A file object declared with
the same alias still kept only a four-byte pointer. Its indirect calls could
not recover fixed conversions, record identity, arity, variadic state, or a
SIMD result channel.

Doom has a complete active example. `vpatchclipfunc_t` declares a Boolean
callback over a patch pointer and two coordinates. `patchclip_callback` starts
as `NULL`, `V_SetPatchClipCallback` stores a callback parameter in it, and the
video path invokes the stored value. Treating the file object as an untyped
address would make its declaration disagree with its call ABI.

## Decision

When a private CupidC file object uses a direct function-pointer typedef,
capture the typedef index immediately after parsing its type and copy the
retained signature to the global symbol. Variable loads already publish a
symbol's callback metadata, so indirect calls through the object use the
existing fixed cdecl path.

Allow a callback file object to start with grouped integer zero or the grouped
`(void *)0` spelling produced by the active `NULL` macro. The data slot remains
zero-filled. Reject a function designator in a static initializer with a
focused diagnostic because the private AOT writer has no data-address fixup
for that value.

Check plain assignment to a signature-bearing function pointer before storing
the address. A non-null function designator or callback object must match the
destination result, record-pointer identities, fixed parameter list, and
variadic boundary. Null clears the object. An explicit pointer cast remains
the deliberate signature-erasure path. Compound assignment is rejected.
Provisional signatures inferred from a later target are applied only after the
right-hand expression passes the complete check.

Keep source and REPL transactions around these changes. A rejected initializer
or assignment must restore global symbols, typedef metadata, provisional
function signatures, patches, code, and data before another source uses the
same compiler state.

## Evidence

`python -B -m unittest -q tests.test_private_cupidc_call_abi` passes all 235
tests in 35.950 seconds. JIT and AOT execute a Doom-shaped null global, setter,
indirect Boolean call, and clear operation. A second JIT and AOT case passes a
`float4` value through one 16-byte callback slot and returns the result through
XMM0.

The negative contracts reject a fixed-parameter mismatch and a direct function
designator in static data, then compile and execute valid sources in the same
compiler state. The active-source assertions pin the `vpatchclipfunc_t`
declaration, global object, setter assignment, and indirect call.

`python -B -m unittest -q tests.test_gui_terminal_smoke` passes all 125 tests
in 2.771 seconds. The feature-14 contract now requires
`[feature14-callback-global] PASS float4=4 calls=1 cleared=1` after the existing
typedef-parameter callback marker and rejects its failure marker. A new full
image and guest run were still required at this focused checkpoint. The later
integrated four-vCPU guest frontier printed the global marker once, followed by
the automatic callback marker, and completed the feature run cleanly. Its
148,491-byte log has SHA-256
`b31fcc79c861cbdead01967c1417409f7a8cdf46cc375300a17e64df4beca041`.

`make kernel/lang/cupidc_parse.o kernel/lang/cupidc_elf.o` passes with the
promoted Windows checked seed. `cupidc_parse.o` is 462,552 bytes with SHA-256
`05abc78236517ccc9b3ddd861f85b7670fa104bbe9a14463a96ad5cebc56cb31`.
The unchanged 3,604-byte `cupidc_elf.o` has SHA-256
`c2ad171aacd493a33a477e7a3196a5d28b04b0f74521cd8cbaec2598f391880c`.

## Rejected alternatives

Do not infer the signature from arguments at each indirect call. The arguments
cannot recover the declared result channel or distinguish every fixed type.

Do not accept a direct function designator in static data without a real
data-address fixup. Writing the parser's current address would fail for later
definitions and could disagree between JIT and AOT output.

Do not special-case Doom callback names. The typedef is the ABI authority for
any supported file object.

## Consequences

Private JIT and AOT programs can retain a direct callback typedef across a file
object, a free-function parameter, checked plain assignment, null clearing, and
an indirect call. This covers the callback storage shape used by Doom's patch
clipping path without changing that source.

The typedef table still holds sixteen entries, with at most 32 fixed parameters
per callback signature. Typedef-typed automatic and block-static objects,
method parameters, record and class fields, callback arrays, alias chains,
recursive signatures, aggregate results, and arbitrary computed callback
expressions remain outside this boundary. Direct function-designator global
initialization also remains unsupported until initialized-data fixups exist.
ADR 0310 later adds declaration-initialized automatic callback objects and
Cupid class method parameters.

This changes no build owner or host dependency. The promoted standalone
CupidC seeds do not contain the private in-kernel parser or ELF writer, so seed
reproof does not prove this capability. `TempleOS/` remains untouched reference
material.
