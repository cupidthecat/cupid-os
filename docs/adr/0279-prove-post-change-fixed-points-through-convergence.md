# ADR 0279: Prove post-change fixed points through convergence

## Status

Accepted on 2026-08-13. The clean Linux convergence gate and seed promotion
are complete under ADR 0280. The native Windows clean proof and promotion remain
pending.

## Context

A checked seed may predate a compiler change that affects generated object
bytes. In that case, stage two is built by the older seed, while stage three is
built by the changed stage-two compiler. A difference between those stages can
be the expected transition to the new code generator rather than evidence of a
nondeterministic compiler.

The new large-frame stack probes exposed this distinction. A source-stable
native Windows run stopped safely at the `cupidobj_main` object comparison
after 821.9 seconds. The Linux run reached the same comparison and stopped
safely after 883.3 seconds. Neither run published a fixed-point bundle. The old
driver compared only stage two with stage three, so it could not tell a valid
one-generation codegen transition from failure to converge.

## Decision

Both fixed-point drivers build through stage four. Stage two is produced by the
checked seed. Stage two produces stage three, and stage three produces stage
four. Stage two and stage three are transition generations. The fixed-point
comparison is stage three against stage four.

The comparison covers every planned C object, each startup or assembly object,
and all five linked tool images. Behavior checks run against stage three and
stage four. Publication includes all three built stages, behavior evidence,
and one report only after the byte comparison, behavior gates, and repeated
source and seed checks succeed.

Seed validation is a boundary check, not only an entry check. The Linux driver
reloads the live Linux manifest and its five artifacts at every generation
boundary and immediately before publication. The Windows driver applies the
same rule independently to its PE execution seed and Linux plan seed. A changed
manifest or artifact aborts the run without publishing a bundle.

Linux behavior evidence names the stage-three and stage-four Windows
`cupiddis_main` objects in its inventory. The audit follows each object from
its Windows-profile compile through replacement in the reconstructed behavior
link. This prevents a report from proving the final PE while leaving the
driver-main inputs implicit or using an object from the wrong profile.

The native operator first runs `make verify-windows-bootstrap-seed`, then
`make bootstrap-windows-from-seed`. The proof publishes only to
`build/bootstrap/checked-windows-seed` after every convergence and behavior
gate succeeds.

## Evidence

The 821.9-second Windows failure and 883.3-second Linux failure both identified
`cupidobj_main` at the old stage-two versus stage-three boundary. They are
evidence that the earlier comparison rule was inadequate. They also confirm
that the transaction stopped before publication.

Source head now contains the stage-four build, stage-three versus stage-four
comparison, report fields, and publication boundary for the Windows and Linux
drivers. Behavior checks and their nested artifact labels now name stages three
and four, and the audit rejects a return to transition-stage labels.

A native Windows unittest reached stage four without reporting a mismatch,
then timed out at exactly 1,200 seconds. One child briefly retained the private
proof tree. The exact process tree was stopped, and the abandoned proof
directories were moved to the Recycle Bin. The integration harness now gives
each fixed-point subprocess 2,400 seconds.

The direct Windows proof ran from 19:53:29 to 20:14:12, a total of 20 minutes
43 seconds. It passed with 20 C objects, two assembly objects, and all five
native tools identical between stages three and four. Both stages passed five
help cases, five successful operations, and five useful failures. The direct
Linux proof ran from 19:53:29 to 20:17:51, a total of 24 minutes 22 seconds.
It passed with 19 C objects, one startup object, and all five static tools
identical between stages three and four. Both Linux stages passed five help
cases, 18 successful operations, and 16 useful failures.

Both reports bind the same 50-input source snapshot with SHA-256
`d8481a39e0d1c7f42779a8c9f5fc5de10d7e5b9bc4df63ce6afe9ddd9c9716da`.
Both record `stage3=4`.
Independent verification rehashed every reported inventory member and checked
each recorded size and hash. These reports remain preliminary because they
began from an uncommitted source tree. A later clean Linux proof satisfied that
target's promotion gate. Native Windows still requires its clean proof.

The preliminary Linux report also reconstructs native Windows behavior. That
cross-path check exposed a CupidDis-only mismatch: the reconstructed
387,584-byte image
had SHA-256
`ad6147cd426e204756ec8bf52ae85c64fff9ad39b0bc26e5744f3c421be1e9aa`,
while the native Windows proof produced the same-size image with SHA-256
`07cff807224c425d686e32d54dc1ad541f57aaa624f7b736bba0f9ef5001ce6a`.
The other four Windows tools matched. That reconstruction compiled
`cupiddis_main.cc` without `_WIN32=1`, so it selected the wrong driver path.
The hosted Windows plan now applies `_WIN32=1` to all five tool mains. A
test-first compile and link parity contract covers CupidDis, and the build
audit rejects a plan that drops that definition.

The Make dry run and two Make contract tests pass for the verification and
bootstrap entry points.

Six runtime mutations cover one manifest and one artifact in each live seed
cohort: Linux, Windows execution, and Windows plan. The first five cases
reproduced the unsafe transition into stage three. The completed matrix passes
inside a five-test focused command in 2.294 seconds. The audit requires five
seed checks in each driver, including the prepublication check, and binds both
reconstructed Windows `cupiddis_main` objects to their compile and replacement
paths. Its complete mutation test passed in 119.241 seconds. These checks
hardened the later clean Linux proof. Native Windows still needs the same proof
from a named clean commit.

The clean Linux proof from revision
`5d690c7508cc031a0cb32b2963bf16300b32e267` passed in 1,383.775 seconds.
Stages three and four matched across 19 C objects, startup, and five tools,
then passed the 5/18/16 behavior matrix. The promoted-seed reproof passed in
1,411.998 seconds with all five initial seed comparisons true. ADR 0280 records
the promoted stage-four identities and manifest provenance.

## Consequences

A compiler change may alter stage two as the old seed crosses into the new
code generator. Stage three must still reproduce the same objects and tools
when stage three builds stage four. The uncapped runs demonstrate that
convergence for one frozen, uncommitted source snapshot. Bootstrap work now
takes one additional generation. The clean Linux proof and seed promotion are
complete; native Windows is the next clean promotion gate. Any path that
reconstructs native Windows tools must also apply the complete Windows profile
to each driver main, including CupidDis. Each driver must keep every live seed
role equal to its frozen capture until the publication boundary.
