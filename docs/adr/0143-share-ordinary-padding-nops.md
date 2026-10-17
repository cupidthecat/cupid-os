# ADR 0143: Share ordinary compiler padding NOPs

- Status: Accepted
- Date: 2026-07-27

## Context

Host compilers align functions and branch targets with NOP instructions that
are longer than the single-byte `90` form. The shared x86 catalogue only knew
the single-byte form. CupidDis therefore emitted `db` rows for ordinary
`66 90` and `0F 1F /0` padding, while CupidASM could not express the ModRM
forms through the same instruction authority.

An independent instruction-boundary census found 1,100 ordinary multibyte NOP
instructions in 74 host-built kernel and Doom objects. They occupy 6,610
bytes. The corpus contains all nine common lengths from two through ten bytes:

| Bytes | Sites |
| --- | ---: |
| `66 90` | 105 |
| `0F 1F 00` | 126 |
| `0F 1F 40 00` | 139 |
| `0F 1F 44 00 00` | 123 |
| `66 0F 1F 44 00 00` | 136 |
| `0F 1F 80 00 00 00 00` | 108 |
| `0F 1F 84 00 00 00 00 00` | 117 |
| `66 0F 1F 84 00 00 00 00 00` | 117 |
| `66 2E 0F 1F 84 00 00 00 00 00` | 129 |

The same corpus contains 117 single-byte NOPs, which already decoded.
Repeated operand-size prefixes used by a separate Clang padding family are
not ordinary encodings and remain outside this decision.

## Decision

Add ordinary `90` and `0F 1F /0` NOP forms to the shared 16-bit and 32-bit
x86 model.

The semantic operand width selects the operand-size prefix. A plain `nop`
keeps the shortest `90` encoding. An explicitly non-default operand width
encodes `66 90`. The `0F 1F /0` family accepts a word or doubleword register
or memory operand. CupidASM applies its normal mode-sized memory default, so
`nop [eax]` is a doubleword operation in 32-bit mode and `nop [bx + si]` is a
word operation in 16-bit mode.

Normal address-size and segment overrides remain available for memory
operands. The group digit must be zero. Byte and quadword operands, mismatched
register widths, `LOCK`, `REP`, and `REPNE` are rejected.

The decoder reports ordinary NOPs as catalogue-backed known instructions.
Their form identity can be replayed to preserve the exact bytes. The existing
`F3 90` PAUSE form remains distinct.

## Rejected alternatives

Teaching only CupidDis about the byte patterns was rejected because ordinary
NOPs are valid assembler input and belong in the shared instruction model.

Treating every prefixed `90` as a NOP was rejected. `F3 90` is PAUSE, and
other legacy prefixes need their own defined semantics or conservative
recovery.

Changing compiler flags or active C source to suppress alignment padding was
rejected. These instructions are normal i386 compiler output, so Cupid tools
must understand them.

Accepting every `/r` digit after `0F 1F` was rejected. The represented
instruction requires ModRM digit zero.

## Consequences and evidence

The source catalogue now has 587 rows, 242 canonical mnemonics, 64 registers,
and fingerprint `68E281CB`.

The shared x86 contract checks the nine measured forms, both processor modes,
both operand widths, register and memory operands, segment and address-size
overrides, semantic decode fields, exact-form replay, every-byte truncation,
invalid group digits, illegal prefixes, width failures, PAUSE separation, and
same-buffer recovery. CupidASM checks exact bytes from source in both modes
and useful failures for unsupported widths, registers, prefixes, and extra
operands. CupidDis checks canonical rendering, instruction boundaries,
truncation, and recovery.

Across the complete set of 228 available i386 kernel objects, CupidDis
fallback rows fall from 6,952 in 77 objects to 3,597 in 68 objects. The new
decoder renders 1,781 NOP rows in that corpus. Recognizing one padding
instruction can also repair downstream instruction alignment, so the
fallback reduction is larger than the 1,100 independently counted ordinary
NOP sites.

This change moves no source owner and retires no host dependency. The checked
bootstrap seed still contains the earlier 583-row model. It must be refreshed
in an integration follow-up before fixed-point or production use can claim
this capability. Repeated-prefix Clang padding and packed-integer SSE2 remain
open.
