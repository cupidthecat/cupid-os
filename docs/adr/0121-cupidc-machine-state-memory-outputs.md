# Emit machine-state snapshots through GNU memory outputs

- Status: Accepted
- Date: 2026-07-26

## Context

The FPU panic path records three pieces of processor state before it reports
the fault:

```c
__asm__ volatile("fnstsw %0" : "=m"(fsw));
__asm__ volatile("fnstcw %0" : "=m"(fcw));
__asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
```

CupidC could already emit these instructions through the shared x86 model,
but GNU assembly outputs were limited to register constraints. The frontend
rejected `=m` before the instruction reached Linear IR. Replacing the
snapshots with helper calls or integer register temporaries would change
active source to work around the compiler.

GNU memory operands cover a much larger language than these three statements.
This work needs one output address, fixed instruction widths, and no input or
clobber list. It does not need general address substitution, matching memory
inputs, read/write memory operands, or an embedded host assembler.

## Decision

In GNU mode, CupidC accepts `=m` for a modifiable, non-atomic 16-bit or 32-bit
integer lvalue. That constraint is usable only with these exact volatile
templates:

- `fnstsw %0` with one 16-bit output;
- `fnstcw %0` with one 16-bit output; and
- `stmxcsr %0` with one 32-bit output.

Each statement must have one output, no input, and no clobber. The frontend
publishes the original operand type, expression, constraint, template, and
source locations. It rejects a width mismatch, a nonvolatile statement, an
extra input, or `=m` attached to any other template.

Linear IR validates the frozen record again. It evaluates the output lvalue
once and leaves its address directly below the `ASSEMBLY` instruction. No
synthetic value or register output is introduced. The state-memory path has a
maximum abstract stack depth of one.

The i386 emitter consumes that address into EAX and asks the shared x86 model
to encode `FNSTSW word [EAX]`, `FNSTCW word [EAX]`, or
`STMXCSR dword [EAX]`. The instruction writes the destination directly, so
the function needs no assembly-output staging slot. Invalid public metadata
fails before partial object output is published.

## Rejected alternatives

Changing the panic code to collect the values through general registers was
rejected. The active source asks for memory destinations, and the instruction
width is part of that contract.

Adding wrapper functions was rejected. A call would change the fault-reporting
path and leave the same machine instructions to another compiler boundary.

Passing the template to GCC, Clang, or a standalone assembler was rejected.
CupidC already owns instruction selection and ELF32 emission through the
shared x86 catalogue.

Accepting arbitrary `=m` templates was rejected. General GNU memory operands
need a broader substitution and allocation model. An exact supported slice
is safer than accepting syntax whose memory effects are not represented.

## Consequences and evidence

Frontend, Linear IR, and object selectors cover all three accepted forms.
Their negative cases include byte and eight-byte outputs, both instruction
width mismatches, an unrelated template, missing volatility, an input in
place of the output, a forbidden memory clobber, forged constraints, forged
layouts, altered flags, and same-job recovery.
The object selector also pins the three unchanged statements in
`kernel/core/panic.c`.

The deterministic object fixture is 448 bytes with 58 bytes of `.text`, five
sections, four symbols, and no relocations. Its three functions are emitted
twice and compared byte for byte. The decoder finds exactly one correctly
sized memory instruction in each function and confirms an EAX base with no
index or displacement.

The new IR validation initially rejected operand-free assembly because an
empty operand table is legitimately null. The existing operand-free IR and
object selectors caught the regression. The validator now keeps that valid
empty-table case while still requiring storage for a state-memory output.

An isolated compiler-head probe first processed the unchanged
`kernel/core/panic.c` snapshots under the complete `KERNEL_I386` profile and
stopped at the later `call 1f` local-label template on line 193. Combining
the call-next work removes that blocker. Two full compiles now produce the
same validated 10,212-byte ELF32 object with SHA-256
`84daa51a65d6970ae7a7918b05fe64b7676c39d3309264375e349cf0ae20d428`.
The unchanged `kernel/cpu/fpu.c` source still stops at
`target("general-regs-only")`, before its `stmxcsr` statement.

ADR 0122 moves the capability into the checked seed. No normal-build object
changes owner, and no `.c` source is renamed. An OS image or boot result does
not exercise these new bytes until the production transfer.
