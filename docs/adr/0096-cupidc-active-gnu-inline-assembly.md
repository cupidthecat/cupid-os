# Support the active CSPRNG inline assembly in CupidC

- Status: Accepted
- Date: 2026-07-23

Current status: ADR 0097 moved this compiler into the checked seed, and ADR
0098 moved `csprng.c` into the normal CupidC-owned build. The compiler-head
evidence below remains the capability proof.

## Context

After CupidC learned the null and external-array rules used by the X.509
sources, `kernel/crypto/csprng.c` was the last crypto file with a language
blocker. Its three assembly statements read the timestamp counter, query
CPUID, and obtain an RDRAND value and carry flag:

```c
__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
__asm__ __volatile__("cpuid"
    : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    : "0"(eax));
__asm__ __volatile__("rdrand %0; setc %1"
    : "=r"(v), "=qm"(ok));
```

The shared frontend previously stopped at the assembly keyword. Rewriting
these helpers as magic compiler calls or leaving this one source on a
permanent host-only path would have hidden a real requirement from the
language and backend.

GNU extended assembly is a large interface. The active source needs output
operands, one matching input, and four instructions. It does not need basic
assembly, clobbers, named operands, read/write operands, memory-only
constraints, `asm goto`, or a general-purpose assembly parser inside CupidC.

## Decision

The public translation unit now owns immutable assembly and operand tables.
An assembly statement refers to one source-ordered record. Each record keeps
its decoded template, volatile flag, output and input slices, constraints,
types, expressions, and both source locations.

This first GNU assembly slice accepts:

- `asm`, `__asm`, and `__asm__` in GNU mode;
- `volatile`, `__volatile`, and `__volatile__`;
- one to four outputs using `=a`, `=b`, `=c`, `=d`, `=r`, or `=qm`;
- one-digit matching inputs such as `"0"`, with each output tied at most once;
- modifiable, non-atomic integer output lvalues;
- 32-bit fixed or general-register outputs and an 8-bit `=qm` output; and
- the exact `rdtsc`, `cpuid`, `rdrand %N`, `setc %N`, and `nop` template
  instructions needed by the active source and focused contracts.

Plain `asm` remains an ordinary identifier when GNU extensions are disabled.
The double-underscore spellings still receive a direct diagnostic saying
that GNU extensions are required.

Linear IR evaluates every output address first and every input value second,
then emits one `ASSEMBLY` instruction that consumes the complete operand
slice. Whole-unit validation checks the packed partition, source order,
constraint spelling, numeric tie, type, value category, and statement
ownership. Unreachable statements pass through the same count-only
validation, so dead code cannot smuggle malformed records into the unit.

The i386 emitter assigns the available EAX, ECX, EDX, and EBX registers
deterministically. Fixed constraints reserve their named register before
general constraints are assigned. It saves EBX in private frame storage,
loads matching inputs, emits each instruction through the shared x86 model,
snapshots the outputs, restores EBX, and stores the results through their
original lvalue addresses. The 8-bit carry result uses the selected low-byte
register and an exact byte store.

Templates and constraints outside this subset fail explicitly. The compiler
does not pass unknown text to a host assembler and does not pretend that the
supported templates form a complete GNU assembly implementation.

## Rejected alternatives

Replacing the CPU helpers with new builtins was rejected. The existing source
uses a standard GNU C interface that appears elsewhere in the kernel, and
supporting its typed operands gives the compiler a reusable boundary.

Sending template text through a host assembler was rejected. That would add a
hidden host dependency, split register allocation from CupidC, and make
object production depend on an external parser.

Treating every output as EAX was rejected. CPUID needs four fixed outputs,
RDTSC needs EAX and EDX, and the carry result needs a byte-addressable
register.

Leaving EBX clobbered was rejected. EBX is callee-saved in the i386 cdecl ABI,
and CPUID writes it.

