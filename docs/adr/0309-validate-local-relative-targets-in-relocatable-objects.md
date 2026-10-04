# ADR 0309: Validate local relative targets in relocatable objects

## Status

Accepted on 2026-08-21.

## Context

ADR 0300 added local relative target checks for mapped raw images. ADR 0305
promoted that rule into the bootloader and SMP publication transactions.
CupidDis also decodes every executable section in a static ELF32 relocatable
object and checks that each code relocation owns a compatible operand field.

That left one object-level gap. An unrelocated call or jump could leave its
section or land inside an instruction while every byte still decoded. A
relocated relative field needs different treatment because its final target is
not known until link time. Combining instruction starts across sections would
also be wrong: equal byte offsets in two sections do not identify the same
code location.

## Decision

Extend `CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS` and the hosted
`--require-local-targets` option to static i386 ELF32 `ET_REL` input. The CLI
still requires `--require-known`. A typed ELF request must include the
disassembly view.

CupidDis treats each executable `PROGBITS` section as a separate address
domain. It allocates one bit per section byte, records instruction starts in a
first decode pass, and checks targets in a second pass. The scratch map is
rewound before the next section. An unrelocated constant direct relative
target must remain inside its source section and land at an instruction start
in that section.

A relocation that begins at the decoded relative operand field excludes that
operand from the local-target count. Executable relocation ownership remains
independent, and the strict caller still rejects the wrong relocation width,
kind, or site. This lets an external `R_386_PC32` call keep its link-time
meaning without treating its placeholder addend as a local branch.

Append `direct_relative_outside_section_count` to the typed decode summary.
Raw input keeps its outside-image, data, wrong-mode, and mid-instruction
classes. Relocatable input uses outside-section and mid-instruction counts.
The existing field offsets stay unchanged, although the public summary and
report each grow by eight bytes.

Far pointers and indirect register or memory transfers remain outside the
policy. Linked `ET_EXEC` input is rejected explicitly because its final
control-flow and retained-relocation rules need a separate design. Ordinary
raw inspection remains unchanged.

Keep this as a source-head capability. Do not promote either checked seed or
add the option to the production CupidASM object transaction in this slice.

## Evidence

The typed contract covers a clean local jump, a relocated external call,
outside-section and mid-instruction targets, far and indirect exclusions, and
two executable sections with overlapping byte offsets. It also checks an
invalid view combination, malformed ELF input, explicit `ET_EXEC` rejection,
zeroed failure reports, constrained start-map storage, and recovery in the
same job.

The hosted CLI accepts a CupidASM object containing one relocated external
call and one resolved local jump. Separate objects fail with the exact
outside-section or mid-instruction reason. The linked executable case reaches
inspection, reports the focused unsupported boundary, and does not fall back
to command-line usage text.

Source-head CupidDis accepts both active CupidASM objects with the option. The
ISR object keeps eleven external `R_386_PC32` calls outside the local count.
The context-switch object has three resolved local targets. Changing its
branch displacement at `.text+0x29` from `0x0d` to `0x0c` makes one target
land inside the following instruction, and the CLI rejects the object.

Focused commands and final results are recorded in the bootstrap log. No seed,
production hostbuild rule, object, linked image, or OS artifact changed.

## Rejected alternatives

Reusing the outside-image count for relocatable input was rejected. A section
is the real identity boundary before link, and the public report should say so.

Counting a relocated placeholder as a local target was rejected. Its bytes
encode an addend, not the final destination. Relocation ownership already
checks whether that field is valid.

Sharing one instruction-start map across executable sections was rejected.
The same section-relative offset can name unrelated bytes in another section.

Silently adding the rule to `--require-known` was rejected. Local-target
validation remains an explicit artifact-owner policy.

Applying the same rule to `ET_EXEC` was rejected. Linked images can cross
sections and segments, and retained relocations do not by themselves define a
complete local-control-flow model.

## Consequences

Source-head CupidDis can now prove the section-local direct control flow that
is knowable in a static relocatable object. The local-target check adds two
linear decode passes and one compact scratch map per executable section after
the existing strict decode summary.

The promoted Linux and Windows CupidDis seeds predate this report layout and
behavior. Production object publication still uses their strict decode and
executable-relocation checks without selecting the new object-level policy.
A later seed promotion and fixed-point reproof must carry this capability
before hostbuild adopts it.

The appended field changes the source-head C interface between CupidDis and a
caller built from the same header. It does not change an object format, guest
system-call ABI, persisted artifact, or interface used by the promoted tools.
No kernel caller selects the object policy. The hosted contract and CLI tests
compile the producer and consumers together, so an OS build or boot smoke
would not exercise this source-only path. That runtime proof belongs with the
later promotion or in-OS adoption.

Dynamic ELF, linked-image local targets, source-label identity, and data mixed
inside an executable section remain open. This slice adds no host dependency,
changes no owner or transform count, qualifies no `.c` source for renaming,
and leaves `TempleOS/` untouched.
