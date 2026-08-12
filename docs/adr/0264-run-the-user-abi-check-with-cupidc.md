# ADR 0264: Run the user ABI check with CupidC

## Status

Accepted on 2026-08-12.

## Context

The external-program build already used CupidC and CupidLD for its three
tracked programs, but its precompile ABI check was still a Python-only
semantic transform. That checker snapshots six kernel and public declarations
and verifies the reviewed i386 syscall table, scalar types, constants, record
layouts, and provider list. The check protects a production boundary. The ABI
rules therefore need a Cupid-built executable implementation.

The checked Toolchain publisher already compiles static i386 contract programs
with stage-two and stage-three CupidC, compares their objects and executables,
links them with CupidLD, and publishes them as one verified cohort. This gives
the ABI check an existing fixed-point path without weakening the source it
validates.

## Decision

Add `toolchain/tests/user_syscall_abi_contract.cc` as the fifteenth regular
Toolchain contract. The contract snapshots and rereads the six ABI inputs,
then checks version 5, 103 fields, the 412-byte table, public scalar types and
constants, both VFS record layouts, and all 101 function providers. Its report
includes the reviewed field and provider fingerprints.

`user/test-syscall-abi` verifies or rebuilds the complete checked cohort. The
coordinator copies that verified publication to a private directory and runs
the Cupid-built contract from the copy. It also copies the six ABI inputs once.
The Cupid contract and the existing Python oracle read that same snapshot,
while the contract rereads the live tree before returning success. The
coordinator compares both reports, then verifies the live publication and all
of its inputs again. A concurrent source or publication replacement therefore
fails closed.

Python keeps path, link, mutation, launch, and publication safety duties, but
it is no longer the only implementation of the ABI rules.

The active-build audit records the transform with both `cupid_c_contract` and
`host_python` participants. The Toolchain publication contains sixteen
compared executables, seventeen compared objects, five tools, and 21 published
artifacts. Its frozen inventory contains 58 inputs.

## Consequences

The supported graph has two Python-only supplemental transforms instead of
three. The ABI gate still requires host Python to coordinate the checked seed
and independent oracle, and Windows still runs the i386 Linux contract through
WSL. Removing those launch and safety roles belongs to the later native
Windows and Python-free bootstrap stages.

The C contract intentionally bounds each source at 1 MiB, token storage at
32,768 entries, field and assignment tables at 128 entries, and each digest
input at 64 KiB. Inputs outside those limits fail with a diagnostic. The
Python oracle retains its stricter path and symbolic-link checks.

An initial full-graph audit exposed an overly broad operation classifier: any
transform that listed the Python ABI oracle was labelled as the ABI verifier.
The new contract publisher legitimately freezes that oracle, so its manifest
was mislabelled. The classifier now also requires the exact
`test-syscall-abi` output. A regression keeps the user gate and Toolchain
publisher distinct.

No `.c` source moved with this decision. The new contract was written as
`.cc`.

## Evidence

The focused ABI, publication, and graph-audit suite covers the valid report
plus field, scalar, constant, layout, provider, reread, selector, publication,
and fixed-point failures. The six executable mutation cases now compile the
contract with checked-seed CupidC, compile the hosted runtime with CupidC,
assemble startup with CupidASM, link with CupidLD, and run the resulting i386
ELF under WSL. The positive report matches the Python oracle. Publication-race
coverage proves that execution uses the frozen ELF and rejects a live
replacement afterward.

The active graph records all 58 publication inputs, all 43 bootstrap sources,
and all six seed files as an exact 85-path union. Together with `user/Makefile`,
the ABI transform has 86 declared inputs. Missing, unexpected, and drifted
members fail the independent audit.

A fresh complete cohort passed in 2,519.5 seconds. All seventeen object pairs
and sixteen executable pairs match between stages, the hosted runtime passes,
and all 58 frozen inputs survive the final live-input check. The publication
contains 21 artifacts. Its manifest has SHA-256
`be2a6fd21a0d93721be1a0663780899ed089f23f034b1b7e75c88410f8a4890b`,
and the published ABI executable has SHA-256
`4bb2a5caa2b4aeb92592df2af6b3c059ec94a2f1834c8c28075dcb92d536a3bb`.
The independent verifier accepts the publication, and the normal `user-abi`
command reproduces the reviewed ABI and provider fingerprints.

The regenerated active-build audit and its independent drift check pass. The
graph records 728 inputs, 449 transforms, and two remaining Python-only
supplemental transforms. No kernel object, image byte, boot path, or ABI
transport changed, so an OS boot would not add evidence for this hosted
contract transfer.

## Rejected alternatives

- Keeping the ABI rules only in Python would leave the semantic production
  gate outside the staged Cupid toolchain.
- Removing the Python checker would discard an independent oracle and its
  stricter path, link, and publication checks.
- Building a one-off ABI executable outside the contract cohort would bypass
  the existing stage identity, input freezing, and atomic publication rules.
- Running the live published ELF after verification would leave a replacement
  window between hashing and execution.
- Letting the Cupid contract and Python oracle read separate live snapshots
  would allow a concurrent edit to produce reports for different inputs.
