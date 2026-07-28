# ADR 0146: Represent LDMXCSR memory inputs

## Status

Accepted on 2026-07-28.

## Context

The unchanged `fpu_init_cpu()` body in `kernel/cpu/fpu.c` loads the initial
MXCSR value with this GNU assembly statement:

```c
__asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
```

The operand is a memory input. Its C expression names the object that the CPU
reads, so the compiler must retain an address rather than apply the ordinary
lvalue conversion and pass the object's value. CupidC previously rejected
the independent `m` constraint before it reached template validation.

Changing the statement to use a pointer register would only move the compiler
gap into the operating system source. The active form is a direct and useful
GNU C contract, and Cupid's x86 model already knows the LDMXCSR instruction.

## Decision

Compiler-head CupidC accepts an independent `m` input only for the exact
volatile `ldmxcsr %0` template. The statement has no outputs or clobbers and
has exactly one input. That input must be an addressable, non-atomic 32-bit
integer lvalue. Ordinary, `const`, and `volatile` objects are valid. Bit
fields, register objects, rvalues, atomic objects, other widths, and `m`
inputs on other templates remain rejected.

The frontend keeps the input expression as an lvalue and records its type in
the immutable assembly operand. Linear IR evaluates the address once, even
when the lvalue comes through an indirect expression or appears in
unreachable code. The IR validator checks the exact template, flags, operand
slice, integer layout, and atomic qualification.

The i386 emitter pops the evaluated address into EAX and asks the shared x86
model to encode LDMXCSR at `[EAX]`. The resulting instruction is exactly
`0F AE 10`. It needs no temporary frame slot, relocation, private encoder, or
host assembler. Object emission repeats the frozen metadata checks before it
writes any bytes.

## Evidence

Dedicated frontend, Linear IR, and object selectors cover local and indirect
qualified objects, one-time expression evaluation, unreachable statements,
exact public metadata, deterministic repeats, malformed frozen records,
transactional failure, rollback, and same-job recovery. Negative frontend
cases cover wrong widths, rvalues, bit fields, register and atomic objects,
the wrong constraint, an extra clobber, and an unrelated template.

The object fixture emits two 20-byte functions. Each contains the exact
`0F AE 10` instruction with EAX as its base and no index, segment override,
or displacement. The complete deterministic ELF32 object is 400 bytes, has
40 bytes of text, five sections, three symbols, and no relocations.
The hosted test decodes the instruction instead of executing it, so the
contract does not change the test process's floating-point control state.

An exact `KERNEL_I386` command compiles the unchanged `kernel/cpu/fpu.c`
source twice. Both attempts pass the line 28 LDMXCSR statement, publish no
partial object, and stop at line 63 on the floating `=m` output in the later
multiline MOVSS round trip.

## Rejected alternatives

Loading the value through a general register was rejected because LDMXCSR
takes a memory operand.

Rewriting `fpu_init_cpu()` to pass a pointer was rejected because it would
change valid operating system source to work around a compiler limitation.

Treating `m` as a general assembly constraint was rejected because the
emitter does not yet implement arbitrary memory operand substitution.

Using a private byte encoder was rejected because the shared x86 model already
owns LDMXCSR and its memory encoding.

## Consequences

Compiler head now represents the complete `fpu_init_cpu()` body without
changing that source. The next exact frontier in `kernel/cpu/fpu.c` is the
floating `=m` output at line 63, followed by its floating `m` input and XMM0
clobber.

The checked bootstrap seed does not carry this capability. The FPU root
therefore stays host-built and keeps its `.c` name. This decision changes no
production object, image, ABI, runtime path, source owner, or host dependency.
