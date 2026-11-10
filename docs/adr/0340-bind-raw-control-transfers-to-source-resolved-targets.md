# ADR 0340: Bind raw control transfers to source-resolved targets

## Status

Accepted on 2026-08-24.

## Context

CupidDis could prove that a decoded relative transfer landed at an instruction
start in code of the same mode. That check could not distinguish the label
CupidASM resolved from another valid instruction start. It also excluded far
immediate transfers, including the active 16-bit to 32-bit transitions in the
bootloader and SMP trampoline.

The source assembler already knows the resolved expression, instruction
offset, active mode, and emitted image layout. That information was lost after
assembly. Reconstructing it from bytes would repeat the ambiguity that the new
check is meant to remove.

## Decision

Raw `ctool_asm_result_t` values publish an ordered, arena-backed control-edge
array. Each row records the source instruction offset, edge kind, proof class,
target offset and address, target mode, and far segment when present. Direct
relative transfers and immediate far calls or jumps are classified as local
when the resolved address belongs to the raw image. Resolved addresses outside
the image are external. Register- and memory-indirect calls and jumps are
published as unprovable, with their target fields left unknown.

The hosted map writer uses `cupid.raw-map.v2`. It keeps the v1 size, base, and
range records, adds one exact edge count, then writes canonical edge rows in
source order. CupidDis continues to accept v1 maps for compatibility. The
`--require-source-edges` option requires strict known-instruction checking,
raw range-map input, and explicit v2 metadata.

CupidDis first validates the metadata shape. It rejects missing storage,
duplicate or unordered sources, rows outside the image or inside data,
inconsistent local addresses, target modes that disagree with the range map,
and malformed local, external, or unprovable fields. It then decodes every
code range twice. The second pass requires one matching row for each supported
decoded transfer and compares the encoded destination with the source-resolved
destination. Local targets must also begin an instruction in the recorded
mode. Immediate far transfers must retain both offset and segment. An extra or
missing row is a source mismatch.

The active bootloader map contains twelve rows: nine local relative transfers,
two local far transitions, and the deliberate external far jump to
`0x00100000`. The SMP map contains six rows: four local relative transfers,
one local far transition, and one register-indirect jump whose runtime target
remains explicitly unprovable.

## Evidence

The public assembler result contract checks all three proof classes and both
direct edge kinds. Hosted map tests check exact v2 serialization. Public
CupidDis contracts cover a valid map, a changed displacement that still lands
on a valid instruction boundary, a target in the middle of an instruction,
duplicate rows, rollback, and recovery in the same job. CLI tests add missing
counts, count drift, out-of-range rows, wrong target modes, missing and extra
instruction rows, and v1 compatibility.

The assembler, disassembler, and active-source Python modules passed 51 tests
with one platform skip. The native raw-source contract also passed under the
warning-clean hosted build. Active-source inspection accepted the unchanged
boot and SMP images. A changed SMP far target failed with target and mode
mismatches. A changed external boot target failed even though both mutations
still passed the older known-instruction and local-relative checks.

Review added direct and CLI cases for a 16-bit target that wraps past
`0xffff`, a 16-bit to 32-bit external far jump, and the valid external address
`0xffffffff`. It also proved that an external row cannot reclassify a target
inside the image to bypass the local instruction-start check. The resulting
Python group passed 52 tests with the same platform skip.

## Rejected alternatives

Adding label names to the disassembler input would expose source spelling
without improving the binary contract. The resolved address is the semantic
fact that must match the encoding.

Treating every out-of-image or indirect transfer as local would claim evidence
the assembler does not have. External and unprovable rows keep those limits
visible.

Replacing v1 in place would make existing checked seeds and private maps
unreadable. A new schema keeps old readers and transactions stable until the
capability is promoted and adopted.

## Consequences

Source-head CupidASM and CupidDis can now bind represented raw control-flow
bytes to the assembler's resolved targets. The change does not alter the boot
or trampoline bytes, their ABI, or tool ownership.

The checked Linux and Windows seeds do not yet carry v2. Production boot and
SMP transactions therefore remain on their existing v1 map and local-relative
policy until a later seed promotion and guarded adoption. Returns, interrupt
returns, and other implicit runtime transfers have no static destination row.
Register- and memory-indirect calls and jumps are recorded, but their runtime
destination cannot be proved by this metadata.

ADR 0336 supersedes the production-carriage statement above. Both checked
seeds now carry v2, and the boot and SMP publishers require source-edge
validation. The limits on implicit and indirect runtime transfers remain.
