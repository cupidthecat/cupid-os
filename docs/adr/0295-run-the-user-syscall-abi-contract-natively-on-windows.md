# ADR 0295: Run the user syscall ABI contract natively on Windows

## Status

Accepted on 2026-08-15.

## Context

The normal Windows user build already compiled and linked `hello`, `ls`, and
`cat` with the checked PE32 execution seed. Its ABI gate took a different
route. Host Python verified or rebuilt the static Linux Toolchain contract
publication, staged the Linux `user-syscall-abi-contract.elf` through WSL,
and only then allowed the native user compiles to start.

The ABI check depends on six declarations from the kernel and public user
interface. It does not need the complete 21-artifact Linux contract
publication. Requiring that publication on Windows also made an occupied or
stale publication relevant to a read-only user gate.

The checked Windows cohort is still an execution seed. ADR 0278 forbids
treating it as a native bootstrap plan, and ADR 0291 requires source and seed
provenance to remain independent.

## Decision

On Windows, build one private PE32 ABI contract with the checked execution
seed and run it directly. The operation freezes the seed and a 26-file source
and control closure into separate private directories. It compiles the ABI
contract, `ctool_host.cc`, `ctool.cc`, and the Windows runtime with CupidC,
assembles `tool_start.asm` with CupidASM, and links the ordered objects and
reviewed import table with CupidLD.

Every compiled and assembled object must be an i386 `ET_REL` file. The final
image must pass the existing PE32 entry, section, import, and stack checks
before execution. The private executable reads the same frozen six-file ABI
snapshot as the independent Python oracle. Host Python accepts the result only
when the JSON objects match exactly.

The operation rechecks the live Windows seed after every producer call and
again after execution. It also rehashes the live 26-file closure before
returning. A Linux seed, a malformed PE, source drift, seed drift, a failed
contract, invalid JSON, or oracle disagreement stops the gate.

Keep the Linux path unchanged. Linux still verifies or rebuilds the complete
static contract cohort and runs the published ELF contract. The native
Windows path never calls that publisher and never reads or changes its output
directory. An occupied Linux publication therefore remains byte-for-byte
unchanged during the Windows user gate.

`user/Makefile` passes the Windows execution manifest separately from the
Linux bootstrap manifest. Its canonical Windows prerequisite closure contains
the 26 native build inputs and six checked PE seed files. The build audit
records CupidC, CupidASM, CupidLD, the ABI contract, and Host Python on this
edge. Linux fixed-point contract sources retain their own CupidC ownership
evidence instead of borrowing it from the user edge.

## Evidence

The public selector test first failed because `run_user_syscall_abi` had no
Windows manifest parameter. Source-freeze, build-plan, native execution, and
CLI tests then failed at their missing seams. The implementation made each
case pass before the next seam was added.

Focused coverage passes ten user ABI tests. It includes direct PE selection,
an occupied publication sentinel, one shared snapshot, checked producer order,
malformed PE rejection, source and seed drift, Linux seed rejection, CLI
selection, and oracle disagreement. Three focused audit drift tests and the
Makefile contract test also pass.

A real checked-seed invocation completed in 26.5 seconds. The direct PE
reported version 5, 103 fields, a 412-byte table, and 101 providers. Its JSON
matched the Python oracle. `make -C user test-syscall-abi` repeated the same
native result in 27.4 seconds without invoking WSL. `make -C user all`
completed the gate and the checked `hello`, `ls`, and `cat` builds in 31.3
seconds.

The first combined 204-test contract, production, and audit run exposed one
audit coupling after 455.483 seconds. The old broad Windows user prerequisites
had supplied CupidC ownership evidence for the larger Linux contract closure.
Separating that evidence from the native user transform fixed the audit without
restoring false Windows dependencies. The regenerated audit and
`make check-bootstrap-audit` pass; the final checked replay took 66.7 seconds.
Across the three supported roots, CupidC
now participates in 247 transforms, CupidASM in six, and CupidLD in six.
The corrected long-form manifest fixture passed in 209.726 seconds. The final
combined contract, production, and audit suite passed all 204 tests in
855.203 seconds.

## Rejected alternatives

Keeping WSL for this one gate was rejected because the checked PE producers
already build and run the exact small contract on Windows.

Publishing the PE contract beside the Linux contract cohort was rejected.
The ABI gate needs a temporary executable, not a second public Toolchain
cohort.

Using the PE manifest as a bootstrap plan was rejected. The native recipe is
an explicit, reviewed contract build. It does not reconstruct the five tools
or infer a fixed-point plan from the execution manifest.

Using host Clang, GCC, NASM, or a host linker was rejected because the normal
gate must test the checked Cupid producers that own the Windows build.

## Consequences

The normal Windows user ABI gate no longer requires WSL or a current Linux
contract publication. WSL remains necessary on Windows for Linux fixed-point,
and the full Toolchain contract cohort. The artifact-size path did not execute
the Linux seed; ADR 0297 corrects that earlier grouping and records its native
CupidC contract.

Host Python still freezes inputs, launches checked tools, validates artifacts,
compares the oracle, and enforces drift checks. The PE execution seed keeps
its ADR 0278 role, and the Linux contract path keeps its publication and
fixed-point provenance.

This step changes no active C or assembly source. No `.c` file is eligible for
a `.cc` rename, and the `TempleOS/` reference tree remains untouched and
outside the ownership counts.
