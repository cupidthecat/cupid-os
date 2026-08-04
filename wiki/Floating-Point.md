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
path. A mixed floating comparison uses `double`; only `!=` is true for an
unordered NaN input.

Non-atomic `long double` values now use twelve-byte target objects. Automatic
values use frame snapshots. Static-duration scalars, fixed arrays, and
complete records may contain long-double leaves. Implicit initialization
zeros the complete object; an explicit leaf accepts an integer constant
expression equal to zero. Each leaf contributes twelve zero-filled BSS bytes,
and atomic leaves fail recursively without following pointers. Bounded finite
normal decimal `L` tokens round an exact ratio to a 64-bit explicit
significand with ties to even. The emitter writes that significand and the
positive token's biased exponent into a twelve-byte snapshot; unary minus
supplies the sign. The value-bearing ten bytes move through x87 80-bit `FLD`
and `FSTP` memory forms in Cupid's shared x86
catalogue. The hosted path converts among
`float`, `double`, and `long double`, applies unary plus and minus, and
evaluates addition, subtraction, multiplication, and division. Direct and
indirect fixed, variadic, and unprototyped arguments occupy twelve cdecl
bytes. Functions return the value in x87 `ST0`, and direct or indirect callers
store it in a twelve-byte snapshot. `va_arg(long double)` copies twelve bytes
and leaves the cursor at the following four-byte slot. All six comparisons
accept matching long-double values and mixed `float` or `double` inputs. The
emitter loads right then left, executes `FUCOMIP ST0, ST1`, and discards the
surviving x87 value. Signed zeros compare equal, and only `!=` is true for an
unordered input. Hexadecimal or subnormal long-double literals, decimal
ratios beyond the bounded parser, nonzero and floating static initializers,
and integer conversions
involving `long double` remain open.

The static aggregate proof covers two 24-byte arrays and two 28-byte records.
They occupy 104 BSS bytes, and their 415-byte access function has fingerprint
`BF01CC71`, eight absolute relocations, and six symbols. The hosted i386
runtime checks the initial zero state and then moves 1.5 through file and
block members.

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
`while`, `do`, and `for`, and conversion to `_Bool`. Both signed zeros are false; finite nonzero values,
subnormals, infinities, and NaNs are true. Increment or decrement,
hexadecimal floating constants, binary32 and binary64 subnormal constants,
hexadecimal or subnormal long-double literals, decimal ratios beyond the
bounded parser, nonzero or floating static long-double initializers, integer conversions involving
`long double` other than `_Bool`, general SIMD value semantics, and atomic
floating access remain unsupported. Twelve-byte direct
and indirect fixed, variadic, and unprototyped arguments, function returns,
direct and indirect call results, and `va_arg(long double)` use the represented
automatic `long double` path. The exact production SIMD assembly forms above
are a narrower checked path. The SSE details below describe the private
in-kernel compiler.

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
one-dimensional fixed floating field arrays. Deeper floating pointers,
indirect floating updates, pointer-to-array types, and
assignment through a pointer-valued floating field subscript remain
unsupported. ADR 0210 records
the first array boundary, and ADR 0215 records the expanded lvalue model.

One-dimensional fixed `float4` and `double2` arrays use 16-byte elements in
global, automatic, block-static, and persistent REPL storage. Indexed access
uses `MOVUPS` in both directions, so stack alignment does not affect the
result. Plain assignment and the four arithmetic compound assignments retain
the vector type and allow lane extraction. Matching vectors support direct
`+`, `-`, `*`, and `/`. Every direct operation keeps the written left value in
the machine destination. MIN and MAX intrinsics keep the written second operand
for NaN and equal signed-zero inputs. A both-NaN ADD or MUL may carry either
input payload, depending on the processor or emulator. SIMD pointers,
multidimensional arrays, record fields, allocation with `new`, array
parameters, and call ABI transport remain unsupported. ADR 0216 records this
boundary.

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
- `bin/feature14_simd.cc` - float4/double2 + intrinsics.
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
