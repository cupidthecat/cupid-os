# ADR 0312: Promote and adopt relocatable local-target checks

## Status

Accepted on 2026-08-21.

## Context

ADR 0309 taught source-head CupidDis to validate unrelocated direct relative
targets inside each executable section of a static i386 ELF32 relocatable
object. Relocated operands stayed under the separate relocation-ownership
rule. The promoted Linux and Windows seeds still carried the older report and
behavior, so production CupidASM object publication could not select the new
policy.

That gap mattered on both active assembly objects. The ISR object contains
external calls whose final targets are known only after link. The context
switch object also contains resolved local branches whose section and
instruction boundaries are already knowable before link.

## Decision

Promote the complete stage-four Linux and Windows five-tool cohorts built from
revision `30aaf1b7cd398e6b47a395661a33d20d00363158` and the exact 50-input
snapshot
`2b56c849dd203b386c93fab3a07def099c49c9a6464e342ee55e9641281788f9`.
Keep the build plan unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

Add a relocatable-object pair to each fixed-point behavior matrix. Its valid
form contains an external `R_386_PC32` call with addend `-4` and a resolved
local jump to an instruction start. The relocation must exclude the call from
the local-target count. The negative form changes only the jump displacement
so the target lands inside the following instruction. Linux now requires five
help, twenty success, and nineteen useful failure cases. Windows requires five
help, six success, and seven useful failure cases.

After structural ELF validation, guarded CupidASM object publication must run
the checked seed as:

```text
cupiddis --require-known --require-local-targets PRIVATE_OBJECT
```

The object, source, selected seed, existing output, and private candidate stay
inside the existing frozen publication transaction. A failed target check must
leave the previous object untouched.

Promote all five tools as one trust unit even though only CupidDis bytes change.
Update both manifests, the Windows parent binding, exact artifact-size policy,
manifest contracts, and graph behavior locks together.

## Evidence

The first Linux and Windows carriage tests failed against the preceding seeds
with command-line usage status 2 because their CupidDis images did not accept
the object form. The production hostbuild negative also failed at that old
seed boundary. After promotion, both checked seeds accept the valid object and
reject the mid-instruction form with the exact focused diagnostic. The complete
nine-test hostbuild object module passes and preserves its sentinel output on
every failure path.

The Linux candidate fixed point passed in 1,467.420 seconds. Stages three and
four matched nineteen C objects, one startup object, and all five tool images,
then passed the 5/20/19 behavior matrix. The Windows candidate passed in
1,246.924 seconds. Its stages matched twenty C objects, two assembly objects,
and all five PE32 tool images, then passed the 5/6/7 matrix. Every initial seed
comparison was true in both reports. Those candidate runs used an earlier
324-byte object pair with a local jump but no relocation. It proved seed
carriage for object-local targets, but it did not independently prove that a
relocated operand was excluded.

The promoted Linux manifest remains 5,573 bytes and has SHA-256
`afc56e3654ad7fe4447b31c87f1a010d9c13e89b824357db60b8a73648ad009c`.
Linux CupidDis is 425,940 bytes with SHA-256
`5b719da424294a91b019832e98047dd15c51e295bba8dfe1766fbde29f95626e`.
The promoted Windows manifest remains 2,118 bytes and has SHA-256
`f537e1877f813d2a8f12f9fe2feeaddeff263cf768248def6aebfb009cee1c42`.
Windows CupidDis is 407,040 bytes with SHA-256
`1ce02cadf6cc90bec0389ab9dc6c7b09ce6823a3bd23980fb78b46d3740c9b14`.
The other eight tool images remain byte-identical to their preceding promoted
counterparts.

Fresh promoted-seed reproofs passed with every stage comparison true. Linux
finished in 1,471.191 seconds and produced report SHA-256
`c591f7a622c57224b49ebfdb080bac4c107526cac4156eded5f0af2b63ee2354`.
Windows finished in 1,251.386 seconds and produced report SHA-256
`c9ff6b955a29f789a9ecbc8754bee0b872c982229287be8061400d83457c21e6`.
Both reports bind the promoted manifest, the new source snapshot, and their
full behavior matrices. Before those reproofs, the fixture was strengthened to
416 bytes with `.rel.text`, an external `R_386_PC32` call, and the local jump.
The final reports therefore prove both local-target validation and relocation
exclusion. Normal-build, audit, and runtime evidence is recorded in the
bootstrap log.

## Rejected alternatives

Do not select the source-head option with an older seed. Production validation
must be supplied by the exact checked executable frozen into the transaction.

Do not count the external call's placeholder addend as a local branch. The
relocation owns that operand, and its final target does not exist until link.

Do not promote CupidDis alone. Linux and Windows manifests define complete
five-tool trust units, including their producer lineage and parent relation.

Do not fold this policy into structural ELF validation. Section layout and
relocation shape are object-format facts; instruction boundaries belong to the
checked disassembler.

## Consequences

Both checked production seeds now validate the local control flow that is
knowable inside a relocatable object's executable sections. Production ISR and
context-switch object publication selects the rule on Windows and Linux without
changing their source, object format, ABI, or linker behavior.

Linux CupidDis grows by 4,288 bytes and Windows CupidDis grows by 6,144 bytes.
No other tool image changes. The build keeps nineteen Linux C objects and
twenty Windows C objects; no `.c` source becomes active or qualifies for a
`.cc` rename. Python still coordinates the fixed point and host publication.
Dynamic ELF, linked-image local targets, source-label identity, and typed data
inside executable sections remain open. `TempleOS/` remains untouched
reference material.
