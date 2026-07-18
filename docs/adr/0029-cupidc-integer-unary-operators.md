# Lower 32-bit integer unary operators through CupidC IR

## Context

The hosted CupidC path already retained all four C integer unary operators in its typed AST, but the linear IR accepted only bitwise complement. Active source contains the remaining operations. `dis_signed_bits` in `toolchain/cupiddis.c` negates a converted magnitude, and `cc_skip_brace_initializer` in `bin/cupidc_parse.c` returns the logical negation of its error field. Their complete functions still need casts, control flow, or pointer-based member access that lies outside the current leaf subset.

Rewriting either expression would hide C semantics that CupidC needs. Unary plus has no active runtime requirement today, but it belongs to the same promoted-integer family and completes this bounded operator seam.

## Decision

`CTOOL_C_IR_INSTRUCTION_UNARY` accepts unary plus, unary negation, bitwise complement, and logical not for represented four-byte integer operands. Unary plus, negation, and complement retain the promoted operand type as their result. Logical not retains the operand type in `input_type` and requires plain, unqualified signed `int` as its result type. A caller-supplied frozen unit that changes that result to another four-byte integer type is invalid.

The i386 emitter leaves the existing stack slot in place for unary plus. It emits `NEG EAX` for signed or unsigned negation and keeps `NOT EAX` for complement. Logical not uses `TEST EAX, EAX`, `SETE AL`, and `MOVZX EAX, AL`, then pushes the normalized signed `int` result.

An extra `POP` and `PUSH` pair for unary plus was rejected because it would add code without changing the value. The emitter already maps represented conversions to zero machine bytes, so a semantic IR instruction does not need a machine instruction. Implementing logical not as XOR with one was also rejected because that works only when the input is already zero or one.

## Consequences and evidence

The IR contract guards the active negation and logical-not expressions, then lowers four focused functions. Their 16 exact instructions cover signed unary plus, signed negation, unsigned negation, and logical not over an unsigned operand with a signed `int` result. Each function has a maximum abstract-stack depth of one.

The object contract emits those functions in one deterministic ELF32 object. Its 86 text bytes contain functions of 17, 21, 21, and 27 bytes. The object has five symbols, no relocations, and decoded coverage for `NEG`, `TEST`, `SETE`, and `MOVZX`. Repeated emission is byte-identical and leaves the frozen frontend unit unchanged.

Separate fixtures keep 64-bit unary plus, negation, logical not, and complement at the unsupported-type boundary. A copied logical-not node with an unsigned result receives the invalid-unit diagnostic. Object emission also proves that an unsupported 64-bit logical-not function leaves the output empty.

This is hosted bootstrap evidence. The host C compiler still produces the normal root and user C objects. The private in-kernel compiler remains the embedded runtime JIT and AOT path. No production artifact, build owner, host dependency, boot path, or runtime ABI changed.

Issue #25 remains open. Narrow, wide, floating, and pointer unary operands, address and dereference lowering, bit-field writes, broader statements and calls, production integration, and staged self-hosting still remain.
