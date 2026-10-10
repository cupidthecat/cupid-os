# ADR 0286: Guard CupidASM object publication with CupidDis

## Status

Accepted on 2026-08-14.

## Context

The root build has five active CupidASM transforms. The bootloader and SMP
trampoline already use checked transactions that inspect private raw images
before publication. The ISO spanning fixture also uses a private candidate
and an exact 4,096-byte oracle. The ISR and context-switch rules were the two
exceptions. They ran CupidASM directly against their public `.o` paths.

The final `kernel.bin` transaction does inspect both objects as members of its
431-input cohort. That check is not a substitute for object publication
protection. A direct object can feed both kernel links and kernel-symbol
generation before the final transaction runs. If a later check fails, the bad
object remains public and can affect the next build.

The audit found these production edges:

| Output | Format | Protection before this decision |
| --- | --- | --- |
| `boot/boot.bin` | Raw boot image | Private CupidASM image and map, strict CupidDis, exact size, drift checks, and atomic publication |
| `kernel/smp_trampoline.bin` | Raw mixed-mode image | Private CupidASM image, caller-owned code and data map, strict CupidDis, exact size, drift checks, and atomic publication |
| `kernel/cpu/isr.o` | i386 ELF32 `ET_REL` | Direct CupidASM publication, followed later by the final kernel gate |
| `kernel/core/context_switch.o` | i386 ELF32 `ET_REL` | Direct CupidASM publication, followed later by the final kernel gate |
| `test_iso/fixtures/big.bin` | Raw data fixture | Private CupidASM image, exact byte oracle, drift checks, and atomic publication |

## Decision

Hostbuild exposes one `assemble-cupidasm-object` operation for production
ELF32 assembly. Both active object rules call it with the production seed
manifest, repository root, source, and output. Their prerequisites include
hostbuild, the shared relocatable-object validator, and `CHECKED_SEED_INPUTS`.

The operation reuses the checked assembly transaction that serves the raw
image callers. It locks and pins the public output, freezes the source and
five-tool seed into a private root, and asks checked CupidASM for an ELF32
candidate there. The candidate must pass the shared i386 `ET_REL` validator
and contain at least one byte in an executable `PROGBITS` section.
The object operation imports that validator before it opens the transaction,
so a live edit cannot replace the validation policy between assembly and
inspection. Other hostbuild commands do not load it or gain an undeclared
dependency.

Checked CupidDis then runs as:

```text
cupiddis --require-known PRIVATE_OBJECT
```

Every executable section byte must decode without an unknown, invalid, or
truncated instruction. Hostbuild rechecks the source, seed, candidate, live
output, and output parent before a parent-relative atomic replacement. A
failed assembler, malformed object, incomplete decode, input change,
competing publisher, or publication error leaves the prior object intact.

The object and ISO fixture rules use `CHECKED_SEED_INPUTS`, not the standalone
`CUPIDASM_INPUTS` or `CUPIDDIS_INPUTS` variables. Overriding a development
command or its dependency list cannot weaken any production assembly edge.

The final 431-input kernel transaction remains in place. It is an independent
whole-kernel check, while this operation protects each object at the point it
becomes public.

## Evidence

The first object test failed because hostbuild had no
`assemble-cupidasm-object` command. The first Make tests found direct object
recipes with no CupidDis participant and accepted a poisoned standalone
dependency closure. A separate test showed that the ISO lane still accepted
the same poisoned closure. The executable-section test then showed that a
structurally valid data-only object could reach CupidDis.

After the changes, the seven object-publication tests pass in 0.478 seconds.
They cover successful publication, validator loading, malformed ELF, an object
with no executable bytes, an incomplete strict decode, source and seed drift,
candidate replacement, a competing output, rollback, cleanup, and output
locking. The combined object, bootloader, trampoline, and final-kernel
transaction suite passes 48 tests in 3.144 seconds with one platform-specific
skip.

The complete hostbuild transaction group passes 138 tests in 13.437 seconds
with two platform-specific skips. The focused CupidASM and CupidDis contracts
pass 42 tests in 12.808 seconds with one platform-specific skip. The complete
build-graph module passes all 85 tests in 817.216 seconds.

A forced build with host compiler, assembler, linker, symbol-reader, object
copy, standalone CupidASM, and standalone CupidDis commands set to invalid
names rebuilt both objects and the ISO spanning fixture through the checked
Windows seed. The resulting object artifacts are:

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/cpu/isr.o` | 1,892 | `caa8e1974fbf06857263a743661aae3318abb0b4e10fa154e4ac4994f32464e6` |
| `kernel/core/context_switch.o` | 696 | `8b0fa9415a5f549f6516e3ae4e73d39676d56fb58bbba87d9479610dd95818ea` |

Strict CupidDis accepts both objects with empty output. The rebuild leaves no
candidate, lock, or private transaction directory.

The regenerated audit records six CupidDis participations across the same
452 transforms. Its 2,674,005-byte JSON report has SHA-256
`d9f507246bfdfa2815658599b6dde2b07dbd283fcef2178d241ae0c3864c8c50`.
The 12,269-byte summary has SHA-256
`22dd7276dae52d1e65837425a4a18e5449505a8e612c2c4c43c1f6a4d1fe1b03`.
A fresh deterministic comparison passes in 73.0 seconds.

## Rejected alternatives

Relying only on the final kernel gate was rejected. It cannot roll back an
object that has already reached its public path, and it runs after consumers
have used that object.

Keeping direct Make recipes and adding a later cleanup step was rejected.
Cleanup cannot restore a previous object safely and leaves a window in which
other targets can read the candidate.

Duplicating publication code in both object recipes was rejected. The shared
transaction already owns locking, frozen inputs, drift checks, pinned output
boundaries, cleanup, and atomic replacement.

Treating executable bytes as data was rejected. These two sources are active
kernel code, so a valid production object must contain executable section
bytes and CupidDis must inspect all of them.

## Consequences

CupidASM still owns five production transforms. CupidDis now participates in
six root transforms: kernel symbols, final kernel publication, the two raw
boot paths, and the two assembly objects. Python still coordinates freezing,
structural validation, locking, drift detection, and publication.

The active assembly, object bytes, ABI, kernel link order, and disk layout do
not change. No `.c` source earns a `.cc` rename from this publication change.
The guard proves object structure and complete decoding, not semantic
equivalence to an earlier object. Existing source, relocation, link, and boot
contracts remain responsible for that wider behavior evidence.
