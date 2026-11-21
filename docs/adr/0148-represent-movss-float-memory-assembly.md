# ADR 0148: Represent MOVSS float memory assembly

## Status

Accepted on 2026-07-28.

## Context

The unchanged `fpu_boot_smoke()` body in `kernel/cpu/fpu.c` checks that an
SSE register survives a scheduler yield. It uses all three memory forms that
the check needs:

```c
__asm__ volatile(
    "movss %1, %%xmm0\n\t"
    "movss %%xmm0, %0\n\t"
    : "=m"(readback)
    : "m"(probe)
    : "xmm0");

__asm__ volatile("movss %0, %%xmm0" : : "m"(marker) : "xmm0");
__asm__ volatile("movss %%xmm0, %0" : "=m"(readback) : : "xmm0");
```

The output and input expressions name `float` objects. Their addresses, not
their C values, belong in the assembly operand. CupidC previously restricted
memory operands to integers and did not represent the `xmm0` clobber.

Changing the smoke test to move bit-cast integers or pass pointer registers
would hide both gaps in the operating system source. MOVSS already expresses
the intended scalar transfer directly, and Cupid's x86 model already owns its
load and store encodings.

## Decision

Compiler-head CupidC accepts the exact volatile MOVSS round trip and its two
one-way forms shown above. Each statement must list `xmm0` exactly once and
must not add a `memory` clobber. A `=m` output must be a modifiable,
non-atomic `float` lvalue. An `m` input must be an addressable, non-atomic
`float` lvalue and may be qualified `const` or `volatile`. Other templates,
constraints, scalar widths, rvalues, bit fields, register objects, and atomic
objects remain outside this slice.

The frontend publishes `CTOOL_C_ASSEMBLY_XMM0_CLOBBER` and keeps the typed
operand expressions in the immutable assembly slice. Linear IR evaluates
output addresses before input addresses, once each and in source order. The
round trip therefore leaves both addresses on the evaluation stack. Emission
pops the input address into EAX for `MOVSS XMM0, [EAX]`, then pops the output
address for `MOVSS [EAX], XMM0`. The one-way forms use the same paths without
allocating a frame temporary.

The frontend, IR, and emitter each repeat the exact template, flag, operand
count, constraint, type, layout, and qualification checks at their trust
boundary. A forged frozen unit cannot use the new clobber flag to reach a
different assembly template.

## Evidence

Dedicated frontend, Linear IR, and object selectors cover the round trip,
both one-way forms, indirect operands, one-time expression evaluation,
unreachable statements, immutable metadata, deterministic repeats,
constrained output, rollback, and same-job recovery. Negative cases cover
`double`, `const` output, rvalues, atomic operands, missing or extra
clobbers, wrong constraints, altered templates, and forged layouts.

The object selector fixes these function sizes:

| Function | Bytes | MOVSS instructions |
| --- | ---: | --- |
| `round_trip` | 37 | `F3 0F 10 00`, then `F3 0F 11 00` |
| `load_xmm0` | 21 | `F3 0F 10 00` |
| `store_xmm0` | 21 | `F3 0F 11 00` |

The complete deterministic ELF32 object is 464 bytes. It has 79 bytes of
text, five sections, four symbols, and no relocations. Shared decoding checks
XMM0 and a 32-bit `[EAX]` memory operand with no index, segment override, or
displacement.

The first object check assumed that the decoder would assign a 32-bit width
to the XMM register operand. Cupid's model leaves the register class width
unspecified and assigns 32 bits to the scalar memory operand. The check was
corrected to match that public model while keeping every expected byte fixed.

An exact `KERNEL_I386` command compiles the unchanged `kernel/cpu/fpu.c`
source twice. Both attempts pass all three MOVSS forms, publish no partial
object, and stop at the next unsupported statement:

```text
/kernel/cpu/fpu.c:113:5: error CTB00000F: GNU inline assembly m input template is outside this slice
```

The complete Cupid-built fixed-point check rebuilds all 19 C objects and all
five static tools, then reproduces them with the resulting CupidC. Every
stage-two and stage-three object and executable matches.

## Rejected alternatives

Moving the values through integer lvalues was rejected because it would make
valid FPU source harder to read and would avoid the missing float-memory
semantics.

Passing pointers through general registers was rejected because the active
source correctly asks GNU assembly to select a memory operand.

Treating `m` and `xmm0` as general assembly features was rejected because the
emitter cannot yet substitute arbitrary operands or preserve arbitrary
clobbers.

Using a private byte encoder was rejected because the shared x86 model already
owns both MOVSS encodings and their decoder contract.

## Consequences

Compiler head now represents `fpu_boot_smoke()` through its MOVSS scheduler
check without changing `kernel/cpu/fpu.c`. The next exact frontier is the
`fldl`, `fsin`, and `fstpl` block in `stress_sin()` at line 113. That block
uses a `double` memory input and output.

The checked bootstrap seed does not carry this capability. The FPU root
therefore stays host-built and keeps its `.c` name. This decision changes no
production code owner, ABI, runtime code path, or host dependency. The
updated CTXT pages change delivered help text when the normal image is
rebuilt.
