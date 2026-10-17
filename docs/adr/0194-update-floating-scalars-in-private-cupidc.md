# ADR 0194: Update floating scalars in private CupidC

## Status

Accepted on 2026-07-30.

## Context

Private CupidC recognized prefix and postfix `++` and `--`, but each parser
path used the one-byte x86 `INC EAX` or `DEC EAX` instruction. Scalar
floating variables live in XMM registers, so applying those paths to a
`float` or `double` changed unrelated integer state and stored the wrong
value.

The behavior was copied across prefix expressions, postfix expressions,
standalone statement shortcuts, and `for` increments. Local, parameter, and
global storage paths were duplicated inside those parser branches. Postfix
expressions also need two values at once: the updated value for the store and
the original value for the expression result.

Arrays, structures, function pointers, and SIMD vectors are not valid scalar
update targets. Silently consuming `++` or `--` on one of those values would
hide a source error and leave stale register state behind.

## Decision

One typed variable-update helper serves all eight parser branches. It accepts
direct local, parameter, or global variables whose type is an integer,
supported object pointer, `float`, or `double`.

The floating path loads the variable into XMM0, converts integer one from EAX
into XMM1 at the variable's width, and emits `ADDSS`, `SUBSS`, `ADDSD`, or
`SUBSD`. It then stores XMM0 through the matching local or absolute-memory
path. A postfix expression copies the original XMM0 payload into XMM2 before
the arithmetic and restores it after the store. This preserves negative zero,
NaN payloads, and every other old result bit.

The integer and pointer path keeps its existing EAX representation and
increment size. Postfix forms keep the old EAX value on the stack while the
updated value is stored.

An invalid target reports
`increment or decrement requires a scalar variable`.

## Evidence

The new three-test oracle was red before the implementation: both active
helper extractions were missing, and the parser still contained eight direct
integer opcode copies. It now compiles the active emitter and checks these
exact instruction sequences:

| Operation | Bytes |
| --- | --- |
| `float` increment | `b801000000f30f2ac8f30f58c1` |
| `float` decrement | `b801000000f30f2ac8f30f5cc1` |
| `double` increment | `b801000000f20f2ac8f20f58c1` |
| `double` decrement | `b801000000f20f2ac8f20f5cc1` |

The compiled validation contract accepts a direct scalar `double` global and
rejects a `float4` local with the documented diagnostic. A static caller
contract requires all eight mutation sites to use the shared helper and
rejects either old direct opcode spelling.

The combined binding, unary, comparison, truth, update, and GUI contract run
has 109 passing tests.

`feature13_double.cc` covers local and global prefix and postfix results,
standalone increments and decrements, the `for` increment shortcut, an old
negative-zero payload, and NaN. It reports:

```text
[feature13-update] PASS local=48 global=40 for=3 zero=0x80000000 nan=2
PASS feature13_double
[cupidc] JIT execution complete
```

Checked-seed CupidC compiled the production parser object directly in 45.1
seconds. Replacing the duplicated mutation bodies reduced that object from
298,280 bytes to 297,180 bytes. Its SHA-256 is
`d46d4f50b885795cb4626ace8b16ba1b8bd1ee09c6a69adcce594360cbba161f`.

A four-job Windows normal build completed in 564.1 seconds:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/lang/cupidc_parse.o` | 297,180 | `d46d4f50b885795cb4626ace8b16ba1b8bd1ee09c6a69adcce594360cbba161f` |
| `kernel/kernel.elf.pass1` | 8,596,580 | `e38f3609e71f1c8a7967259f8b914267bdefa4af3a261a4f041ec3b53f8b5c8f` |
| `kernel/kernel.elf` | 8,707,172 | `1ade2983b5ac8e88f69f306b6d7fe67ff0cf5a8b465e4e371311fcbb02788d48` |
| `kernel/kernel.bin` | 8,504,588 | `74c43b1105e00a1c72f0c1a5483b5c850571f4f4d75a7809c189623ccabaa2ca` |
| `cupidos.img` | 209,715,200 | `5afbd32c486e64af1f92ca933e9f0a664cb4c5d7e9b29b23e8c7707529230b9b` |

The complete private four-vCPU e1000 frontier passed in 236.9 seconds. It
changed 91,141 framebuffer pixels, captured 8,301,609 AC97 frames, captured
76,923 PC-speaker frames, and completed every later compiler and hardware
gate. Its 48,990-byte serial log has SHA-256
`ebe6872bd74860c7cb3d9d841b34f4bb8696f51b0c849dfdc0dcbffd9a52db0d`.
The update marker appears once, and no update diagnostic, failure marker, or
panic appears.

The refreshed checked-seed frontier compiled all 155 production sources
twice with zero deferred boundaries and passed in 1,386.9 seconds. The
validated objects total 3,717,636 bytes. The 444-input snapshot has SHA-256
`5124e8c33394388519e391ae359f726b0de60e8b18cfd364b7e09bbbe6765ff1`.

## Rejected alternatives

Converting the floating value to an integer, applying `INC` or `DEC`, and
converting back was rejected because it truncates fractions and cannot
preserve NaN, infinity, signed zero, or values outside the represented
integer range.

Adding a new data-segment `1.0` literal at every update was rejected because
the existing integer-to-floating conversion emits the exact value in four
instructions without growing runtime data.

Patching each parser branch separately was rejected because the existing
copies had already drifted away from the compiler's typed register model.
One helper keeps validation, value preservation, storage, and result typing
together.

Leaving invalid postfix targets as a silent no-op was rejected because it
turns a source error into stale expression state.

## Consequences

Private JIT and AOT programs can use prefix and postfix floating updates in
expressions, statements, and `for` loops. Direct locals, parameters, and
globals share the same implementation. The change adds no host dependency
and moves no build ownership.

Hosted CupidC floating updates, indirect lvalue updates, SIMD updates, and
the remaining Cupid mode gaps stay open.
