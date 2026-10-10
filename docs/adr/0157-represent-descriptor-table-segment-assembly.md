# ADR 0157: Represent descriptor-table and segment assembly

## Status

Accepted on 2026-07-28.

## Context

The unchanged `kernel/smp/percpu.c` translation unit installs the kernel GDT,
reloads the code and data segments, and selects each CPU's `%gs` descriptor.
Four GNU assembly forms carry that work:

```c
__asm__ volatile(
    "lgdt %0\n"
    "mov $0x10, %%ax\n"
    "mov %%ax, %%ds\n"
    "mov %%ax, %%es\n"
    "mov %%ax, %%ss\n"
    : : "m"(gdtr) : "ax", "memory");

__asm__ volatile(
    "pushl $0x08\n"
    "pushl $1f\n"
    "lretl\n"
    "1:\n"
    ::: "memory");

__asm__ volatile(
    "lgdt %0\n"
    "mov $0x10, %%ax\n"
    "mov %%ax, %%ds\n"
    "mov %%ax, %%es\n"
    "mov %%ax, %%ss\n"
    "ljmp $0x08, $1f\n"
    "1:\n"
    : : "m"(gdtr) : "ax", "memory");

__asm__ volatile("mov %0, %%gs" : : "r"(gs_sel));
```

The GDTR operand is a packed six-byte object containing a 16-bit limit and a
32-bit base. The `%gs` input is a represented 16-bit selector. The AX
clobber is required by the data-segment templates, and the memory clobbers
cover the descriptor-table transition and the temporary stack words used by
the code-segment reload.

Compiler head already represented smaller privileged-register and
machine-state statements. It stopped on these templates before the complete
translation unit could reach object output. Rewriting the source around that
limit would conceal an active SMP requirement.

## Decision

Compiler-head CupidC accepts the four exact volatile templates above. The
two LGDT forms require one addressable, non-atomic, complete six-byte object
lvalue through `m`, no outputs, and the exact `ax` plus `memory` clobber set.
The far-return form has no operands and requires the exact `memory` clobber.
The `%gs` form requires one represented 16-bit integer through `r` and no
clobbers. Other widths, constraints, output operands, flags, templates, or
clobbers remain outside this slice.

The frontend records the exact template, flags, and operand layout. Linear
IR repeats the full metadata check, then lowers a GDTR input as one address
value and a selector as one two-byte integer value. Calls, pointer
dereferences, and parameters may supply those values when their represented
types still meet the same boundary. Unreachable statements receive the same
metadata validation but emit no unreachable assembly instruction.

The emitter consumes the GDTR address through EAX and encodes `LGDT m48`.
It loads `0x10` into AX, then writes AX to DS, ES, and SS. The selector path
consumes its two-byte value through EAX and writes AX to GS. Every
instruction uses Cupid's shared x86 encoder.

An absolute `pushl $1f` would require a relocation against a compiler-owned
local label. The emitted code reloads CS without that relocation:

```text
PUSH 0x08
CALL trampoline
continuation:
JMP done
trampoline:
RETF
done:
```

`CALL` pushes the continuation address below the selector. `RETF` consumes
both words and resumes at `continuation` with the original ESP. The jump
skips the trampoline on the resumed path. The same sequence replaces the
exact local `ljmp` form. It changes CS to selector `0x08`, preserves the
source-visible stack and registers, and needs no symbol or relocation.

The frontend, IR, and emitter each validate the full template and metadata
at their own boundary. A copied or forged translation unit cannot attach
this emission path to a different assembly statement.

## Evidence

Dedicated frontend, Linear IR, and object selectors cover all four forms,
address and selector evaluation, parameter and call-produced values,
unreachable statements, deterministic repeats, constrained output,
rollback, and same-job recovery. Negative source cases cover a four-byte or
incomplete GDTR, a GDTR rvalue, a register constraint, missing AX or memory
clobbers, a wide selector, and altered selectors or templates. Frozen-unit
mutations cover flags, constraints, operand slices, expression types, and
forged layouts.

The four fixture functions have exact sizes of 30, 21, 46, and 20 bytes.
Their byte-identical ELF32 object is 528 bytes with 117 text bytes, five
sections, five symbols, and no relocations. Shared decoding checks the
48-bit LGDT operand, AX immediate, DS, ES, SS, and GS register lanes, the
local call and jump targets, and RETF.

An unchanged-source selector locks the input bytes, validates both outputs
through the production ELF32 relocatable-object checker, and requires byte
identity. Its two complete compiler-head compiles of
`kernel/smp/percpu.c` produce the same 6,760-byte object with SHA-256
`3c2c6f0e00e5edec1ca16cba91e9fc593d1c42e24f4ebd3591e5f574fb0dd772`.
The 5,175-byte source has SHA-256
`d4b1a87ee6b8efab71a40263e3ed29104855326b35a15de2c253920e35010da7`.

At this point the hosted source locks are:

| Source | Definitions | Statements | Expressions | Block bindings | Initializers |
| --- | ---: | ---: | ---: | ---: | ---: |
| `toolchain/cupidc_frontend.cc` | 379 | 15,330 | 101,044 | 2,305 | 1,420 |
| `toolchain/cupidc_ir.cc` | 231 | 6,794 | 62,847 | 892 | 320 |
| `toolchain/cupidc_emit.cc` | 256 | 6,420 | 55,543 | 788 | 392 |

The deterministic self-host object locks are:

| Source | Functions | Text bytes | Object bytes | Text fingerprint |
| --- | ---: | ---: | ---: | --- |
| `toolchain/cupidc_frontend.cc` | 379 | 783,947 | 926,264 | `B971EDF4` |
| `toolchain/cupidc_ir.cc` | 231 | 446,163 | 477,724 | `7FD888A7` |
| `toolchain/cupidc_emit.cc` | 256 | 412,063 | 445,920 | `8E274177` |

## Rejected alternatives

Changing the packed GDTR to a compiler-friendly scalar was rejected because
LGDT consumes the six-byte architectural structure.

Dropping the AX or memory clobbers was rejected because they describe real
effects in the unchanged source.

Adding general GNU assembly parsing was rejected for this increment. The
compiler can validate these active templates precisely without claiming
unimplemented substitutions, labels, clobbers, or register allocation.

Emitting an absolute local-label address was rejected because the current
ELF path has no local assembly-label relocation model. The relative
call-and-RETF sequence provides the same segment transition without adding
one.

Using private opcode bytes was rejected because Cupid's shared x86 model
already represents LGDT, segment-register moves, calls, jumps, and RETF.

## Consequences

Compiler head emits unchanged `kernel/smp/percpu.c` completely. The checked
bootstrap seed predates this capability, so the production recipe remains
host-owned and the source keeps its `.c` suffix. A checked seed refresh and
normal-build proof are required before ownership can move.

No ABI, runtime path, production object, or host dependency changes in this
increment. `TempleOS/` remains untouched reference material. The updated
CTXT page changes delivered help text when the normal image is rebuilt.