Accepting arbitrary constraints and hoping the template does the right thing
was rejected. A rejected form with a clear diagnostic is safer than silently
miscompiling a register, memory, early-clobber, or clobber-list contract that
the backend does not implement.

## Consequences and evidence

The frontend contract covers all accepted spellings and the exact three
CSPRNG forms. Negative cases cover basic assembly, `asm goto`, unsupported
modifiers, missing or embedded-null templates, named operands, unsupported
constraints, blank templates, duplicate fixed registers, a fifth register
output, invalid lvalues and widths, atomic and bit-field outputs, malformed
and multi-digit ties, duplicate ties, unavailable outputs, and clobbers.
Every failure preserves the earlier translation unit and allows another
parse in the same job.

The IR contract fixes output-address and input-value order, maximum stack
depths of 2, 5, and 2, and one assembly instruction per source statement.
Malformed arrays, operand partitions, statement references, duplicate fixed
registers, value categories, types, ties, and orphan records fail
transactionally. Forged `const` and `_Atomic` output types fail in reachable
and unreachable statements. A valid unreachable NOP still receives full
validation but emits no instruction. Repeated lowering produces the same
fingerprint.

The object contract emits each fixture twice and requires byte identity. The
decoder finds one RDTSC, one CPUID, `RDRAND EAX`, and `SETB CL`. It also
binds the EBX save and restore to the same private frame slot and requires an
EBX output snapshot when CPUID assigns `=b`. Its execution oracle runs the
matching EAX input with zero, `0x12345678`, and `0xffffffff`, checking the
returned value, stack, and callee-saved registers each time. Replacing a
template with `hlt` or whitespace produces the checked unsupported-template
diagnostic without publishing partial bytes, and the same job then emits the
original object again.

Compiler head compiles unchanged `kernel/crypto/csprng.c` twice under the
complete `KERNEL_I386` profile. Both 6,888-byte i386 `ET_REL` objects have
SHA-256
`ab25ef013e9b91a9fab08c6ba610c6476897bc45296b4111f1df035c0e45e317`.
CupidDis confirms the expected RDTSC, CPUID, `RDRAND EAX`, and `SETB CL`
instructions in the result.

A disposable image then replaced only `kernel/crypto/csprng.o` with that
compiler-head object and repeated the repository's two-pass CupidLD link.
QEMU ran the image with its `max` CPU, which exposes RDRAND. The serial log
records RDRAND seeding, all 48 crypto and TLS checks, desktop and terminal
startup, and a completed CupidC JIT run of `/bin/ls.cc`. It contains no
failure, panic, exception, corruption, or illegal-instruction marker. The
6,418,468-byte `kernel.elf` has SHA-256
`05e6bcb7784d1f9247221b328874d94f5d26ad98670f874269d6c94e640096ce`;
the 6,238,629-byte `kernel.bin` has SHA-256
`f7f50f8b3f4aa291104cbbea099ac5a1f613c859fc79678028966e4c76e2c252`;
and the 209,715,200-byte image has SHA-256
`f3ce84e1c929cfa6ddddda86640e38e96903fc9d9affa56947bedf9b4a60cdcc`.
The serial log's SHA-256 is
`9c6f7cb5d0fc855986b277afadfbef20c27452f9d464a590a736d62705d33787`.

The complete hosted Toolchain suite passes with the new frontend, IR, and
object selectors. The five-tool fixed point also passes: all 19 C objects,
startup, and five static images match between stages two and three, along
with the five help paths, ten successful operations, and six failures. The
final rerun took 462.224 seconds. The resulting generation-one CupidC is
1,883,836 bytes with SHA-256
`f412a39f204380de8986d6dc3c3a8d6feecf4c40990c40b31634e58d254624df`.

This decision clears the language blocker at compiler head but does not move
normal-build ownership. The checked seed still predates this code, so all four
neighboring crypto sources remain host-built until a refreshed seed passes
its own staged bootstrap and the linked kernel boots. Broader GNU assembly
constraints, inputs, clobbers, templates, and header coverage remain open
under issue #26.
