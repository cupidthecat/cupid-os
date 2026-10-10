# ADR 0354: Transfer CupidASM object publication to CupidBuild

## Status

Accepted on 2026-08-27.

## Context

The active Linux and Windows v2 seed directories contain a complete six-tool
cohort. CupidBuild can already freeze the source and seed, run a private
CupidASM candidate, validate its ELF32 relocatable form, inspect it with
CupidDis, recheck every publication boundary, and replace the output
atomically. The normal ISR and context-switch recipes still entered that
transaction through `tools/hostbuild.py`, so Python retained ownership of two
operations that CupidBuild could perform itself.

The direct recipe also exposed two host details. CupidBuild rejects `.` as a
repository root because its path contract requires a stable explicit path.
Make therefore supplies the quoted absolute `$(CURDIR)`. The Linux CupidBuild
seed must also retain executable mode in Git so a fresh checkout can run it
without a repair step.

## Decision

The `kernel/cpu/isr.o` and `kernel/core/context_switch.o` recipes invoke the
platform's promoted CupidBuild seed directly with
`assemble-cupidasm-object`. Each recipe declares the Makefile and the complete
six-tool seed as prerequisites, passes the production seed manifest, supplies
the source and output explicitly, and uses the quoted absolute repository
root. Neither recipe depends on Hostbuild, the standalone CupidASM variable,
or a Python command.

The existing guarded transaction remains the publication policy. It validates
the private candidate as i386 `ET_REL`, requires CupidDis known-decode,
local-target, and code-anchor checks, rechecks the frozen source, manifest,
six-tool cohort, output parent, owner lock, and existing output, then publishes
atomically. This transfer changes the coordinator, not the object bytes or ABI.

The build-graph audit records CupidBuild as `cupid_builder` on these two
transforms. CupidASM and CupidDis remain their language and inspection owners.
Python remains an orchestrator on the other 450 active transforms. CupidBuild
is therefore no longer only a checked non-producer, although its presence in a
fixed-point build still does not claim production ownership by itself.

The tracked Linux CupidBuild seed uses executable mode `100755`. Its bytes and
manifest digest are unchanged. Direct execution under WSL returns CupidBuild's
usage line.

## Evidence

The build-graph contract tests first failed against the Hostbuild recipes and
then passed with the direct CupidBuild commands, exact seed prerequisites, and
the absolute-root requirement. Both recipes were forced on native Windows with
`PYTHON=missing-python`. They completed through the promoted Windows seed and
produced the same bytes as the previous publications:

- `kernel/cpu/isr.o`: SHA-256
  `ffefff3f2ed557d40c636f675bac8597b00179c070b5dea1e995d0c35a0b8840`
- `kernel/core/context_switch.o`: SHA-256
  `440d6605e50b56461cec91a45308b0d65ad5306fd8e6f217b4dc638f22663901`

The regenerated active-source audit records two CupidBuild participations and
450 Python participations across the same 452 transforms.

The complete normal build passed both kernel links, whole-image CupidDis
inspection, all 16 exact artifact sizes, and disk-image publication. The CTXT
updates increased the raw kernel by 508 bytes to 9,506,080, so the checked size
policy moved with the source. A private four-vCPU `max` and E1000 smoke then
brought all CPUs online and ran `/bin/ls.cc` through in-OS CupidC.

## Alternatives considered

Keeping the Python entry point would preserve the old graph but leave an
already implemented transaction outside CupidBuild ownership. Passing `.` as
the root would weaken the path contract and fails before execution. Adding a
generic command runner in the same change would broaden the trusted interface
without being needed for this transfer.

## Consequences

Two normal output-producing recipes now execute CupidBuild directly. The raw
bootloader and SMP trampoline paths, kernel linking, C compilation, generated
sources, user programs, and fixed-point reconstruction still use Python for
coordination. Their transfers require separate typed interfaces and evidence.
No active source suffix changes in this step because both inputs are assembly;
all active CupidC translation units already use `.cc`.

TempleOS remains reference material and is not part of this ownership count.
