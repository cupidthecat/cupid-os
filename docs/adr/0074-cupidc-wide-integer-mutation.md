# Mutate wide integers through CupidC

- Status: Accepted
- Date: 2026-07-22

## Context

ADRs 0065 through 0073 carry signed and unsigned eight-byte integers through hosted Linear IR, i386 cdecl calls, objects, conversions, control flow, and ordinary integer operations. Compound assignment and prefix or postfix update still stopped at the older four-byte mutation boundary.

The active X25519 carry code depends on this missing behavior. `fe_carry` repeatedly adds a shifted eight-byte limb into the next limb and masks the stored value with `&=`. Replacing that code with manual word pairs would move a compiler restriction into the kernel's source.

## Decision

Hosted CupidC accepts all ten compound assignments and prefix or postfix increment and decrement for non-atomic signed and unsigned eight-byte integer objects. The frontend still chooses integer promotions, usual arithmetic conversions, and assignment conversion. Shift counts remain represented four-byte integers. Mixed signedness therefore follows the same conversion records as a non-mutating expression before the result converts back to the left operand's type.

Lowering evaluates the lvalue address once, duplicates that address, and loads one immutable eight-byte snapshot. It passes the loaded value through the existing conversion and binary-operation path, converts the result to the stored type, and uses `STORE_VALUE` to leave the stored value available to the surrounding expression. The address handle remains a single machine word even though the object it names is eight bytes, so `DUPLICATE_ADDRESS` now accepts a wide integer type. No new public IR instruction or target opcode is needed.

An update uses a wide integer constant of one. Prefix update returns the stored snapshot. Postfix update derives the earlier value from that snapshot with the inverse one-step operation, so it does not evaluate the address or load the object a second time. This is exact for unsigned arithmetic modulo 2^64 and for defined signed executions. Signed overflow remains undefined C behavior.

A volatile wide object follows the same one-load, one-store semantic sequence. Its multi-instruction i386 implementation is not atomic. `_Atomic` wide objects remain unsupported.

This increment widens mutation only when the destination itself is an eight-byte integer. A byte, word, doubleword, or bit-field destination combined with an eight-byte right operand, including a shift count, remains unsupported in Linear IR. Assignment narrowing and wide-count transport need their own contracts before they can share this path.

A public word-pair mutation operation would expose an emitter detail to every earlier compiler stage. Reusing an input snapshot would also break expressions that still refer to it. A second object load for the postfix result is unsafe because it repeats a volatile access and could observe a different value.

## Consequences and evidence

The unchanged `fe_carry` body remains the active source guard for wide `+=` and `&=`. The focused fixture publishes 15 functions and 225 exact IR instructions. It covers every compound operator, signed and unsigned prefix and postfix update, mixed signedness, narrow-to-wide conversion, a side-effecting lvalue, volatile access, and a chained store result.

The deterministic ELF32 proof contains 17 functions in 4,410 bytes of `.text`, has fingerprint `4B337038`, publishes 18 symbols including the null symbol, and has no relocations. Its execution oracle covers the ten compound operators, signed and unsigned updates, postfix snapshot preservation, one-time indexed evaluation, and volatile mutation. Malformed computation and operation metadata, atomic wide mutation, and a constrained output fail transactionally. The same job then reproduces the complete object byte for byte.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, its contracts, and every normal C object. No production transform, OS object, boot path, host dependency, or ownership count changes.

Issue #25 remains open. Eight-byte values passed through an ellipsis or an unprototyped call, floating values, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished.
