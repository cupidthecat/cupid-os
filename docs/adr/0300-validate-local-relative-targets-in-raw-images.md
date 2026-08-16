# ADR 0300: Validate local relative targets in raw images

## Status

Accepted on 2026-08-15.

## Context

Strict CupidDis inspection already rejects unknown, invalid, or truncated code
bytes. It also checks relocation ownership in ELF32 objects. A raw image could
still contain a well-formed relative call or jump whose displacement lands in
data, in another instruction, in code with the wrong mode, or outside the
image. Every byte could decode cleanly while control flow was wrong.

This matters in two active sources. The bootloader has nine direct near
relative targets after its three far jumps are excluded. The SMP trampoline
has four after its far mode transition and indirect call are excluded. Their
source-derived range maps already distinguish 16-bit code, 32-bit code, and
data.

## Decision

Add `--require-local-targets` as an explicit modifier for
`--require-known --raw`. Keep `--require-known` unchanged. The first version
rejects ELF input rather than guessing how relocations, sections, and external
targets should interact.

CupidDis marks every decoded instruction start in a one-bit-per-byte scratch
map, then decodes the code ranges a second time. Constant relative operands
are checked against the complete raw image. Range lookup uses binary search
over the already validated ordered map. Crossing an intervening data range is
legal when the destination is an instruction start in code with the same
mode.

Failures are classified in this order:

1. outside the image;
2. inside declared data;
3. inside code with a different mode;
4. inside an instruction rather than at its start.

For 16-bit code, CupidDis maps a wrapped logical target back to the image with
`(target16 - (base & 0xffff)) & 0xffff`. A raw input containing code16 cannot
use this policy when it is larger than 65,536 bytes because the mapping would
not be unique. A 32-bit target must fall in the non-overflowing image address
interval.

Only constant operands represented as `CTOOL_X86_OPERAND_RELATIVE` enter the
check. Far pointers and register or memory targets do not. Unknown, invalid,
and truncated instructions remain the responsibility of `--require-known`.

The typed inspection API records the total target count and four failure
counts. A structurally valid inspection still returns `CTOOL_OK`; an enforcing
caller must reject a report with any nonzero failure count. The command-line
driver performs that rejection and reports the total plus each reason.

Keep the production bootloader and trampoline calls on their checked-seed
arguments for now. The current seeds do not carry the option. Production may
adopt it only after Linux and Windows fixed-point rebuilds, promotion, and
reproof.

## Evidence

The typed and command-line contracts cover valid targets, all four failure
classes, a jump across data, 16-bit wraparound, an empty image, oversized
code16 input, invalid policy combinations, constrained scratch storage,
same-job recovery, and fail-atomic reports. Direct-family coverage includes a
call, short and near jumps, and short and near conditional jumps. A separate
case confirms that far and indirect transfers do not enter the count.

The full CupidDis module passes 25 tests with one platform skip in 4.540
seconds. The active-source checks pass in 2.845 seconds and preserve the exact
bootloader and trampoline bytes. Deliberately changing one displacement makes
CupidDis report one invalid target out of nine for the bootloader and one out
of four for the trampoline. The complete fail-closed fixed-point audit
mutation matrix passes in 116.545 seconds.

The checked Linux CupidC seed compiles the final sources without a host C
compiler:

| Source | Object bytes | SHA-256 |
| --- | ---: | --- |
| `toolchain/cupiddis.cc` | 108,024 | `ca41af4dba884ad9abdcabcfed00f4ac6d59c91dcf80a3bf162be5ac2b8238dd` |
| `toolchain/cupiddis_main.cc` | 38,940 | `92a799d930e0138660a53fb463ea055053ef30cbb3ecd77bfa5a9d982bf77e4a` |
| `toolchain/tests/cupiddis_contract.cc` | 127,500 | `8d5b187527037fa4add2e5172f554db75243ad26102eac680f785f812ab9dff4` |

The fixed-point behavior source now includes a valid local jump and an invalid
outside-image jump for both Linux stage comparison and native Windows loader
comparison. The existing Windows text-rendering check remains separate. A
full fixed-point run and seed promotion are not evidence for this source-only
increment.

## Rejected alternatives

Do not silently strengthen `--require-known`. Some raw callers may contain
intentional relative transfers outside their declared image, so the stricter
policy must be selected by its owner.

Do not infer code from bytes. CupidASM's source-derived range map remains the
authority for code and data boundaries.

Do not accept any destination merely because it lies in a code range. Landing
in the middle of a valid instruction is still structurally wrong.

Do not claim source-label identity. A corrupted displacement that lands on a
different valid instruction start in same-mode code passes this structural
check. Proving the intended source label needs richer assembler metadata.

Do not fold far, indirect, or ELF targets into this first boundary. Each needs
its own ownership and relocation rules.

## Consequences

Source-head CupidDis can independently check thirteen direct local targets in
the two active raw boot artifacts. The check is bounded by two linear decode
passes, a compact bitset, and logarithmic range classification.

No production transform, owner count, host dependency, checked seed, or OS
artifact changes in this step. The normal bootloader and SMP transactions keep
their current strict decode checks until promotion. No source suffix rename is
due, and `TempleOS/` remains untouched reference material.
