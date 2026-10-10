# ADR 0175: Represent kernel-entry BSS-clear assembly

## Status

Accepted on 2026-07-29.

## Context

The unchanged `_start()` definition in `kernel/core/kernel.c` establishes the
fixed kernel stack and clears BSS before calling `kmain()`:

```c
__asm__ volatile(
    "mov $0xF00000, %%esp\n"
    "mov %%esp, %%ebp\n"
    "mov $_bss_start, %%edi\n"
    "mov $_kernel_end, %%ecx\n"
    "sub %%edi, %%ecx\n"
    "shr $2, %%ecx\n"
    "xor %%eax, %%eax\n"
    "cld\n"
    "rep stosl\n"
    ::: "eax", "ecx", "edi", "memory");
```

The statement has no operands. Its two linker symbols need absolute
relocations, and its clobber list names three 32-bit registers plus memory.
It also replaces the compiler-created frame by assigning ESP and EBP.
Smaller operand-free statements were already supported, but this entry
sequence exposed missing register clobbers, symbol immediates, stack-state
tracking, and entry-return behavior.

## Decision

CupidC recognizes this sequence only as the direct first statement of the
externally linked, prototyped `void _start(void)` definition in
`.text.start`. The function cannot have a compiler-managed local frame. The
statement must have no operands, use the exact `eax`, `ecx`, `edi`, and
`memory` clobber set, and resolve `_bss_start` and `_kernel_end` as visible
external object declarations.

The public assembly record has separate EAX, ECX, and EDI clobber bits. The
existing `cc` bit keeps its `0x20` identity. The new bits use `0x40`, `0x80`,
and `0x100`.

The frontend tracks statement nesting explicitly. A first statement hidden
inside an `if`, label, or nested compound is not an entry reset. Linear IR
then proves that the assembly node is the first direct child of the outer
function body, has depth zero, and is the first effect at an empty abstract
stack. It repeats the complete template,
binding, operand, clobber, linkage, visibility, prototype, and section
checks before publishing an assembly instruction.

The i386 emitter installs the stack, loads `_bss_start` into EDI and
`_kernel_end` into ECX, derives the doubleword count, clears EAX, and emits
`CLD` plus `REP STOSD` through the shared x86 model. The symbol loads produce
`R_386_32` relocations with addend zero.

Ordinary post-prologue calls use stack-base residue eight. Calls after this
entry reset use residue zero, so the following `kmain()` call needs no
padding. If `kmain()` returns, `_start` disables interrupts and enters a
`HLT` loop. It cannot use `leave; ret` because the assembly deliberately
discarded the incoming frame.

## Evidence

The exact fixture is 42 bytes: a three-byte prologue, the 27-byte reset and
clear sequence, a five-byte `kmain()` call, and a seven-byte terminal loop.
The `_bss_start` and `_kernel_end` relocations are at offsets 11 and 16 with
zero addends. The `kmain` `R_386_PC32` relocation is at offset 31 with addend
minus four. CupidDis decodes the final bytes as `CLI`, `HLT`, and a branch
back to `HLT`.

Frontend, IR, and object negatives cover the wrong function, old-style or
wrong prototypes, leading statements, label-wrapped statements, nested `if`
and compound bodies,
forged statement references, altered bindings, visibility and linkage
changes, missing or duplicate clobbers, operands, compiler-managed frames,
rollback, and same-job recovery. The object contract also forges valid
assembly metadata onto a nested node and confirms that both IR and object
lowering reject it.

The unchanged 31,172-byte `kernel/core/kernel.c` source contains 950 newline
bytes and has SHA-256
`fcc92bb561ed107ec6b328f5e9502f1040a2fedd9cf573f6876e5b93556945c3`.
Two runs of the Cupid-built `cupidc-cupidc.elf` produce the same
25,920-byte ELF32 object with SHA-256
`d44d06949d48ead865d0d8c1bdd3b76a67b429e0b7a369318ec4fbe8d9f44ed7`.

The combined hosted source records are:

| Source | Definitions | Statements | Expressions | Block bindings | Initializers |
| --- | ---: | ---: | ---: | ---: | ---: |
| `toolchain/cupidc_ir.cc` | 258 | 7,127 | 66,562 | 938 | 344 |
| `toolchain/cupidc_emit.cc` | 327 | 8,037 | 68,223 | 980 | 653 |
| `toolchain/cupidc_frontend.cc` | 412 | 16,128 | 106,991 | 2,422 | 1,487 |

The combined deterministic object records are:

