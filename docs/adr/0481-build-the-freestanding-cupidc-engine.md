# Build CupidC as a freestanding ELF module

- Status: Accepted
- Date: 2026-11-17

Checked CupidC already compiles the full compiler source under its
freestanding profile. The initial fixed-point experiments built their entry
and loader from archived scripts. Keep the module entry in source control and
give it an explicit build so the guest port can use the same compiled core.

`make -C bootstrap/guest` uses the checked native seeds to compile the nine
units and link a static i386 ELF at the start of the reserved CupidC arena.
The caller sets a version and request size, then supplies the compiler's
ordinary job callbacks and options. The entry validates the ABI before opening
a job. One call preprocesses, parses and emits a relocatable object through
the shared invocation transaction.

The module's sources live under `bootstrap/guest`. An initial placement under
`toolchain` put the new header inside the Doom profile's recursively scanned
include roots. The active build audit exposed the extra input before it could
become a production dependency.
The build writes to top-level `build/guest-engine` so its transient test
directories are outside the Doom compiler's scanned roots.

Linux verifies the loaded module's behavior and compares its complete next
generation. Using CupidBuild's generic runner for the harness startup assembly
failed because that installed command accepts CupidC, CupidObj and CupidLD.
The startup recipe uses the existing checked CupidASM runner. The Linux
loader accepts CupidLD's empty GNU stack header and checks every load range
and the file-backed executable entry before mapping.

Cupid OS still needs a worker stack and an exclusive claim on the compiler
arena. The guest loader needs to release that claim and the compiler's
allocations after scheduler detachment. The existing ELF loader's permanent
image ownership cannot provide that lifetime. See the
[engine record](../bootstrap/FREESTANDING-COMPILER-ENGINE.md) for reproduction
commands and test results.
