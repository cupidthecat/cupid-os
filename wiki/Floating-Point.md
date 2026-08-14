# Floating Point in CupidOS

CupidOS supports x87 and SSE/SSE2 floating point in the kernel, CupidC,
CupidASM, libm, and `printf`.

## Overview

- **Target policy**: Kernel and Doom profiles use SSE/SSE2 floating code and
  16-byte call alignment. The native-oracle CFLAGS record this as
  `-mfpmath=sse -msse -msse2 -mstackrealign`.
- **Init**: `fpu_init()` is the first call in `kmain`. Sets CR0.EM=0/MP=1/NE=1/TS=0
  and CR4.OSFXSR=1/OSXMMEXCPT=1, runs FNINIT, loads MXCSR=0x1F80.
  `fpu_init_cpu()` uses `target("general-regs-only")` so the compiler cannot
  generate floating-register work before that setup finishes.
- **Context switch**: `context_switch.asm` uses FXSAVE on the outgoing PCB's
  `fp_state[512]` and FXRSTOR on the incoming task. State switching is eager;
  it does not use CR0.TS for lazy switching.
- **PCB**: `process_t.fp_state` is 512 bytes at 16-byte alignment. Offset 80.
- **Exceptions**: IDT vectors 7 (#NM), 16 (#MF), 19 (#XF) route to
  `fpu_nm_handler`/`fpu_mf_handler`/`fpu_xf_handler`, which call `panic_fpu`
  with a serial dump of FSW/FCW/MXCSR. These handlers are not expected to fire
  while MXCSR exceptions are masked and eager switching is active.

## CupidC FP types

- `float`   - 4 bytes, SSE scalar.
- `double`  - 8 bytes, SSE scalar.
- `float4`  - 16 bytes, SSE packed (4 floats).
- `double2` - 16 bytes, SSE packed (2 doubles).

### Shared self-hosting path

The shared compiler used for the self-hosting migration combines its i386 x87
transport and arithmetic path with SSE conversion and comparison emission. It
carries non-atomic `float` and `double` values through objects, calls,
variadic reads, and returns. Explicit casts and assignment conversion work in
either direction between those widths. Mixed arithmetic and conditional arms
use `double`. Matching floating conditional arms keep their width, and the
condition may be a represented integer or pointer.

`+=`, `-=`, `*=`, and `/=` compute at the common width and convert the stored
result back to the left type. The compiler evaluates the left designator once.
Each changed x87 result is stored at its C width before the next Linear IR
instruction.

Decimal constants, represented integer conversions, mixed
integer-and-floating arithmetic, and all six comparisons use the shared SSE
path. Runtime `float` and `double` values convert to represented unsigned
targets through four bytes. Binary32 widens exactly to binary64, and a
four-byte result splits at 2^31 before signed truncation. A mixed floating
comparison uses `double`; only `!=` is true for an unordered NaN input. ADR
0250 records the unsigned-output rule.

Non-atomic `long double` values now use twelve-byte target objects. Automatic
values use frame snapshots. Static-duration scalars, fixed arrays, and
complete records may contain long-double leaves. Implicit initialization
zeros the complete object. An explicit leaf accepts a represented integer
constant expression or a bounded decimal `L` literal with parentheses
and unary signs. The emitter writes the ten exact x87 value bytes, clears the
two padding bytes, and selects `.bss`, `.data`, or `.rodata` from the payload
and qualifiers. Atomic leaves fail recursively without following pointers.
Bounded finite normal decimal `L` tokens round an exact ratio to a 64-bit
explicit significand with ties to even. The emitter writes that significand
and the 16-bit sign and exponent into a twelve-byte snapshot. The ten value
bytes move through x87 80-bit `FLD` and `FSTP` memory forms in Cupid's shared
x86 catalogue. The hosted path converts among
`float`, `double`, and `long double`, applies unary plus and minus, and
evaluates addition, subtraction, multiplication, and division. Direct and
indirect fixed, variadic, and unprototyped arguments occupy twelve cdecl
bytes. Functions return the value in x87 `ST0`, and direct or indirect callers
store it in a twelve-byte snapshot. `va_arg(long double)` copies twelve bytes
and leaves the cursor at the following four-byte slot. All six comparisons
accept matching long-double values and mixed `float` or `double` inputs. The
emitter loads right then left, executes `FUCOMIP ST0, ST1`, and discards the
surviving x87 value. Signed zeros compare equal, and only `!=` is true for an
unordered input. Runtime casts, assignments, arguments, and returns convert
between `long double` and signed or unsigned integers at 8, 16, 32, and
64 bits. The integer-output path restores the x87 control word after
truncation toward zero. Unsigned 64-bit corrections temporarily select
64-bit x87 precision, preserve the caller's rounding mode, and restore the
complete saved word before the final store. Static initializer conversion
covers `_Bool`, plain `char`, every signed or unsigned i386 integer width, and
an enum whose compatible integer type has the represented target layout. For
a nonzero magnitude `M` with bit width `w`, the x87 significand is
`M << (64 - w)`. The high word is
`(negative ? 0x8000 : 0) | (0x3fff + w - 1)`. The reverse path truncates the
decoded significand toward zero before the range check for integer
destinations other than `_Bool`. `_Bool` tests the original floating value:
both signed zeros become false, and every represented finite nonzero value
becomes true. The fixture converts `-0.5L` to both targets, producing true for
`_Bool` and zero for an unsigned integer. Integer zero keeps `ZERO`
metadata. Static long-double truth, all six comparisons, short-circuit logic,
and conditional selection use one target-only decoder. It accepts canonical
x87 zero, subnormal, normal, infinity, and NaN payloads while rejecting pseudo
encodings. Finite binary32 and binary64 values widen exactly. Infinity keeps
its sign, and NaN becomes one quiet x87 payload. Narrowing uses
round-to-nearest, ties-to-even packing for finite values and canonical target
encodings for infinity and NaN. Folded results add no runtime IR. Hexadecimal
or subnormal long-double literals and decimal ratios beyond the bounded parser
remain open. Static `+`, `-`, `*`, and `/` use an unsigned 128-bit target
packer. It rounds once to the explicit x87 significand, including gradual
underflow, and emits canonical special values. The 80-result contract checks
all twelve object bytes and proves that Linear IR publishes no runtime work.

The static scalar and aggregate proofs cover both scopes, mutable and const
objects, positive and negative values, both signed zeros, the largest accepted
bounded literal, exact section placement, symbols, padding, malformed
metadata, recovery, and deterministic repeated emission. The hosted i386
runtime reads the three target words for every literal payload. The conversion
fixture covers every integer kind, signed and unsigned enums, both signed
64-bit endpoints, `ULLONG_MAX`, and both results of `-0.5L`. ADR 0251 records
the static-data boundary, ADR 0254 records integer conversion, and ADR 0255
records static controls and finite width conversion. ADR 0256 records the
canonical x87 decoder and special-value transport. ADR 0260 records the
static arithmetic and rounding model. ADR 0258 records checked-seed carriage
of the earlier static frontier, and ADR 0265 records carriage of the arithmetic
path.

The checked native Windows seed carried this path through an earlier
2026-08-13 poisoned-host checkpoint. Its first invocation stopped at the
602.5-second command limit; the resumed build finished in 968.5 seconds, for
1,571.0 seconds of cumulative work. At that checkpoint, the 2,560-byte boot
image had
SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The 9,056,612-byte pass-one ELF has SHA-256
`e2f63b5cd9c4e2769b9d6bc893ab5cf778951b97aec954ece6cbac0cc429e92a`,
the 9,179,492-byte final ELF has SHA-256
`1bc06263dbf9849e6d2c594b6fb4be2a3f3b673c91f69d23a2d2e639b1f64776`,
and the 8,962,776-byte raw kernel has SHA-256
`3170aa71eafa656b1f6e23c918f1f472860f513c9c5cd0376d7d4f5f8a7d891c`.
Its exact-size prerequisite accepted all nine artifacts before publishing the
209,715,200-byte image with SHA-256
`3b5dd6523a90d6ed0543a6ab2464892f3289b876654f9869f88db0901940b91e`.
A four-vCPU RTL8139 frontier passed from this image in 820.7 seconds. Private
CupidC emitted the broad indirect-update marker, compiled and loaded the
dedicated external ELF as PID 4, emitted
`[feature13-derived-aot] PASS score=41 once=2 zero=0x80000000`, and reported
that same PID exiting. The full SMP, framebuffer, audio, USB detach/replug, and
survival checks passed. The completed dual-NIC checkpoint immediately before
this rebuild used image
SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
The earlier 1,057.969-second build and definitive four-vCPU E1000 and RTL8139
frontiers remain pre-freeze evidence. Those boots passed the AC97 and PC
speaker checks with exits 0 in 794.034 and 758.667 seconds.

The current poisoned-host normal build finished in 674.693 seconds. Its
2,560-byte boot image keeps the same SHA-256. The 9,211,340-byte pass-one ELF
has SHA-256
`2a6f5deafb580b30254483179d6dade9ed4ed7b17b39f9368137b1ff14932263`,
the 9,334,220-byte final ELF has SHA-256
`bc855462c1f8f42e34d94a974443f7c6e565d60b1913e3b6f33b3e6e375f3ed6`,
and the 9,114,084-byte raw kernel has SHA-256
`8b5d73e74538ce11c1fb074f88b3852d690038aa5cb3a8de3ce222e9df88cade`.
The published 209,715,200-byte image has SHA-256
`813c9b0c78f795c1ac9fcff59b9c4111a958a07eb1e3943dc7af60c536521110`.
A private four-vCPU `/bin/ls.cc` JIT boot passed from that image in 49.257
seconds.

The checked i386 Linux seed at ADR 0138 carries static floating constant data
and this complete comparison path.

The checked seed understands the `general-regs-only` target on canonical
file-scope functions. It rejects compiler-generated floating instructions,
values, and call arguments while permitting explicit source assembly through
its separate contract. It also accepts the exact LDMXCSR
memory input used by `fpu_init_cpu()` and emits `0F AE 10` through the shared
x86 model. It accepts the exact MOVSS float-memory round trip in
`fpu_boot_smoke()` and the matching one-way load and store forms. Each form
requires the `xmm0` clobber. The shared encoder emits `F3 0F 10 00` for the
load and `F3 0F 11 00` for the store through EAX. It also accepts the exact
volatile x87 block in `stress_sin()`. The statement has one `double` output,
one `double` input, and no clobbers. It emits `FLD`, `FSIN`, and `FSTP`
through the shared encoder, with balanced x87 depth and no frame temporary.
The checked compiler also accepts exact `fldcw %0` with one addressable, non-atomic
16-bit integer memory input. GNU semantics make the no-output statement
volatile even when the keyword is omitted. Linear IR evaluates the address
once, and the emitter produces `D9 /5` through EAX. ADR 0258 records
checked-seed carriage.
The earlier compiler proof used `kernel/cpu/fpu.c`. The unchanged
implementation is now `kernel/cpu/fpu.cc`, a checked CupidC production root.
Two checked compiles produce the same validated 6,620-byte object with
SHA-256
`14c3ea232b7d4455ceabd561c69293cc5849abae24d9f210aa69d64ed8c8a5cb`.
Cupid's ELF reader and x86 decoder also enforce the production
`fpu_init_cpu()` order: one CR4 write, then `FNINIT`, then one 32-bit memory
`LDMXCSR`, with no helper call or other floating work. Four-vCPU e1000 and
RTL8139 boots print `[fpu] SSE2 enabled`, `[fpu] boot smoke ok`, and
`FPU boot smoke passed` before finishing `feature16_asm_fpu.cc`.

The checked seed also represents the two exact EFLAGS restore statements
that guard SIMD CPUID detection. Each volatile statement takes one 32-bit
integer through `r`, has no output, and requires one `cc` clobber. The shared
x86 path emits `POP EAX`, `PUSH EAX`, and `POPF` with balanced ESP.
It also accepts the CPUID leaf input sharing EAX with its
compatible write-only output. It loads that leaf immediately before CPUID
and keeps the four existing output snapshots. The checked seed also emits all
six packed SSE2 statement shapes in `kernel/cpu/simd.cc`. It retains the
ordered pointer and integer inputs and exact memory plus XMM0 through XMM7
clobbers. The production wrapper freezes the source and its seven-header
closure. Two checked builds produce the same validated 8,768-byte ELF32
`ET_REL` object with SHA-256
`fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`.
The normal SIMD recipe now uses this checked object.

The checked seed also represents the exact x87 round-down statement in
`str_floor()`. It takes one `double` memory output and one `double`
memory input, with the exact `ax` and `memory` clobbers. The emitted sequence
saves the incoming x87 control word below ESP, changes its rounding-control
field to round toward negative infinity, executes `FRNDINT`, restores the
saved word, and stores the result. It reuses the consumed input-address slot
for scratch, so the pending output address is not overwritten and no frame
temporary is needed.

A bounded shared-decoder oracle checks eight binary64 values under all four
incoming rounding modes. It verifies exact results and control-word
restoration without executing native x87 code. Two exact compiles of the
unchanged helper produce the same 420-byte object.

The checked seed also emits the later explicit non-atomic `double` to
`uint64_t` casts. It obtains the high word from `value / 2^32`, reconstructs
and subtracts that exact multiple, then obtains the low word from the
remainder. Each unsigned-word step splits at 2^31 before the signed SSE
truncation. The decoder-driven oracle covers positive and negative fractions
and the active range through the largest binary64 value below 2^64. Two
complete compiles of unchanged
`kernel/core/string.cc` produce the same 14,460-byte object with SHA-256
`d48bb6ea18b7124fbefeaca0d5d5ee8a517db950f21ea88e30ededd6c5c2a577`.
The production wrapper freezes the source and its two headers, validates the
ELF32 object, and publishes it without a host compiler. ADR 0181 records the
transfer.

The checked seed represents the x87 power statements in `libm_pow_impl()` and
`libm_powf_impl()`. The double form has five `double` memory operands. The
mixed form has a `float` output, two `float` inputs, and two `double` inputs.
Each requires one memory clobber. Linear IR evaluates each set of five
addresses once in source order. Both 116-byte focused functions contain
seventeen x87 instructions, use `DC E9` for `FSUB ST(1), ST(0)`, reach stack
depth three, and return to their incoming depth without a relocation. All
seven active range-reduction sites use the corrected GNU spelling for the
forward `x - round(x)` remainder. The checked seed keeps the old `DC E1`
spelling as a compatibility contract. ADRs 0208 and 0209 record seed carriage
and the runtime-tested source correction.

The checked seed also represents the exact volatile `sqrtsd %1, %0` statement
in `libm_sqrt_impl()`. It takes one modifiable, non-atomic `double` `=x`
output and one non-atomic `double` `x` input, with no clobbers. Linear IR
evaluates the output address before the input value. The focused function
contains 65 text bytes, no relocations, and a direct `MOVSD`, `SQRTSD`,
`MOVSD` path through Cupid's shared x86 model.

The exact volatile x87 statement in `libm_atan2_impl()` is represented too.
It takes one modifiable, non-atomic `double` `=m` output, two addressable,
non-atomic `double` `m` inputs in `y`, `x` order, and one `memory` clobber.
Linear IR evaluates the three addresses once in source order. The focused
function contains 53 text bytes and no relocations. Its direct 15-byte path
loads both operands, applies `FPATAN`, and stores the result through Cupid's
shared x86 model.

The exact volatile x87 statement in `libm_exp_impl()` is represented too.
It takes one modifiable, non-atomic `double` `=m` output, two addressable,
non-atomic `double` `m` inputs in `x`, `log2e` order, and one `memory`
clobber. Linear IR evaluates all three addresses once in source order. The
focused function contains 71 text bytes and no relocations. Its direct
33-byte path computes `exp2(x * log2(e))`, reaches x87 depth three, and
returns to its incoming depth through Cupid's shared x86 model.

The checked seed also represents the exact aligned `fabs` mask block and the
following `fabs` and `fabsf` wrappers. The masks occupy the first 32 bytes of
`.rodata`, with local labels at offsets 0 and 16. The 15-byte double wrapper
uses `MOVSD` and `ANDPD`; the 14-byte float wrapper uses `MOVSS` and `ANDPS`.
Each has one absolute relocation to its mask.

The following `floor`, `floorf`, `ceil`, `ceilf`, `round`, `roundf`,
`trunc`, and `truncf` wrappers are represented by the checked seed too. They
save the incoming x87 control word, select the source rounding mode, apply
`FRNDINT`, and restore the original word. The four pairs select down, up,
nearest-even, and toward-zero mode. Together they occupy 384 text bytes with
no relocations, never exceed x87 depth one, and balance ESP and x87 depth.

The following `fmod` and `fmodf` wrappers are represented by the checked seed
as well. Each repeats `FPREM` while x87 status-word C2 is set, uses a short
backward branch to the reduction instruction, discards the divisor, and
returns the remainder through XMM0. Each body contains 35 text bytes, reaches
x87 depth two, balances ESP and x87 depth, and has no relocation.

The checked seed also represents the aligned `libm_log2e_const` and
`libm_ln2_const` data and the next eight exponent and logarithm wrappers.
The constants occupy 16 `.rodata` bytes at alignment eight. `exp2` and
`exp2f` use the shared `FRNDINT`, `F2XM1`, and `FSCALE` sequence. `exp` and
`expf` first multiply by `log2(e)`. `log2` and `log2f` load one before
`FYL2X`, while `log` and `logf` load `ln(2)`. The functions add 264 text
bytes and four absolute relocations, never exceed x87 depth three, and
balance ESP and x87 depth. The file then reaches `pow` on line
846.

The checked seed represents `pow` and all 17 later cdecl bridge wrappers.
The `pow`, `hypot`, and `nextafter` pairs take two arguments. The `asin`,
`acos`, `sinh`, `cosh`, `tanh`, and `cbrt` pairs take one. Each copies its
original cdecl argument words, calls the matching external implementation,
reclaims the copy, and moves the ST(0) result into XMM0 at float or double
width. The family has 558 text bytes and 18 PC-relative call relocations.
Two complete compiles of corrected `kernel/cpu/libm.cc` produce the same
16,164-byte ELF32 relocatable object with SHA-256
`c0911732361f2e1ea78aa778f834719ba12208cc2d9f0a312455a5e6a38a75b4`.

The normal `kernel/cpu/libm.cc` recipe now uses the checked CupidC wrapper.
Its frozen closure contains `kernel/core/types.h` and `kernel/cpu/libm.h`.
The guest smoke runs `/bin/feature15_libm.cc` and requires seven x87 range
checks, all 29 checks, both zero-failure summaries, and `PASS feature15_libm`.
Fresh four-CPU e1000 and RTL8139 runs pass that gate in 235.259 and 232.832
seconds, respectively.
ADR 0176 records the production transfer, and ADR 0209 records the numerical
correction.

The normal build now compiles `kernel/gfx/jpeg.cc` and
`kernel/gfx/glyph_raster.cc` with that seed. JPEG exercises exact static
floating data, while glyph rasterization exercises the comparison path. The
strong guest gate checks the TrueType path and every pixel of a byte-fixed
baseline JPEG. ADR 0139 records the production transfer.

Runtime `float`, `double`, and automatic `long double` values work with unary
`!`, `&&`, `||`, the controlling operand of `?:`, the conditions of `if`,
`while`, `do`, and `for`, and conversion to `_Bool`. Both signed zeros are
false; finite nonzero values, subnormals, infinities, and NaNs are true.
Checked-seed hosted CupidC accepts prefix and postfix increment and decrement
on modifiable non-atomic `float` and `double` lvalues. It evaluates the lvalue
once and stores the result of adding or subtracting exact-width `1.0`.
Postfix returns the original raw payload, including signed zero and NaN bits.
Atomic floating and `long double` updates,
hexadecimal floating constants, binary32 and binary64 subnormal constants,
hexadecimal or subnormal long-double literals, decimal ratios beyond the
bounded parser, general SIMD value semantics, and atomic floating access
remain unsupported. Static truth, comparison, arithmetic,
short-circuit logic, conditional selection, and floating-width conversion fold
through the target representation.
Twelve-byte direct
and indirect fixed, variadic, and unprototyped arguments, function returns,
direct and indirect call results, and `va_arg(long double)` use the represented
automatic `long double` path. The exact production SIMD assembly forms above
are a narrower checked path. The SSE details below describe the private
in-kernel compiler.

Private comparisons and truth tests merge the parity flag with `SETNP` or
`SETP`. Checked-seed CupidDis recognizes both byte predicates, so
`dis /bin/test_fpaug.cc` stays aligned through the following Boolean merge and
`MOVZX`. The guest then executes those bounded parity checks before running the
full feature-13 comparison and truth program. ADR 0259 records the shared
instruction boundary, and ADR 0265 records checked-seed carriage.

That private compiler now passes arbitrary mixtures of represented four-byte
scalars and pointers with eight-byte `double` values. Direct,
function-pointer, and method calls preserve left-to-right evaluation, arrange
complete argument words in cdecl source order, and use the same widths for
callee parameter offsets and caller cleanup. The feature13 guest requires
ten calls through one mixed-width tolerance helper. ADR 0198 records this
private ABI boundary.

A direct function or method with parsed fixed parameter types converts
represented integer, `char`, `float`, and `double` arguments to the declared
slot type before cdecl layout. Represented pointer categories and integer null
forms can fill a pointer slot. A represented object pointer can fill a fixed
`int` or `unsigned int` slot as one unchanged i386 word. Narrow and floating
destinations remain rejected. A parsed variadic tail widens `float` to `double`
and promotes `char` to `int`. Function-pointer calls, kernel bindings, and
calls without that metadata keep their source-width slots.

Private decimal `float` and `double` literals use a fixed 1536-bit integer
workspace. The converter forms the exact decimal ratio and rounds once to the
requested IEEE width using nearest-even. An `f` suffix selects binary32 before
rounding. Tests cover long halfway cases, minimum subnormals, largest finite
values, overflow to infinity, underflow, and signed zero. Numeric tokens may
contain 95 characters including their suffix. Hexadecimal floating and `long
double` literals remain unsupported. ADR 0217 records this boundary.

Fixed `float` and `double` arrays keep their element type through one, two, or
three dimensions in global, automatic, block-static, and persistent REPL
storage. The private compiler uses the remaining row stride for each
subscript, typed SSE access at the leaf, scalar assignment conversion, and
matching arithmetic compound assignment. Unevaluated
`sizeof(array[index])` reports the row without running the index. Bounds and
dimension products are checked before allocation.

Depth-one floating pointers keep their pointee width through address
expressions, returns, function and method array parameters, dereference,
subscripting, direct pointer updates, and assignment. Structure and class
objects, object arrays, and object pointers keep scalar floating fields and
one-dimensional fixed floating field arrays. Prefix and postfix updates work
through pointer dereference, fixed-array index, and scalar fields reached from
direct, pointer, or indexed-record members. The address is evaluated once, and
postfix retains the original binary payload. Deeper floating pointers,
pointer-to-array types, and assignment through a pointer-valued floating field
subscript remain unsupported. ADR 0210 records the first array boundary, ADR
0215 records the expanded lvalue model, and ADR 0273 records derived updates.

Fixed `float4` and `double2` arrays with one, two, or three dimensions use
16-byte vector leaves in global, automatic, block-static, and persistent REPL
storage. Outer indexes scale by the complete remaining row or middle slice.
The final vector access uses `MOVUPS` in both directions, so stack alignment
does not affect the result. Plain assignment and the four arithmetic compound
assignments retain the vector type, evaluate every index once, and allow lane
extraction. Row and vector `sizeof` keep their complete sizes without running
an index. Matching vectors support direct
`+`, `-`, `*`, and `/`. Every direct operation keeps the written left value in
the machine destination. MIN and MAX intrinsics keep the written second operand
for NaN and equal signed-zero inputs. A both-NaN ADD or MUL may carry either
input payload, depending on the processor or emulator. Incomplete row
assignment is rejected. SIMD pointers, record fields, allocation with `new`,
array parameters, row values, and call ABI transport remain unsupported. ADR
0216 records the first fixed-array boundary, and ADR 0257 records
multidimensional row descent.

### Arithmetic

Binary operators `+`, `-`, `*`, `/` work on scalar float/double via SSE
scalar opcodes (ADDSS/ADDSD/etc.). Implicit promotion: `int + float -> float`,
`char + int -> int`, `char + float -> float`, and `float + double -> double`.
SIMD types need matching types for `+/-/*//`; mixing scalar and SIMD is a
compile error.

### Casts

`(int)3.7` -> 3 (truncating). `(float)5` -> 5.0. A `char` uses the same
integer-to-floating conversion path. `(double)1.5f` widens. Casts lower to
CVTSI2SS/CVTTSS2SI/CVTSS2SD/etc.

Private CupidC also converts `float` and `double` to an unsigned 32-bit word
for values in C's defined interval. Its lower path truncates directly. Its
upper path subtracts 2^31 and restores bit 31 after truncation. Values outside
`(-1, 2^32)` have no promised result. ADR 0249 records that private path. The
feature-13 guest checks four conversion values around the split, signed and
unsigned `%=` results, and one evaluation of a side-effecting destination. The
required marker is `[feature13-unsigned] PASS conversions=4 remainders=2
once=1`.

### Element access

`v.x / v.y / v.z / v.w` on float4 extracts a scalar float via SHUFPS.
`v.x / v.y` on double2 via SHUFPD.

### Intrinsics

`_mm_*_ps` (17 variants) and `_mm_*_pd` (11 variants) are recognized by name
and inlined as SSE opcodes. See `kernel/cpu/simd_intrin.h` for the full list.

## libm

25 operations (50 symbols with f-variants). Hardware fast-paths for sqrt,
sin, cos, tan, atan, atan2, fabs, floor, ceil, round, trunc, fmod, exp2,
log2. Composite for exp, log, pow, asin, acos, sinh, cosh, tanh, cbrt,
hypot, nextafter.

libm functions use a CupidC-internal ABI that returns results in XMM0 rather
than ST(0), as System V expects. Default GCC-generated kernel C cannot call
them directly. CupidC JIT code calls them through the `BIND_T` table.

## CupidASM opcodes

CupidASM implements about 80 floating-point opcodes: FPU state control
(FXSAVE, FXRSTOR, FINIT, FNINIT, FWAIT, LDMXCSR, STMXCSR), SSE scalar (23:
MOVSS/SD, ADDSS/SD, etc.),
SSE packed (24: MOVAPS/UPS, ADDPS/PD, SHUFPS, CMPPS, etc.), x87 (26: FLD,
FUCOMIP, FSIN, FPATAN, F2XM1, FYL2X, etc.). XMM0-7 and ST0-7 register tokens.

## Testing

- `bin/feature12_float.cc` - scalar float arithmetic, casts, element access.
- `bin/feature13_double.cc` - exact decimal payloads, typed lvalues, calls, and transcendentals.
- `bin/feature13_derived_aot.cc` - external AOT proof for pointer, index, and
  indexed-record floating updates, including one evaluation per designator.
- `bin/feature14_simd.cc` - packed operators, one-dimensional arrays, matrices,
  cubes, NaN behavior, and intrinsics.
- `bin/feature15_libm.cc` - 29 fixed-reference checks, including seven x87 range paths.
- `bin/feature16_asm_fpu.cc` - CupidC inline asm using SSE + x87.
- `bin/fp_drill.cc` - manual #XF provocation (panics kernel).
- `demos/fpu_kernel.asm` - CupidASM FPU state + scalar + packed + x87.
- `demos/simd_blur.asm` - SIMD box blur via MOVUPS/ADDPS/MULPS.

Interactive: boot QEMU graphical, type the command in the shell.

## Stress test

`fpu_context_stress()` is defined in `kernel/cpu/fpu.cc`, but the normal boot
does not call it. The checked CupidC production recipe does not consume host
`CFLAGS`, so the old `-DFPU_STRESS` instruction is no longer a valid
activation path.

## Exception drill

`fp_drill` shell command unmasks DE in MXCSR and divides by zero, verifying
the #XF handler panics with MXCSR dump. Kernel reboot required afterward.