| Source | Functions | Text bytes | Object bytes | Text fingerprint |
| --- | ---: | ---: | ---: | --- |
| `toolchain/cupidc_ir.cc` | 258 | 474,830 | 510,952 | `41018078` |
| `toolchain/cupidc_emit.cc` | 327 | 507,836 | 569,964 | `7EE9548E` |
| `toolchain/cupidc_frontend.cc` | 412 | 827,634 | 983,992 | `1ACA7181` |

The promoted-base hybrid proof used an isolated, timestamp-preserving
snapshot of the BSS worktree rebased onto commit
`1cc2dc99837d759ffaa89c75586f7ea9cf3bc6d8`. A dry run confirmed that the
image target would rebuild only the two links, generated symbol source and
object, flattened kernel, and image. Two Cupid-built kernel objects were
byte-identical before one replaced the snapshot's host-owned object. The
image path passed in 78.894 seconds. A before-and-after hash check covered
the host-owned kernel object, every linked or generated product, the image,
five native Cupid tools, and the Cupid-built compiler generation. All 13
original artifacts were unchanged.

CupidLD resolved `_start` at `0x00100000`, `kmain` at `0x00101FB3`,
`_bss_start` at `0x0089D000`, and `_kernel_end` at `0x00CC1A30`. CupidDis
decoded all 42 linked entry bytes, including the complete clear sequence,
direct call, and halt loop. The pass-one ELF has SHA-256
`a686c42a48417de0a9c6ffe2c8ff948747fa0b563d43be3aa3b519f055ccc7d0`.
The final ELF has SHA-256
`fc9d2746991d5f7f3bdf314ed238d77386b5e164052044db17eabc49bc41b10b`,
and the flattened kernel has SHA-256
`73ef001a893dc11dccae717b551d3b3118e236c96e6d07e7273c1d5c98531874`.
The resulting 209,715,200-byte image has SHA-256
`d88989f6c6af0ad4095f977e7cb8d3ff2d1d835cb726b46d8d2c8089d800a82b`.

The private-image GUI smoke passed in 56.898 seconds. It reached the desktop,
passed the FPU smoke, brought up e1000 and DHCP, launched the terminal,
compiled `/bin/ls.cc`, and completed JIT execution without a panic or failure
marker. The 35,082-byte serial log has SHA-256
`1f8bb0101c64b639fd4082568cb58ac88a4d5d9c735a5ce406d431684aba99db`.

The rebased BSS tree passed the 23-test checked-seed module in 865.331
seconds, the 104-test object module in 1,214.504 seconds, and the 62-test
build-graph module in 762.018 seconds. The complete frontend and IR modules
passed 173 tests in 38.331 seconds. The native toolchain build and contract
test took 37.513 and 28.472 seconds, respectively. The normal production
image build passed in 895.032 seconds before the isolated proof began.

## Failed approaches and rejected alternatives

The first recognizer accepted the template without proving that it belonged
to the exact external `.text.start` `_start` definition. That boundary was
too broad.

The first call-alignment change kept the ordinary stack residue of eight
after the assembly reset ESP and EBP. That inserted the wrong padding before
`kmain()`. Call padding now receives the actual stack-base residue.

The first return path emitted ordinary `leave; ret` after the assembly had
abandoned the incoming frame. The entry path now ends in a non-returning
interrupt-disabled halt loop.

Allowing a compiler-managed frame alongside the reset would lose local
storage when ESP changes. Such functions now fail with a direct diagnostic.

A frontend statement-count check did not prove nesting because the AST is
constructed in postorder. Explicit statement depth now carries that fact.

The first depth guard missed labels parsed directly as block items. A
labeled assembly body therefore looked as shallow as a direct statement.
Labeled block items now enter the ordinary statement-depth path before the
label parser handles their body. The focused negative failed before that
change and passed afterward.

The first IR defense checked effect order but did not prove that the exact
assembly node was the outer body's first direct child. IR now carries the
function-body statement identity and validates the parent-child relation.

Passing the statement to GAS was rejected because compiler-owned ELF output
must not gain a host assembler dependency. Dropping clobbers or replacing
linker symbols with numeric addresses was also rejected because either
change would misrepresent the source contract.

## Consequences

Compiler head and the Cupid-built compiler now emit unchanged
`kernel/core/kernel.c` completely, and a private hybrid image proves the
entry path at runtime. The promoted manifest seed still predates the BSS
statement, and the normal production recipe still uses the host-owned
kernel object. The source therefore keeps its `.c` suffix.

No production ownership count or host-dependency count moves in this
increment. Issue #26 remains open for BSS checked-seed carriage, production
transfer, the SIMD `xmm1` clobber, and later GNU assembly forms.
`TempleOS/` remains untouched reference material.
