# ADR 0359: Remove the Windows fixed-point CupidC bridge

## Status

Accepted on 2026-08-28.

## Context

ADR 0341 introduced a temporary stage-two bridge after the older native
Windows CupidC exhausted its 32-bit address space while compiling
`cupidc_frontend.cc`. The checked Linux CupidC produced the Windows C objects
through WSL, while the checked Windows seed supplied CupidASM, CupidDis, and
CupidLD. Native stages two and three then produced the compared stages three
and four.

The active Windows seed now contains CupidC with 64 KiB arena blocks. That
size matches Windows allocation granularity and avoids the fourfold virtual
address waste of the older 16 KiB blocks. The temporary bridge should end once
the checked PE compiler can compile the complete current source closure.

## Decision

Use the checked Windows execution seed for every stage-two producer in the
native Windows fixed point. CupidC, CupidASM, CupidDis, and CupidLD all run as
PE32 tools. The checked Linux seed remains the reviewed plan source and paired
provenance input, but none of its executable images runs during the native
Windows reconstruction.

The report names the stage-two producer generation
`checked-windows-execution-seed`. Native stage two still builds stage three,
native stage three still builds stage four, and publication still requires
every final C object, assembly object, and tool image to match. Object-level
and whole-image CupidDis checks are unchanged.

The coordinator continues to freeze and recheck both seed roles. The Windows
manifest selects the execution cohort and names the exact Linux plan manifest.
The Linux manifest supplies the source list, include arguments, link order,
and paired identity. Removing executable use does not weaken that relationship.

## Evidence

The checked Windows CupidC first compiled the current
`toolchain/cupidc_frontend.cc` through the ordinary checked-seed runner. The
command returned zero after about 161 seconds and produced a valid i386
relocatable object.

The report test was written before the coordinator change. It first observed
the old `checked-linux-cupidc-and-windows-execution-seed` producer value. After
the change, the focused report and seed-freeze tests passed.

The complete native Windows bootstrap then ran while every WSL launch returned
`Wsl/Service/E_UNEXPECTED`. It froze 58 source inputs, built 23 C objects and
three assembly objects in each generation, and compared all six tool images.
Stages three and four matched. The behavior inventory passed 13 failure cases,
six help cases, and 18 success cases. The 64,500-byte report has SHA-256
`9393e3eef5274243ea73fae0a0d402b97f928431e29922d4173ee8bd148dd316`.
Its source snapshot has SHA-256
`2cb3345665458cffa9f9f995e2f78008c3b4a80569916994a8124abd7db3b0f3`.

The execution seed matched stage two for CupidC, CupidASM, CupidDis, CupidLD,
and CupidObj. CupidBuild differed because source head contains the native
checked CupidObj runner that has not entered the paired seeds yet. That is the
expected reason for the next seed refresh.

The final `make -j2 all` production build also passed with the checked Windows
seed. It compiled the complete active source and Doom trees, linked both kernel
stages with CupidLD, validated the linked code with CupidDis, accepted all 16
exact artifact rows, and published the disk image. A four-CPU QEMU smoke then
brought every CPU online, passed the in-kernel toolchain self-tests, obtained a
DHCP lease, opened the GUI terminal, and compiled and ran `/bin/ls.cc` with
CupidC.

## Alternatives considered

Keeping the bridge was rejected after the native seed compiled the complete
closure and reached a fixed point. It would preserve a WSL dependency whose
original memory constraint no longer exists.

Falling back to a host compiler was rejected because stage-two code generation
must remain under Cupid ownership.

Changing the compiler source to reduce memory use was rejected. The native
seed already carries the allocator policy intended to remove the bridge, and
the active source should not be reshaped around an obsolete seed limit.

## Consequences

Native Windows fixed-point reconstruction no longer executes a Linux tool or
depends on WSL. WSL is still required on Windows for Linux fixed-point
reconstruction and the remaining static Linux Toolchain contract paths.

The checked seed binaries, normal Make graph, artifact policy coverage, and
source ownership are unchanged. The shorter CTXT changed the flat-kernel policy
row from 9,515,260 to 9,515,232 bytes. This coordinator change does not make an
additional C source CupidC-owned, so no `.c` file changes suffix. `TempleOS/`
remains read-only reference material.
