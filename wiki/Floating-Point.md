# Floating Point in CupidOS

CupidOS supports x87 and SSE/SSE2 floating point in the kernel, CupidC,
CupidASM, libm, and `printf`.

## Overview

- **Build**: `-mfpmath=sse -msse -msse2 -mstackrealign` in CFLAGS.
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
Unchanged `kernel/cpu/fpu.c` now produces a deterministic checked-seed
object. Its production recipe remains host-owned until the separate
ownership and runtime transfer passes.

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
unchanged helper produce the same 420-byte object. The complete
`kernel/core/string.c` file still stops at its independent
double-to-`uint64_t` cast on line 190, so this compiler-head increment does
not move production ownership.

The normal build now compiles `kernel/gfx/jpeg.cc` and
`kernel/gfx/glyph_raster.cc` with that seed. JPEG exercises exact static
floating data, while glyph rasterization exercises the comparison path. The
strong guest gate checks the TrueType path and every pixel of a byte-fixed
baseline JPEG. ADR 0139 records the production transfer.

Direct floating truth, a floating controlling expression, increment or
decrement, hexadecimal or subnormal constants, `long double`, SIMD, and
atomic floating access remain unsupported. The SSE details below describe the
private in-kernel compiler.

### Arithmetic

Binary operators `+`, `-`, `*`, `/` work on scalar float/double via SSE
scalar opcodes (ADDSS/ADDSD/etc.). Implicit promotion: `int + float -> float`,
`float + double -> double`. SIMD types need matching types for `+/-/*//`;
mixing scalar and SIMD is a compile error.

### Casts

`(int)3.7` -> 3 (truncating). `(float)5` -> 5.0. `(double)1.5f` widens.
Casts lower to CVTSI2SS/CVTTSS2SI/CVTSS2SD/etc.

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
SSE packed (24: MOVAPS/UPS, ADDPS/PD, SHUFPS, CMPPS, etc.), x87 (25: FLD,
FSIN, FPATAN, F2XM1, FYL2X, etc.). XMM0-7 and ST0-7 register tokens.

## Testing

- `bin/feature12_float.cc` - scalar float arithmetic, casts, element access.
- `bin/feature13_double.cc` - double + transcendentals.
- `bin/feature14_simd.cc` - float4/double2 + intrinsics.
- `bin/feature15_libm.cc` - cycle 8 functions x 7 inputs vs glibc reference.
- `bin/feature16_asm_fpu.cc` - CupidC inline asm using SSE + x87.
- `bin/fp_drill.cc` - manual #XF provocation (panics kernel).
- `demos/fpu_kernel.asm` - CupidASM FPU state + scalar + packed + x87.
- `demos/simd_blur.asm` - SIMD box blur via MOVUPS/ADDPS/MULPS.

Interactive: boot QEMU graphical, type the command in the shell.

## Stress test

`fpu_context_stress()` in `kernel/cpu/fpu.c` spawns 8 threads each running 100k
sin() loops, compares against a serial reference. Gated by `-DFPU_STRESS`.
To run: add `-DFPU_STRESS` to `CFLAGS`, rebuild, boot. Panics on corruption.

## Exception drill

`fp_drill` shell command unmasks DE in MXCSR and divides by zero, verifying
the #XF handler panics with MXCSR dump. Kernel reboot required afterward.
