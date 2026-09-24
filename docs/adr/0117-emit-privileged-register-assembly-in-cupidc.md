# Emit privileged register assembly in CupidC

- Status: Accepted
- Date: 2026-07-26

## Context

Three strict kernel roots stop on small GNU assembly forms that move values
between C and i386 control registers or read a model-specific register:

- `kernel/cpu/idt.c` reads CR2.
- `kernel/mm/paging.c` reads and writes CR0, and writes CR3.
- `kernel/smp/lapic.c` uses RDMSR with ECX as the selector and EDX:EAX as the
  result.

The source already states the required register and ordering contracts.
Replacing these helpers, removing the assembly, or passing the template to a
host assembler would hide a CupidC gap. The shared frontend, Linear IR, and
ELF32 emitter need to represent the source as written.

The same exact assembly family includes the CR4 reads and writes used by the
separate floating-point and SIMD roots. This decision represents those forms,
while this increment's strict-root probes stay with the three roots above.

The existing input constraint path was limited to fixed accumulator and DX
operands used by port I/O. It did not represent an independent general
register input or a fixed ECX input. The emitter also routed every independent
input toward the port-I/O templates, so control-register and RDMSR statements
could not reach a suitable target path.

## Decision

GNU-mode function assembly accepts two more independent input constraints:

- `r` accepts a represented four-byte integer or a represented data or
  `void` pointer. Function pointers and other widths are rejected.
- `c` accepts a represented four-byte integer and reserves ECX.

The existing `a` and `d` constraints keep their width-aware port-I/O
semantics. Fixed EBX input and `q` remain outside this slice. A fixed-register
collision between an input and an output is still an error.

The exact supported templates are:

- `mov %%cr0, %0`, `mov %%cr2, %0`, and `mov %%cr4, %0` with one four-byte
  integer `=r` output
- `mov %0, %%cr0`, `mov %0, %%cr3`, and `mov %0, %%cr4` with one four-byte
  integer or data-pointer `r` input
- `rdmsr` with four-byte integer `=a` and `=d` outputs followed by one
  four-byte integer `c` input

Each statement is volatile. A control-register write may carry the source's
single `memory` clobber. The read and RDMSR forms do not accept clobbers in
this boundary.

Linear IR keeps the source operand types and consumes output addresses before
input values. Every operand is evaluated once and in source order. Whole-unit
validation checks the constraint, value category, width, type, output
partition, and fixed-register collisions before lowering.

The i386 emitter recognizes these templates before its port-I/O path. It
emits the control-register moves and RDMSR directly through checked target
bytes. Control-register reads use ECX as a short-lived result before storing
through the output address. Writes pop their input into EAX. RDMSR pops the
selector into ECX, executes RDMSR, then stores EDX and EAX through the two
output addresses. These forms need no private assembly frame and do not use
EBX.

Frontend, IR, and object validation remain transactional. Invalid source or
forged frozen metadata leaves the input unchanged, publishes no partial
object, and permits a valid operation in the same job.

## Rejected alternatives

Calling a host assembler was rejected. It would add a hidden normal-toolchain
dependency to CupidC object emission.

Rewriting the kernel helpers as intrinsics was rejected. The current assembly
expresses the ABI boundary clearly and is shared with the host build.

Treating every `r` operand as an integer was rejected. Active CR3 source passes
a data pointer, and a represented i386 data pointer is a valid four-byte
general-register value. Function pointers are kept out because no active
privileged template needs them.

Routing the new forms through the existing port-I/O branch was rejected. That
branch interprets any independent input as a port and cannot preserve the
control-register or RDMSR contracts.

Adding general GNU assembly was rejected for this increment. Exact template
recognition keeps diagnostics, register allocation, and target bytes bounded
to active source requirements.

## Consequences and evidence

Public `privileged-register-assembly` selectors cover the frontend, Linear
IR, and ELF32 object boundaries. Positive cases include all six
control-register moves and RDMSR. Negative cases cover bad widths and types,
function-pointer `r`, pointer `c`, fixed-register collisions, fixed EBX, `q`,
unsupported control-register directions, malformed clobbers, swapped RDMSR
outputs, rollback, and same-job recovery.

The exact object fixture contains seven functions in 199 text bytes, eight
symbols including the null symbol, five sections, no relocations, and text
fingerprint `A3095253`. Shared decoding finds `0F 20 C1`, `0F 20 D1`,
`0F 20 E1`, `0F 22 C0`, `0F 22 D8`, `0F 22 E0`, and `0F 32`. It also checks
the surrounding loads and stores, proves that EBX is absent, and requires a
byte-identical second emission. The contract decodes these privileged
instructions but does not execute them.

Compiler head compiles the three unchanged roots twice under the complete
`KERNEL_I386` profile. Every result is a validated, byte-identical i386
ELF32 relocatable object:

| Source | Object bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/cpu/idt.c` | 8,756 | `0ad16fd3250bc09ced7c928cb287123db245980de73c15f0249db71a2f2f6ea3` |
| `kernel/mm/paging.c` | 2,336 | `fc9b757a35cf474f90436333ba732be252253feeea531cad851215e17f793e2d` |
| `kernel/smp/lapic.c` | 4,184 | `6ce344d265ad3fb6b221a9159d860954c5f5512a7eac526838e69bc181a4c045` |

The complete hosted Toolchain contract passes with the new selectors. Exact
self-source locks now record 204 functions for `cupidc_ir.c`, 209 for
`cupidc_emit.c`, and 322 for `cupidc_frontend.c`.

ADR 0122 moves the new input constraints and templates into the checked seed.
The three roots remain host-owned `.c` files, and the 136-source production
boundary does not move. Production recipe transfer, `.cc` renames, an image
build, and runtime proof remain separate work. WRMSR, CR1, CR2 writes,
arbitrary control-register forms, general clobbers, fixed EBX and `q` inputs,
and broader GNU assembly are still unsupported.
