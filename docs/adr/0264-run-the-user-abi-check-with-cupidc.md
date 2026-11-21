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

The promoted-seed frontier later rebuilt and transactionally published the
complete 21-artifact cohort. A fresh build in a unique output directory then
passed in 10.492 seconds. It reproduced these six artifact identities:

| Program | Object bytes | Object SHA-256 | Executable bytes | Executable SHA-256 |
| --- | ---: | --- | ---: | --- |
| hello | 6,124 | `64e0a6ee0d7a45a0901d3db614e73481cdc6b30903345c5015601b2bf344be04` | 13,992 | `4c5622969f39ffe7c2427d65abae2d293dfbd76db2aa80c96f9e6cf01613600c` |
| ls | 7,120 | `e0627996a1d9cd6fd428642ffdfada7e07afa81d9267bc714360014af0dd3971` | 18,112 | `094b017eb6914bce6fbc1e99adeae845d5dc05280c1c1d897e68ab9d687c8d79` |
| cat | 6,292 | `ff002fc4710704c3941bf6320249e772a3448d15f99269987ab1b9b608b3acb4` | 13,992 | `b66cba4c98221f5006ad4aeee70349a82db20410e027aa863bc33fa5818b5f4c` |

Each program then ran from a disposable copy of the staged image and returned
0. Hello passed in 54.546 seconds, ls in 52.637 seconds, and cat in 80.043
seconds. Cat read a 62-byte marker-shaped fixture and passed the negative
serial-event boundary. The source and evidence images retained SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
The final four-vCPU E1000 and RTL8139 frontiers passed from that image. Both
used the partitioned USB fixture, `--smp 4`, `--cpu max`, SMP and frontier
runtime verification, a private image, and a 300-second phase timeout. E1000
exited 0 in 725.058 seconds with 103,673 changed framebuffer pixels, 29,608,822
AC97 frames at peak 25,600, and 76,784 PC speaker frames at peak 30,710.
RTL8139 exited 0 in 725.406 seconds with 106,151 changed pixels, 29,601,879
AC97 frames at peak 25,600, and 76,719 PC speaker frames at peak 31,501. Both
used a 640 by 480 framebuffer, and the image hash remained unchanged.

The final active-build audit records 728 inputs and 450 transforms, including
441 under root `all`. Python participates in all 450, CupidC in 245, CupidObj
in 191, CupidASM in five, CupidLD in five, and CupidDis in two. Two
supplemental transforms remain Python-only. `make bootstrap-audit` passed in
64.780 seconds. The current OS boots above now cover the combined promotion;
they do not change this hosted contract's ownership boundary.

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
