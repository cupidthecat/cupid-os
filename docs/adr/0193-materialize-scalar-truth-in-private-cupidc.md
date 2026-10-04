# ADR 0193: Materialize scalar truth in private CupidC

## Status

Accepted on 2026-07-30.

## Context

The private in-kernel CupidC compiler kept integer and pointer expressions in
EAX but kept `float` and `double` expressions in XMM0. Its control-flow
parsers tested EAX without first converting a floating value to C truth.
Unary `!`, conditional selection, `if`, `while`, `for`, and `do ... while`
therefore could branch on stale integer state.

The same parser path exposed a separate symbol contract problem. The kernel
binding table registered 223 non-void declarations through the `void` macro,
two clipboard wrappers erased their integer results, and
`fontsys_face_family` published an integer instead of a character pointer.
The old integer-only condition path happened to use EAX after these calls,
so the wrong symbol type could remain hidden until scalar validation was
added.

C truth makes both signed zero encodings false. Every other floating value is
true, including infinity and NaN. A valid condition must also be a scalar;
`void`, structures by value, and SIMD vectors cannot silently inherit a
register left by an earlier expression.

## Decision

Private CupidC uses one helper to materialize scalar truth in EAX. Integer
and pointer types already have their value there. For `float` and `double`,
the emitter clears XMM1, compares XMM0 with zero using `UCOMISS` or
`UCOMISD`, combines `setne` with `setp`, and zero-extends the result. The
parity term makes an unordered NaN true even though `UCOMI` also sets its
zero flag.

Unary `!`, `?:`, `if`, `while`, a present `for` condition, and the trailing
`do ... while` condition all call the same helper. An invalid operand reports
`truth test requires a scalar operand` and leaves the compiler in its normal
error-recovery path.

Kernel bindings must publish their declared result type. `BIND` is reserved
for functions that return no value. `BIND_T` identifies every integer,
pointer, `float`, or `double` result. The resulting 510-entry table contains
244 integer, 25 `float`, 25 `double`, 19 character-pointer, five
other-pointer, and 192 `void` results.

## Evidence

The focused truth suite extracts the active emitter and checks the exact
binary32 and binary64 instruction sequences. Its flag interpreter covers
positive and negative zero, subnormals, finite values, infinities, quiet
NaNs, and signaling NaNs. Separate contracts check the accepted scalar types,
the rejected aggregate and vector types, the diagnostic, and parser recovery.

The binding contract parses every local function-pointer declaration and
registration. It checks the complete 510-entry table, exact Cupid result
types, duplicate declarations, duplicate registrations, and missing
registrations. Its negative fixture proves that an integer declaration using
`BIND` receives a useful failure. The combined emitter, binding, unary,
comparison, and GUI contract run has 105 passing tests.

`feature13_double.cc` executes every truth-consuming parser path and reports:

```text
[feature13-truth] PASS zero=2 nonzero=3 control=255 nan=1
PASS feature13_double
[cupidc] JIT execution complete
```

The first four-vCPU run reached that marker, then failed while compiling
`godsong.cc` at its direct `if (input_dialog(...))` condition. The binding
still advertised `input_dialog` as `void`. Auditing the whole table instead
of patching that one call found the other result-type errors described above.

After the complete binding repair, a four-job Windows build completed in
554.3 seconds. It produced:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/lang/cupidc.o` | 288,272 | `6f89f5bf02e3e35e549f501601387ee5f41e380f1284e9a94ad2c55310770f45` |
| `kernel/lang/cupidc_parse.o` | 298,280 | `e8017bd231d0908a56faf63a53e2a74dcb6c4dba3421fe4d31b4ad0fccde1592` |
| `kernel/kernel.elf.pass1` | 8,592,316 | `b7aed88e66d90722b70ba2c10cb3eb1920c7bdeeb516582b39f016be85252a89` |
| `kernel/kernel.elf` | 8,702,908 | `4f6c3dfbc46d5e4eb2b1792c7ea2fe5bb95e74f2667e8eedb865c1e1ee35f906` |
| `kernel/kernel.bin` | 8,502,512 | `745ff6ba128d4a48e127eceed709672cf7c77e537bc457f84c72f01adf89e2b4` |
| `cupidos.img` | 209,715,200 | `e5b8f04fa689bcd24d84eabca3f0976a9efb13c152e4664e96a10581425fb233` |

The complete private four-vCPU e1000 frontier passed in 237.3 seconds. It
continued through `godsong.cc` and all later guest commands, completed six
USB storage lifetimes and HID reattachment, changed 71,952 framebuffer
pixels, captured 8,279,024 AC97 frames, and captured 76,163 PC-speaker
frames. The 46,975-byte serial log has SHA-256
`81fa437c3ab77cf05287573dfb0fd801c716a65286536c9fd7b4ab5ab4e09450`
and contains no truth diagnostic, failure marker, or panic.

The first complete checked-seed frontier compiled the cohort but stopped on
its old aggregate-size assertion. The approved objects now total 3,718,736
bytes rather than 3,708,988. After refreshing the reviewed compiler-object
and 444-input snapshot fingerprints, the 155-source, zero-boundary frontier
passed in 1,437.9 seconds. It compiled every source twice and required
byte-identical validated ELF32 output.

## Rejected alternatives

Testing EAX without converting a floating condition was rejected because EAX
does not contain the value of an XMM expression.

Using `setne` alone was rejected because `UCOMI` sets the zero flag for NaN.
The parity flag is required to make unordered values true.

Accepting every expression kind as a condition was rejected because `void`,
structures by value, and vectors are not C scalar operands.

Changing only `input_dialog` and the six other active control bindings was
rejected after the source contract found result-type drift throughout the
table. A complete declaration-to-registration audit gives later language
features a stable symbol boundary.

## Consequences

Private JIT and AOT programs can use scalar `float` and `double` values in
all supported C control forms. Binding calls now retain their declared result
type in later operators and conditions. No source or build ownership moved,
and no host compiler, assembler, linker, or packager was added.

Floating increment and decrement, SIMD operators outside the existing
intrinsics, and the remaining Cupid mode gaps stay open.
