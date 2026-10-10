# Divide wide integers through CupidC

- Status: Accepted
- Date: 2026-07-22

## Context

ADRs 0065 through 0072 carry signed and unsigned eight-byte integers through hosted Linear IR as one value backed by a private i386 frame snapshot. The remaining ordinary binary arithmetic gap was division and remainder.

Active source needs both operations. `cfront_constant_apply_binary` computes signed and unsigned quotients and remainders while folding target integers. CupidASM's `asm_parse_number` divides a 64-bit overflow bound by the current base. The preprocessor integer parser and frontend static-address checks have the same wide divisor requirement. Rewriting those expressions or moving them to host-only helpers would push a compiler limit into working source.

## Decision

Linear IR accepts `DIVIDE` and `REMAINDER` when the result and both post-conversion operands are the same represented signed or unsigned eight-byte integer type. The frontend continues to own promotion and the usual arithmetic conversions. A malformed record with matching wide operands but a different result type is invalid input.

The i386 emitter uses a fixed 64-step restoring divider. It copies both immutable operands into a balanced 40-byte machine-stack scratch area and keeps two-word dividend, divisor, quotient, and remainder values there. Each round shifts the quotient, moves the dividend's top bit into the remainder, compares the remainder with the divisor as an unsigned 64-bit value, and subtracts with `SUB` and `SBB` when needed. A carry branch preserves the complete restoring comparison before the high- and low-word checks. The scratch area is released before a fresh result snapshot is published.

Signed operations first convert both operands to unsigned magnitudes. The quotient receives the XOR of the operand signs, while the remainder receives the dividend sign. This implements truncation toward zero and gives a nonzero remainder the dividend's sign for every defined C case. Unsigned operations use the same restoring loop without sign conversion.

Division by zero and signed `INT64_MIN / -1` or `INT64_MIN % -1` remain undefined C behavior. The software sequence promises neither the hardware divide trap used by the narrow path nor a particular result for those inputs. Contracts do not turn either result into an ABI guarantee.

A runtime helper was rejected because it would add a call, relocation, ABI surface, and link dependency. A 64-round unrolled sequence was rejected because it would make every quotient or remainder object needlessly large. Reusing an operand snapshot was rejected because later expressions may still read either input.

## Consequences and evidence

The arithmetic IR fixture has 26 functions and 165 instructions. Its original 83-instruction prefix keeps fingerprint `245E6D8F4F77588E`, and every multiplication slice is unchanged. Seven exact slices cover signed and unsigned quotient and remainder, mixed signedness, a narrow divisor widened by the frontend, and a chained quotient/remainder expression. Invalid conversion and result metadata fail transactionally. The earlier wide-mutation rejection became the red baseline for ADR 0074; an invalid wide shift count remains a useful unsupported contract.

The deterministic ELF32 proof contains eleven functions, 4,775 bytes of `.text`, fingerprint `55F1A495`, twelve symbols including the null symbol, and no relocations. Thirteen emitted divide or remainder operations each have one fixed loop. Decoder checks pin the carry branch, unsigned high- and low-word comparisons, shared subtraction block, repeat edge, sign handling, balanced scratch, and absence of calls or hardware divide instructions. Thirty-three defined execution checks cover low- and high-word operands, all four signed sign combinations, unsigned high-bit divisors, mixed and narrow conversions, chaining, input reuse, ABI state, and stack sentinels.

The unchanged `cfront_constant_apply_binary` body is source-guarded as the signed and unsigned folding requirement. CupidASM's unchanged number-parser expression supplies a second active wide-divisor guard. These guards tie the focused executable fixtures to real source without claiming that either complete function has transferred to CupidC.

Repeat emission is byte-identical. A constrained output leaves no partial object, and the next operation in the same job reproduces the complete result. The emitter uses EAX, ECX, EDX, and scratch memory only, so EBX, ESI, EDI, EBP, ESP, arguments, and the surrounding frame retain their cdecl obligations.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, its contracts, and every normal C object. No production transform, OS object, boot path, host dependency, or ownership count changes.

Issue #25 remains open. ADR 0074 later adds wide compound assignment and prefix or postfix update. Values without declared parameter types, floating values, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished.

## Extension: wide variadic transport

ADR 0075 carries signed and unsigned eight-byte values through ellipsis and unprototyped calls and returns wide `va_arg` reads in private snapshots. Division and remainder consume those snapshots without changing the restoring loop or undefined-input boundary.
