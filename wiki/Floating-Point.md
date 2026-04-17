# Floating Point in CupidOS

CupidOS supports hardware x87 + SSE/SSE2 floating point end-to-end: kernel,
language, assembler, libm, printf.

## Overview

- **Build**: `-mfpmath=sse -msse -msse2 -mstackrealign` in CFLAGS.
- **Init**: `fpu_init()` is the first call in `kmain`. Sets CR0.EM=0/MP=1/NE=1/TS=0
  and CR4.OSFXSR=1/OSXMMEXCPT=1, runs FNINIT, loads MXCSR=0x1F80.
- **Context switch**: `context_switch.asm` FXSAVEs the outgoing PCB's
  `fp_state[512]` and FXRSTORs the incoming task's. Eager: no CR0.TS games.
- **PCB**: `process_t.fp_state` is 512 bytes at 16-byte alignment. Offset 80.
- **Exceptions**: IDT vectors 7 (#NM), 16 (#MF), 19 (#XF) route to
  `fpu_nm_handler`/`fpu_mf_handler`/`fpu_xf_handler`, which call `panic_fpu`
  with a serial dump of FSW/FCW/MXCSR. Should never fire under masked
  MXCSR + eager switch.

## CupidC FP types

- `float`   - 4 bytes, SSE scalar.
- `double`  - 8 bytes, SSE scalar.
- `float4`  - 16 bytes, SSE packed (4 floats).
- `double2` - 16 bytes, SSE packed (2 doubles).

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
and inlined as SSE opcodes. See `kernel/simd_intrin.h` for the full list.

## libm

25 operations (50 symbols with f-variants). Hardware fast-paths for sqrt,
sin, cos, tan, atan, atan2, fabs, floor, ceil, round, trunc, fmod, exp2,
log2. Composite for exp, log, pow, asin, acos, sinh, cosh, tanh, cbrt,
hypot, nextafter.

**Important**: libm functions use a CupidC-internal ABI (result returned in
XMM0, not ST(0) as System V would expect). Calling them from plain kernel
C will NOT work with default GCC code. They are intended to be called from
CupidC JIT'd code via the BIND_T table.

## CupidASM opcodes

Phase B adds ~80 new opcodes: FPU state control (FXSAVE, FXRSTOR, FINIT,
FNINIT, FWAIT, LDMXCSR, STMXCSR), SSE scalar (23: MOVSS/SD, ADDSS/SD, etc.),
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

`fpu_context_stress()` in `kernel/fpu.c` spawns 8 threads each running 100k
sin() loops, compares against a serial reference. Gated by `-DFPU_STRESS`.
To run: add `-DFPU_STRESS` to `CFLAGS`, rebuild, boot. Panics on corruption.

## Exception drill

`fp_drill` shell command unmasks DE in MXCSR and divides by zero, verifying
the #XF handler panics with MXCSR dump. Kernel reboot required afterward.
