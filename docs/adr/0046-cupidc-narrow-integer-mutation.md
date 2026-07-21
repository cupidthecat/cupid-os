# Lower narrow integer mutation in hosted CupidC

## Context

ADR 0045 made one-byte and two-byte integers represented values in the hosted CupidC path. Compound assignment and increment or decrement still stopped at the older four-byte mutation check.

Active Toolchain source needs those operations without changing its types. `x86_put_u8` increments the one-byte `size` field after writing an encoded byte. Other decoder paths increment one-byte counters and combine prefix flags with `|=`. Replacing those fields with four-byte objects or spelling the operations as manual loads and stores would hide ordinary C requirements from the bootstrap plan.

The existing mutation IR already evaluates an lvalue once, computes in a promoted type, converts the result for assignment, and stores once. The narrow value work already gives the emitter signed and unsigned byte and word loads, canonical 32-bit stack values, conversions, and exact-width stores. A separate narrow mutation instruction or target opcode would duplicate those semantics.

## Decision

Hosted CupidC accepts compound assignment and prefix or postfix increment and decrement for represented, non-Boolean integer objects that occupy one, two, or four bytes. This includes plain `char`, signed and unsigned `char`, signed and unsigned `short`, and the established four-byte integer types. Atomic objects and bit fields remain outside this path.

`DUPLICATE_ADDRESS` now accepts any represented scalar address. Integer mutation still checks the narrower integer rules before using it. A byte or word load enters the abstract stack as a canonical 32-bit value. Integer promotion converts every supported narrow integer to the target's signed `int`. Compound assignment then applies the frontend's usual arithmetic type when required. The result passes through assignment conversion before `STORE_VALUE` writes the declared byte, word, or doubleword width.

CupidC gives out-of-range conversion to signed byte and word types a deterministic target result. The emitter keeps the low AL or AX lane and sign-extends it, which gives two's-complement wrapping on the i386 target. Prefix and postfix mutation use the same rule at signed minima and maxima.

Prefix updates return the canonical stored value. Postfix updates keep the existing single-load design: after the store, they promote the stored result, apply the inverse one-step operation, and convert back to the declared type. This also reconstructs the earlier value across byte and word wrapping without reading a volatile object twice.

`_Bool` mutation remains unsupported. Its assignment conversion saturates every nonzero result to one, so the inverse-operation method cannot reconstruct a prior true value after `state++`. Supporting that case needs a different postfix value strategy rather than a special exception in the current sequence.

A frozen update or compound-assignment node that claims a narrow computation type is malformed. Narrow operands must still compute in a four-byte promoted type. IR lowering reports the invalid-unit diagnostic, publishes no IR, preserves the input unit, and rewinds its temporary arena storage.

## Consequences and evidence

The complete unchanged `x86_put_u8` body is the active-source guard. Focused IR fixtures cover all ten compound operators, signed and unsigned byte and word updates, a volatile byte update, and a nested one-byte member compound assignment. The matrix requires 19 narrow address duplications, 25 narrow loads, 26 promotions, 23 narrowing assignment conversions, 20 exact-width stores, and one volatile load. Exact update and mixed-signedness fixtures check the order and types of every public IR record.

The deterministic ELF32 proof contains eight functions in 878 text bytes. Their exact offsets and sizes are 0/446, 446/108, 554/44, 598/44, 642/60, 702/60, 762/57, and 819/59. The object has ten symbols, a one-byte BSS object aligned to one byte, and one `R_386_32` relocation with addend zero. Shared decoding finds signed and unsigned byte and word loads, fourteen byte stores, four word stores, one signed divide, two unsigned divides, one multiplication, one left shift, two right shifts, and eight returns. A decoder-driven execution oracle runs twelve zero and wrap-boundary cases for signed and unsigned prefix and postfix updates. It checks EAX, the stored lane, and poisoned padding in the four-byte cdecl argument slot. Repeated emission is byte-identical and leaves the frozen translation unit and job arena unchanged.

Useful negative contracts retain precise failures for Boolean, atomic, bit-field storage smaller than four bytes, and wider mutation. ADR 0064 later replaces the four-byte bit-field negative with positive compound and update coverage. The malformed narrow computation-type case returns the invalid-unit diagnostic transactionally. The first positive update and compound tests stopped at the old unsupported-value check. The malformed computation test then returned unsupported until the validation path distinguished bad represented metadata from a genuinely unsupported computation type.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, ELF32, and contract modules. The host C compiler still produces the normal root and user C objects. The private in-kernel CupidC compiler remains the embedded runtime JIT and AOT path. No production artifact, build transform, host dependency, boot path, or runtime behavior changes ownership here.

Issue #25 remains open for Boolean mutation, aggregate initialization and values, bit fields outside the represented four-byte storage boundary, atomic access, 64-bit integers, floating values, production integration, staged self-hosting, and the fixed-point bootstrap.
