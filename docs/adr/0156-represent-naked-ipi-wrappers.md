# ADR 0156: Represent naked IPI wrappers

## Status

Accepted on 2026-07-28.

## Context

`kernel/smp/smp.c` defines three interrupt entry points with GNU `naked`:

```c
__attribute__((naked))
static void ipi_reschedule_stub(void) {
    __asm__ volatile(
        "pushal\n"
        "call ipi_reschedule_c\n"
        "popal\n"
        "iret\n"
    );
}
```

The call IPI uses the same wrapper around `ipi_call_c`. The panic IPI disables
interrupts and stays in a halt loop:

```text
cli
1: hlt
jmp 1b
```

These functions own their complete machine entry and exit sequences. A normal
C prologue would change the interrupt frame, and a normal return would execute
`RET` instead of `IRET`. Compiler head previously rejected `naked` before it
could represent either body.

Moving the wrappers to a separate assembly file would hide a requirement that
the active C source already states clearly. Passing the templates to a host
assembler would also leave a conventional assembler inside CupidC's object
path.

## Decision

Compiler-head CupidC accepts `naked` and `__naked__` on a canonical file-scope
function. Compatible declarations merge the fact into that function. The
function must have the nonvariadic type `void (void)` and a definition with
exactly one top-level represented assembly statement. CupidC rejects objects,
typedefs, parameters, block declarations, non-void functions, functions with
parameters, and bodies that ask the compiler to generate other work.

The represented wrapper form is exactly:

```text
pushal
call <C identifier>
popal
iret
```

It must be a basic or volatile assembly statement with no operands or
clobbers. The call target must name a declared file-scope function. The
frontend resolves that identifier to its canonical binding and stores a
one-based binding reference in the immutable assembly record. The one-based
form leaves zero as the existing no-callee value for zero-initialized public
fixtures.

The represented panic form is exactly:

```text
cli
1: hlt
jmp 1b
```

It has the same statement-shape restrictions and carries no direct-call
binding. Labels and calls remain unsupported outside these two complete
templates.

Linear IR requires the naked body to lower to one assembly instruction
followed by its structural `RETURN_VOID`. It requires zero automatic stack
storage and checks the signature, canonical attribute mask, exact template,
and direct-call metadata again. Ordinary functions cannot use either
control-transfer template.

The i386 emitter omits the compiler prologue, local reservation, synthetic
return, and epilogue for this path. Cupid's shared x86 model emits the wrapper
as:

```text
60 E8 FC FF FF FF 61 CF
```

The `CALL` displacement owns one `.text` `R_386_PC32` relocation with addend
`-4`. The panic entry emits:

```text
FA F4 E9 FA FF FF FF
```

That relative jump returns to the `HLT`. The frontend, Linear IR, and emitter
each validate the frozen function and assembly metadata before accepting the
special placement.

## Evidence

Dedicated frontend tests cover both attribute spellings, compatible
redeclarations, the two wrapper targets, and the panic loop. Negative cases
cover arguments, invalid declaration sites, undeclared or nonfunction call
targets, changed templates, compiler-managed statements, an invalid
signature, forged attribute placement, mismatched call metadata, rollback,
and same-job recovery.

The IR contract fixes each naked function to one assembly instruction and one
structural void return with no stack reservation. The object contract checks
the exact eight-byte wrapper and seven-byte panic images through the shared
x86 decoder. It also checks the local wrapper symbols, the undefined global
handler, and the single direct-call relocation at wrapper offset 2.

Two full `KERNEL_I386` compiles of unchanged `kernel/smp/smp.c` produce
byte-identical, validated ELF32 relocatable objects. Each object is 8,444
bytes and has SHA-256
`806509a6dd1ac7eb34b7ffcb67a1f8852950663a274145584d0260da76dcba54`.

The focused frontend, Linear IR, exact-object, and unchanged-source tests pass.
The hosted source gates, self-host object locks, and generated bootstrap audit
also pass with the added capability.

## Rejected alternatives

Moving the entry points to an out-of-line assembly file was rejected because
the existing source describes the ABI boundary directly and should remain the
language requirement.

Sending the template text to a host assembler was rejected because CupidC
must own the emitted object and relocation.

Hard-coding `ipi_reschedule_c` and `ipi_call_c` in the emitter was rejected.
Resolving the source identifier gives the exact active form a typed,
canonical callee without making handler names part of the compiler.

Accepting arbitrary naked C bodies was rejected because any hidden
compiler-generated instruction can corrupt an interrupt entry frame. New
naked forms need an explicit body, stack, and control-transfer contract.

## Consequences

Compiler head can compile unchanged `kernel/smp/smp.c`. The checked bootstrap
seed does not carry this capability, so the normal SMP recipe remains
host-built and the source keeps its `.c` name. This decision changes no
production owner, normal build recipe, runtime path, ABI, or host-dependency
count.

Broader naked functions, arbitrary labeled assembly, general direct calls in
assembly, clobber lists, compiler-managed locals, and other interrupt entry
shapes remain open. A later checked-seed promotion must repeat object,
production-build, and SMP runtime proof before ownership or the source suffix
can move.
