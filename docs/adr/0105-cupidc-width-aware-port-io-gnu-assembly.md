# Support width-aware port I/O GNU assembly in CupidC

- Status: Accepted
- Date: 2026-07-24

Current status: ADR 0106 promotes this compiler capability into the checked
i386 Linux seed. The normal production cohort remains a separate hand-off.

## Context

`kernel/core/ports.h` was the last failure in the 154-header active non-Doom
sweep. Its eight inline helpers use fixed registers and three operand widths:

| Helpers | Template | Operands |
| --- | --- | --- |
| `inb`, `inw`, `inl` | `in %%dx, %%al`, `in %%dx, %%ax`, `in %%dx, %%eax` | `=a` result, `d` port |
| `outb`, `outw`, `outl` | `out %%al, %%dx`, `out %%ax, %%dx`, `out %%eax, %%dx` | `a` data, `d` port |
| `insw` | `cld; rep insw` | `+D` buffer, `+c` count, `d` port, `memory` clobber |
| `outsw` | `cld; rep outsw` | `+S` buffer, `+c` count, `d` port |

CupidC already represented output constraints and matching inputs for the
CSPRNG assembly. It could not represent independent fixed-register inputs,
read/write operands, or a memory clobber. Its fixed-register outputs also
required a 32-bit integer, so the byte and word input helpers failed before
the compiler reached IR.

Cupid's shared x86 model already encoded all eight instruction forms. The
missing work belonged in the C frontend, IR, and emitter rather than in a
second instruction encoder or a rewritten kernel header.

## Decision

In GNU mode, the frontend represents the independent `a` and `d` integer
inputs used by the active port forms and rejects duplicate assignments to
one fixed register. Operand types keep their declared width in the AST. The
`a` input and `=a` output may be an 8-, 16-, or 32-bit integer, while the `d`
input in these templates is a 16-bit integer. The older `=b`, `=c`, and `=d`
outputs remain 32-bit forms. The selected template fixes the physical AL,
AX, EAX, and DX lanes. CupidC does not promote the public assembly operand
type to make the emitter simpler.

An independent-input statement may have no outputs. Like the existing empty
extended form, that statement is implicitly volatile. Matching inputs keep
their existing numbered-output rules.

The `+D` and `+S` constraints accept a modifiable four-byte object-pointer
lvalue. The `+S` form may point to const data. The `+c` constraint accepts a
modifiable four-byte integer lvalue. Linear IR evaluates every output
destination once in output order, then evaluates the independent inputs in
input order. Its `ASSEMBLY` instruction consumes those output addresses and
input values. The emitter loads each read/write value through its saved
address and writes the final pointer or count back through the same address.

The frontend represents exactly one `memory` clobber as an assembly flag.
The INSW template requires that flag. OUTSW requires the unchanged source
form without it. Other clobber names and duplicate `memory` entries remain
unsupported. On an otherwise represented extended statement, the flag is a
compiler ordering boundary and emits no instruction bytes. Basic assembly
cannot carry a clobber list.

The i386 emitter accepts only the eight templates and operand layouts in the
table above on this path. It emits every instruction through the shared x86
model. The scalar helpers select the byte, word, or doubleword accumulator
lane without widening the operation. INSW loads EDI and ECX, clears the
direction flag, emits the repeated word input, writes back EDI and ECX, and
restores the caller's EDI. OUTSW does the same work with ESI and restores the
caller's ESI. The path does not borrow EBX.

## Rejected alternatives

Replacing the inline helpers with compiler intrinsics was rejected. The
unchanged header expresses the ABI and instruction widths directly, and the
same GNU assembly features are needed by other active kernel sources.

Sending the template to a host assembler was rejected. That would add an
external code-generation step to CupidC and split instruction validation from
the shared x86 model.

Accepting the syntax while discarding the read/write or `memory` semantics was
rejected. INSW and OUTSW update their pointer and count registers, and the
compiler barrier on INSW is part of the source contract.

Widening every assembly operand to 32 bits was rejected. It would hide source
types in the public graph and could select the wrong accumulator lane.

Leaving ESI or EDI changed after a helper returns was rejected. Both registers
are callee-saved in the i386 cdecl ABI.

## Consequences and evidence

The frontend contract parses unchanged `kernel/core/ports.h` under the kernel
profile. It publishes eight assembly records and 18 operands with their
declared widths, constraints, source locations, volatility, and INSW memory
flag. Negative cases cover narrow `=b`, `=c`, and `=d` outputs, unsupported
independent `b` and `c` inputs, a narrow read/write count, an integer in a
pointer constraint, duplicate fixed inputs, a pointer fixed input, a wide
fixed input, and a duplicate memory clobber. The active non-Doom header sweep
now passes 154 of 154.

The IR contract lowers one `ASSEMBLY` instruction in each helper. The complete
fixture has 51 instructions and fingerprint
`0632DB96740C4BAB`. Scalar helpers have a maximum abstract stack depth of two;
INSW and OUTSW have a depth of three. Repeated lowering is identical.
Malformed volatility, matching metadata, unsupported independent registers,
narrow non-accumulator outputs, fixed-register collisions, integer
`+S`/`+D` outputs, read/write constraints, and value categories fail without
changing the input unit, and the same job recovers afterward.

The deterministic ELF32 fixture is 916 bytes with 408 bytes of `.text`, five
sections, nine symbols, no relocations, and text fingerprint `008DEF58`.
Decoder checks pin `EC`, `EE`, `66 ED`, `66 EF`, `ED`, `EF`,
`FC F3 66 6D`, and `FC F3 66 6F`. They also check exact lane widths, output
writeback, the distinct saved output addresses, the 12-byte string-helper
temporary area, the EDX port load, the ECX count load, ESI and EDI
restoration, and the absence of EBX use. A narrow accumulator outside the
eight port templates receives a useful unsupported-template diagnostic.
Wrong template widths, unsupported independent registers, a partial string
template, a missing or extra memory flag, and a swapped string pointer
register fail transactionally. Repeated object emission is byte-identical,
and the job emits the valid object again after a failure.

Port I/O instructions are privileged, so the hosted object contract uses the
decoder as its ABI oracle instead of executing the helpers in user mode. This
decision does not change a production object or boot path.

The compiler-head boundary in this decision changed no production object or
boot path. ADR 0106 later promoted its stage-three compiler into the checked
seed. The normal build still remains at 26 CupidC-owned sources and 366,592
deterministic object bytes until the separate production hand-off passes.

File-scope assembly, labels and control transfer, general clobbers, naked
functions, arbitrary templates, and the broader GNU constraint language
remain outside this decision.
