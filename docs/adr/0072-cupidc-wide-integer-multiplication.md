# Multiply wide integers through CupidC

- Status: Accepted
- Date: 2026-07-22

## Context

ADRs 0065 through 0071 carry signed and unsigned eight-byte integers through hosted Linear IR as one value backed by a private i386 frame snapshot. Addition, subtraction, shifts, bitwise operations, comparisons, conditions, and switch dispatch already use that representation. Multiplication still stopped at the unsupported-type boundary.

Active source needs the normal C operation. CupidASM's `asm_parse_number` multiplies a `ctool_u64` accumulator by its base. X25519's `fe_mul_u32` multiplies eight 32-bit limbs into 64-bit intermediates before carrying them. Rewriting those functions into source-level word arithmetic would move a compiler limit into working code.

## Decision

Linear IR accepts `MULTIPLY` when the result and both post-conversion operands are the same represented signed or unsigned eight-byte integer type. The frontend continues to own integer promotion and the usual arithmetic conversions. This keeps mixed signedness and a represented narrow operand out of the backend until an explicit conversion instruction makes the types agree. Wide division, remainder, and mutation were separate boundaries in this decision. ADR 0073 later adds division and remainder, and ADR 0074 reuses multiplication for compound assignment.

Let each operand be `a0 + 2^32 a1` and `b0 + 2^32 b1`. The i386 emitter computes the result modulo 2^64 as:

`a0 * b0 + 2^32 * (a0 * b1 + a1 * b0)`

One unsigned 32-bit `MUL` produces the complete low-word product in EDX:EAX. Two two-operand `IMUL` instructions produce the low halves of the cross products. Those low halves are identical for signed and unsigned inputs, so both C types use the same target sequence. The cross sum lives briefly in a net-zero machine-stack slot while EDX:EAX computes the low product. The completed value is stored in a new private snapshot.

Unsigned multiplication wraps modulo 2^64. Signed multiplication uses the same bit sequence for every defined result; signed overflow keeps C's existing undefined-behavior rule. The sequence makes no helper call, adds no relocation, exposes no public word lane, and does not use a callee-saved register.

A runtime helper was rejected because it would add an ABI and link dependency to a leaf operation. Publishing two 32-bit Linear IR values was rejected because it would expose the i386 representation. Reusing an operand snapshot was rejected because chained expressions may still need either input.

## Consequences and evidence

The first red IR run stopped at `/wide-arithmetic.c:35:15` with the existing unsupported-type diagnostic. After IR accepted the operation, object emission stopped transactionally before writing an object. The final implementation closes both seams without changing the frontend or active source.

The arithmetic IR fixture at this boundary has 19 functions and 118 instructions. Its original 83-instruction prefix keeps fingerprint `245E6D8F4F77588E`. Five exact slices cover signed multiplication, unsigned multiplication, signed-to-unsigned usual arithmetic conversion, narrow-to-wide conversion, and a chained expression with two fresh results. A malformed multiplication conversion reaches production validation and fails without changing the input unit. Wide division and remainder remain useful unsupported contracts here; ADR 0073 replaces them with positive quotient and remainder proofs.

The deterministic ELF32 multiplication proof contains six functions, 1,103 bytes of `.text`, fingerprint `E357BE84`, seven symbols including the null symbol, and no relocations. Its decoder finds seven one-operand `MUL`, fourteen two-operand `IMUL`, six returns, and no call, `DIV`, or `IDIV`. The execution oracle covers zero, identity, low-word carry, cross-word contribution, high-bit wrap, `INT64_MIN * 1`, negative by positive, negative by negative, mixed signedness, wide by narrow, chaining, and reuse of a still-live input snapshot. It also checks arguments, ESP, EBP, EBX, ESI, EDI, and a stack sentinel after every call.

The unchanged `asm_parse_number` and `fe_mul_u32` bodies guard the active requirements. Repeat emission is byte-identical. A 64-byte output limit leaves no partial object, and the next emission in the same job reproduces the complete result.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, its contracts, and every normal C object. No production transform, OS object, boot path, host dependency, or ownership count changes.

Issue #25 remains open. ADR 0073 later adds wide division and remainder, and ADR 0074 adds compound assignment and prefix or postfix update. Values without declared parameter types, floating values, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished.

## Extension: wide variadic transport

ADR 0075 carries signed and unsigned eight-byte values through ellipsis and unprototyped calls and returns wide `va_arg` reads in private snapshots. Multiplication consumes those snapshots through the same immutable operand path defined here.
