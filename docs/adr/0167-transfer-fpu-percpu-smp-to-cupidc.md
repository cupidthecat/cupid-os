# ADR 0167: Transfer FPU, per-CPU, and SMP roots to CupidC

## Status

Accepted on 2026-07-28.

## Context

The checked i386 Linux seed could already compile the complete FPU, per-CPU,
and SMP translation units. Their active requirements had reached CupidC in
earlier increments:

- `fpu.c` needed the general-register-only target attribute, LDMXCSR, MOVSS
  memory operands, and the balanced x87 sine block.
- `percpu.c` needed the descriptor-table and segment-register statements used
  for GDT and GS setup.
- `smp.c` needed the three exact naked IPI entries.

Those compiler and seed proofs did not move production ownership. The normal
Make graph still sent the three roots to the host compiler, kept their `.c`
suffixes, and omitted them from the checked wrapper's frozen-closure policy.

FPU initialization also has an ordering requirement that an object hash alone
does not explain. CupidC must not introduce a helper call or floating work
before `fpu_init_cpu()` writes CR4, and the function must initialize x87 before
loading MXCSR.

## Decision

Rename the production roots and move their normal recipes to the checked seed:

- `kernel/cpu/fpu.cc`
- `kernel/smp/percpu.cc`
- `kernel/smp/smp.cc`

Their object names and link positions do not change. Each Make recipe calls
`tools/cupidc_kernel_compile.py`. The wrapper freezes the verified seed and
the complete recursive source closure, compiles from that private snapshot,
validates i386 ELF32 `ET_REL`, rechecks the live closure, and publishes only a
completed object. Input drift or a failed compile leaves the previous output
untouched.

The frozen header closures are:

| Source | Recursive headers |
| --- | --- |
| `kernel/cpu/fpu.cc` | `drivers/serial.h`, `kernel/core/panic.h`, `kernel/core/process.h`, `kernel/core/types.h`, `kernel/cpu/fpu.h`, `kernel/cpu/isr.h`, `kernel/cpu/libm.h` |
| `kernel/smp/percpu.cc` | `drivers/serial.h`, `kernel/core/process.h`, `kernel/core/types.h`, `kernel/smp/percpu.h` |
| `kernel/smp/smp.cc` | `drivers/serial.h`, `kernel/core/process.h`, `kernel/core/types.h`, `kernel/cpu/fpu.h`, `kernel/cpu/idt.h`, `kernel/cpu/isr.h`, `kernel/mm/memory.h`, `kernel/smp/acpi.h`, `kernel/smp/bkl.h`, `kernel/smp/ioapic.h`, `kernel/smp/lapic.h`, `kernel/smp/mp_tables.h`, `kernel/smp/percpu.h`, `kernel/smp/smp.h` |

The production FPU object has a typed code-generation policy. Cupid's ELF
reader finds `fpu_init_cpu()`, and the shared x86 decoder walks its exact
symbol range. Every instruction must decode as known. The policy:

- rejects every `CALL`;
- requires one typed write to CR4 before x87, MMX, XMM, SSE, or floating
  machine-state work;
- requires exactly one `FNINIT` after that write;
- requires exactly one 32-bit memory `LDMXCSR` after `FNINIT`;
- rejects any other floating work in the function.

The four-vCPU runtime contract requires all three FPU milestones:
`[fpu] SSE2 enabled`, `[fpu] boot smoke ok`, and `FPU boot smoke passed`.
The in-OS `feature16_asm_fpu.cc` run remains the end-to-end floating execution
check.

## Evidence

Two checked compiles of each promoted source produce deterministic, validated
objects:

