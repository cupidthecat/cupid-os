# ADR 0252: Share x87 integer memory forms

## Status

Accepted on 2026-08-09.

## Context

Hosted CupidC transports twelve-byte i386 `long double` values, but runtime
integer conversion still stops before Linear IR. The target conversion path
needs x87 signed integer loads and integer pop stores. Cupid's shared x86
catalogue did not contain those instructions.

Writing opcode bytes inside the compiler would give CupidC an instruction
surface that CupidASM and CupidDis could not represent. The shared catalogue
is the authority for all three tools.

## Decision

Add canonical `FILD` and `FISTP` mnemonics to the shared x86 model. Each
mnemonic has signed integer memory forms at 16, 32, and 64 bits:

| Form | Encoding |
| --- | --- |
| `FILD m16int` | `DF /0` |
| `FILD m32int` | `DB /0` |
| `FILD m64int` | `DF /5` |
| `FISTP m16int` | `DF /3` |
| `FISTP m32int` | `DB /3` |
| `FISTP m64int` | `DF /7` |

The rows work in 16-bit and 32-bit execution modes through the existing x87
memory recipe. They accept only memory operands at the declared width. No
CupidASM parser exception or CupidDis decoder callback is added.

At this decision, source head had 602 forms, 247 canonical mnemonics, 64
register names, and fingerprint `64429699`. The checked seed still had 596
forms, 245 canonical mnemonics, and fingerprint `DA15E97F`. ADR 0258 later
promoted the 602-form catalogue.

## Evidence

The first shared-model run failed to compile because `CTOOL_X86_MN_FILD` and
`CTOOL_X86_MN_FISTP` did not exist. The first public CLI run reported both
spellings as unknown instructions.

The shared contract now encodes and decodes all six forms at exact widths. It
also rejects a register source and an 80-bit store while clearing the output
record. Public CupidASM and CupidDis tests assemble the six forms to
`DF 00 DB 00 DF 28 DF 18 DB 18 DF 38`, render their canonical width
spellings, and reject register and byte operands without publishing a file.

The focused shared-model suite passes all 13 tests. The new public CLI suite
passes both tests, and the existing x87 comparison suite passes all three
tests. The broader CupidASM suite passes all 9 tests. CupidDis passes 15 tests
with one existing platform skip.

## Rejected alternatives

Adding only the 32-bit and 64-bit forms was rejected because the x87 family
has one coherent signed integer memory surface. The 16-bit rows use the same
recipe and give CupidASM and CupidDis complete width coverage.

Adding private emitter bytes was rejected because it would split instruction
ownership across two tables.

Treating `FISTP` as a real-number store was rejected because its memory
operand is a signed integer object and has different opcodes from `FSTP`.

## Consequences

CupidASM and CupidDis can represent the complete signed x87 integer load and
pop-store family through the same typed seam. CupidC does not use the new
forms in this decision. Runtime integer conversion involving `long double`
remains open until the frontend, Linear IR, emitter, and execution contracts
land together.

No production source changes owner, so no `.c` to `.cc` rename is due. ADR
0258 carries these forms in the checked seed. `TempleOS/` remains untouched
reference material.
