# ADR 0185: Accept page-aligned kernel stack tops

## Status

Accepted on 2026-07-29.

## Context

CupidC represented the kernel-entry BSS clear as one deliberately narrow GNU
assembly form. The frontend, Linear IR, and emitter all compared the statement
with a copy that fixed ESP at `0x00F00000`.

The full Doom object cohort made the linked kernel too large for the current
stack boundary. Moving that boundary is a memory-map decision, but the
compiler must first be able to represent the resulting source without adding
another one-off assembly form.

## Decision

Keep the kernel-entry statement structurally fixed while making its stack-top
immediate part of the represented form. Each compiler layer now accepts one
through eight hexadecimal digits after the exact `mov $0x` prefix. The parsed
value must be nonzero and aligned to a 4 KiB page.

The rest of the contract is unchanged. The statement must still be the direct
first child of an external, prototyped `void _start(void)` in `.text.start`.
It must name `_bss_start` and `_kernel_end`, use the exact EAX, ECX, EDI, and
memory clobbers, and run without a compiler-managed frame. Linear IR repeats
those checks, and the emitter writes the parsed value into the `MOV ESP, imm32`
instruction.

This change only prepares compiler head for a later memory-map update. The
checked seed and active kernel source still use `0x00F00000` in this commit.

## Evidence

The frontend, Linear IR, and object fixtures first changed to `0x01100000`
while the implementation still required `0x00F00000`. All three tests failed
with the existing unsupported-template diagnostic.

After implementation, the focused tests pass. The object contract decodes the
exact `BC 00 00 10 01` stack instruction and retains the two `R_386_32`
relocations plus the `R_386_PC32` call relocation. A frontend negative uses
`0x01100001` and confirms that an unaligned stack top is rejected with the
same useful source location.

The source frontier now records:

| Source | Definitions | Statements | Expressions | Block bindings | Initializers |
| --- | ---: | ---: | ---: | ---: | ---: |
| `cupidc_ir.cc` | 262 | 7,250 | 67,354 | 953 | 354 |
| `cupidc_emit.cc` | 353 | 8,533 | 71,971 | 1,041 | 708 |
| `cupidc_frontend.cc` | 420 | 16,374 | 108,280 | 2,458 | 1,497 |

Repeated object emission also fixes the three current implementation objects
at 518,620, 603,816, and 997,160 bytes. Their `.text` fingerprints are
`281B3EDB`, `7DE6EA3F`, and `0F27C0C9`.

## Rejected alternatives

Adding a second hard-coded stack address was rejected because every memory-map
change would require another compiler special case.

Splitting the reset or BSS clear into lower-level C was rejected because the
entry sequence owns ESP before normal C frame rules apply.

Accepting an arbitrary assembly template was rejected because CupidC still
needs typed, checked assembly effects. Only the immediate is variable.

## Consequences

The next checked-seed promotion can carry this capability without changing the
active kernel map. Once that seed is verified, the kernel can move its stack
boundary through a separate source, linker, bootloader, and runtime change.

Other kernel-entry assembly and general GNU template parsing remain outside
this decision.