| Object | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/cpu/fpu.o` | 6,620 | `14c3ea232b7d4455ceabd561c69293cc5849abae24d9f210aa69d64ed8c8a5cb` |
| `kernel/smp/percpu.o` | 6,760 | `3c2c6f0e00e5edec1ca16cba91e9fc593d1c42e24f4ebd3591e5f574fb0dd772` |
| `kernel/smp/smp.o` | 8,444 | `bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1` |

The SMP digest differs from the earlier `.c` proof because its diagnostics
retain `__FILE__`; the production object now records `/kernel/smp/smp.cc`.

The typed FPU policy accepts the production object. Its negative fixture
replaces the only `MOV CR4,EAX` encoding with three NOP instructions. The
decoder then rejects `FNINIT` because floating work appears before the CR4
write.

The complete source-driven frontier compiles all 151 checked-in normal roots
twice. Both passes are byte-identical and total 3,643,676 bytes. The frozen
439-input snapshot has SHA-256
`dd61ee8ece6a26282f7ae2d5f252f53c109827bf3e7a3365a00cc5a6e8d59a8a`.
There are no compiler boundaries in that cohort.

A forced production rebuild names invalid `CC`, `CXX`, `CPP`, `HOSTCC`,
`HOSTCXX`, `ASM`, `LD`, `AR`, `NM`, and `OBJCOPY` commands. All three objects
still rebuild through the checked wrapper. The dry-run contracts now poison
`AS` as well, cover all four roots in the SMP ownership group, and reject any
host-tool expansion. Wrapper tests also cover frozen recursive closures,
source drift, invalid objects, atomic publication, and preservation of an
earlier output after failure.

A clean `make -j2 all WAD_SRCS=` build completes through the production
recipes. CupidLD links both kernel passes, CupidObj flattens the final kernel,
and CupidASM assembles the boot and SMP inputs. The resulting artifacts are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 8,049,800 | `a617da173e8fdaff777221adf1e042d03628108b00a380ba3a6575169c49848a` |
| `kernel/cpu/ksyms_data.o` | 105,920 | `4a343b54571ed94324ce09e3ba48859ecdb36497e4e284b5f7996c81ed260131` |
| `kernel/kernel.elf` | 8,156,296 | `cc8b8ea2dfc767d585f490245ce9d416e085e1bf94d5381efe752667f9802efa` |
| `kernel/kernel.bin` | 7,959,532 | `e83dd816ef250f57aab384faafe5fa71e65fcbb91b6cda1e6ddd54d482577b68` |
| `cupidos.img` | 209,715,200 | `f4545e3ff1d6209dd097c548461b235fb8ae95ae3241bceea24034c359e37e2d` |

All 7,959,532 flat-kernel bytes match the image at LBA 5. CupidDis reports
5,722 accepted rows from each kernel pass, including 4,392 text symbols.
Every shared text symbol keeps its address. The generated symbol blob contains
105,505 meaningful bytes and three zero padding bytes, and the checked wrapper
reproduces the `ksyms_data.o` row above.

Sequential QEMU runs boot private copies of that image with four `max` CPUs.
The e1000 and RTL8139 runs each bring all four discovered CPUs online,
initialize the selected NIC, print the FPU enable and boot markers, and finish
`feature16_asm_fpu.cc` with its PASS and CupidC JIT completion markers. Each
log contains all 62 TLS self-test successes. The 54,869-byte e1000 log has
SHA-256
`65fc984950e3813e387d41a2f480d3f3e59dd38b6a2478abb2df73a6854c11e2`;
the 53,419-byte RTL8139 log has SHA-256
`7bd6fdb1829c8b71194b6a6024e1ba61528122a345baae4078a127bd8394e989`.

An additional E1000 run executes `feature20_smp.cc`. It reports four CPUs,
performs one 100,000-increment atomic counter loop, prints the PASS value, and
reaches the JIT completion marker.

The final integration passes 106 kernel-wrapper, GUI/runtime, and
freestanding tests; three affected frontier-publication tests; all 171
frontend and Linear IR tests; the strict hosted Toolchain build; and the
five-tool static fixed point. The generated 698-source graph has active-source
digest
`8266d73b94adc85dad423397ca19db467a2f37b3af2d6d38e1eb60ac9bba43d3`.
Its 1,526,996-byte JSON has SHA-256
`a395404b91995c35cdbe6ac69decdcdcbd0ba3f8a44b2f5f75f69a1e40f0f775`.

## Rejected alternatives

Keeping the `.c` names after production ownership changed was rejected. The
repository uses `.cc` as the checked CupidC ownership boundary.

Dropping `__FILE__` or preserving the old `smp.c` text inside the object was
rejected because the source already uses the macro legitimately. The new path
is the truthful production value.

Compiling only the three source files without freezing their recursive headers
was rejected. It would not protect production from concurrent header drift or
an undeclared Make dependency.

A host compiler `-S` check for FPU ordering was rejected because it would
inspect the oracle compiler instead of the production CupidC object. The first
CupidDis prototype used a text regular expression and could backtrack into an
opcode byte ending in `FC`, misclassifying it as an x87 mnemonic. The typed ELF
and x86 policy has no formatting dependency.

`kernel/gui/terminal_ansi.c` was investigated but not transferred. The normal
kernel links `kernel/gui/ansi.cc`, the audit marks `terminal_ansi.c` as
superseded, and both files export the same `ansi_*` symbols. Adding the stale
object would duplicate symbols and restore less complete behavior. A direct
checked-seed probe produced a valid 5,676-byte object with SHA-256
`6af75de27b194e2c229a37ce5f12af1272a8e3bec7edea1ba023a35b0dfc33f1`,
but that result does not make it a production root.

Using compiler-head binaries directly was rejected. Production remains tied to
the verified checked seed and its frozen manifest.

## Consequences

The normal checked CupidC cohort now contains 151 checked-in roots and the
generated kernel-symbol translation. Its two-pass frontier has 151 objects per
pass, covers 439 source and header inputs, and totals 3,643,676 bytes.

Four strict checked-in roots remain with the host compiler:

- `kernel/core/kernel.c`
- `kernel/core/string.c`
- `kernel/cpu/libm.c`
- `kernel/cpu/simd.c`

The host compiler and native linker still build hosted development tools and
contracts. Windows still runs the checked i386 seed through WSL for normal root
builds. This transfer does not claim a native Windows fixed point or complete
self-hosting. `TempleOS/` remains untouched reference material.
