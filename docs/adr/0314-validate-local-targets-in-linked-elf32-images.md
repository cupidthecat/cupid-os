# ADR 0314: Validate local targets in linked ELF32 images

## Status

Accepted on 2026-08-21.

## Context

ADR 0300 added local relative target checks for mapped raw images. ADR 0309
extended the same explicit policy to static ELF32 relocatable objects, and ADR
0312 promoted both forms into the checked seeds. Linked ELF32 images still
received a focused rejection even though CupidDis already read their program
headers and decoded their executable bytes.

A linked target has different boundaries from a relocatable target. It may
cross sections or load regions, and there is no unresolved operand relocation
to exclude. The final virtual addresses and load ranges are available, so the
remaining question is whether each direct relative target reaches the start of
file-backed executable code.

## Decision

Apply `CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS` and the hosted
`--require-local-targets` option to static i386 ELF32 `ET_EXEC` input. The CLI
still requires `--require-known`, and a typed request must include the
disassembly view.

Keep the linked-image certification domain static. Reject an input with a
`PT_DYNAMIC` or `PT_INTERP` program header before local-target validation.

Scan each file-backed executable `PT_LOAD` region twice. The first pass records
instruction starts in one compact map. The second pass classifies constant
direct relative targets. A target may cross load regions, but it must land on
an instruction start in file-backed executable code. Executable file ranges
must not overlap because one virtual address cannot have two instruction
identities.

Report a target outside every loaded memory range, a target in loaded memory
without file-backed executable code, and a target inside an instruction as
separate failures. The second class includes non-executable load data and the
memory-only tail of an executable load region. Far pointers and indirect
register or memory transfers remain outside the policy.

Keep this as a source-head capability. No production linked-image transaction
selects the new form, and the checked Linux and Windows CupidDis images still
carry the ADR 0312 behavior.

## Evidence

The typed contract covers targets in the same executable load region and in a
second executable load region. It also covers targets outside the loaded image,
inside a non-executable load region, inside an executable memory-only tail, and
inside an instruction. Far and indirect transfers remain uncounted.

Typed and CLI negative cases reject `PT_DYNAMIC` and `PT_INTERP` inputs as
outside the static certification domain.

Additional cases reject overlapping executable load regions, exhaust the
instruction-start map, and prove allocation recovery in the same job. The CLI
fixtures check exact success behavior and each linked-image failure class.

The staged Linux and Windows bootstrap drivers include one valid and one
invalid linked-image fixture. The generated audit reports failure, help, and
success counts of 20/5/21 for Linux and 8/5/7 for Windows. Audit generation and
checked comparison both pass. These inventories are source-head evidence, not
checked-seed promotion or production adoption.

## Rejected alternatives

Treating every target outside its source load region as invalid was rejected.
A linked direct call may cross executable load regions because final virtual
addresses are known.

Treating every loaded byte as code was rejected. Writable data and executable
memory-only tails do not provide file-backed instruction starts.

Silently adding the rule to `--require-known` was rejected. Local-target
validation remains an explicit artifact-owner policy.

## Consequences

Source-head CupidDis can validate direct relative targets in raw images,
static relocatable objects, and linked ELF32 images through one explicit
policy. The linked form certifies only static images. It adds two linear decode
passes and one compact map over file-backed executable bytes.

The normal kernel and user linked-image publishers do not select this form.
A later seed promotion needs new manifests, clean Linux and Windows candidate
proofs, and promoted-seed reproofs before documentation can say that checked
CupidDis carries it. This change adds no host dependency, owner, transform, or
object-format feature. `TempleOS/` remains read-only reference material.
