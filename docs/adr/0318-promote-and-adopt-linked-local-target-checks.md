# ADR 0318: Promote and adopt linked local-target checks

## Status

Accepted on 2026-08-22.

## Context

ADR 0314 defined strict direct-target validation for static i386 `ET_EXEC`
images. The same repository state also contained private in-kernel raw callback
declarations and a seed-lifecycle repair. The callback parser is not part of
the standalone CupidC tool or either checked seed. The normal kernel publisher
therefore still could not make the new linked-image rule a production
requirement.

Promotion needed to prove more than source-level tests. Each candidate had to
rebuild the complete tool cohort to a fixed point, exercise the expanded
behavior matrix, and validate both real kernel ELFs before any checked artifact
moved.

## Decision

Promote the converged stage-four Linux and native Windows tool cohorts built
from revision `ad7305341003feaa7e630ab7fd45be0a214c4da7`. Both manifests bind
the 50-input snapshot
`73b3fa6964292a7f0b753df3535058dd6399f5e6d8e277a082ac70ce65c79e43`.
The Windows manifest keeps the Linux manifest as its build-plan parent.

Use checked CupidDis twice in output-bearing kernel publication. Keep the broad
`--require-known` scan over all 431 logical inputs. Then apply
`--require-known --require-local-targets` to the frozen pass-one and final
kernel ELFs before CupidObj flattens the final image. Treat both ELF inputs, the
five-tool seed cohort, and the previous raw kernel as one guarded transaction.
Give the combined two-image validation call 600 seconds. The broad scan and
CupidObj call keep their 300-second limits.

Validation-only requests keep the broad scan. They do not flatten or publish a
kernel and therefore do not add a second linked-image call.

Make the active-build audit own the call shape and order. It must find one
broad CupidDis call, one linked-image CupidDis call with the exact flags, and
one CupidObj `flat` call. The linked check must be reachable on every successful
publication path and must guard flattening on status, output, runner failure,
and input or seed drift.

## Evidence

The Linux candidate report has SHA-256
`8838b39e5e2256e75e8b6cc9d3357fcc11cd74912e045971e38562ea5f764709`.
Its stage-three and stage-four generations matched 19 C objects, one startup
object, and five tool images. The behavior matrix passed 21 success, 20 useful
failure, and five help cases.

The native Windows candidate report has SHA-256
`d165af3efd94dc5f314afdd18c1af7e8bb0713b9f6ca39cdec4062b23f8cbee5`.
Its final generations matched 20 C objects, two assembly objects, and five tool
images. The behavior matrix passed seven success, eight useful failure, and
five help cases.

Candidate-built CupidDis accepted both
`kernel/kernel.elf.pass1` and `kernel/kernel.elf` with strict known-instruction
and local-target validation on Linux and Windows.

The promoted Linux manifest is 5,573 bytes with SHA-256
`02ee58c6be6b6f9d2f2e4ab0a07e09fe180d39a18559e5ac3b5faf50078c9d20`.
Its promoted-seed reproof report is 39,329 bytes with SHA-256
`b04c13e5f1544f19c7508c1325f87dc727e4ad32887e2079c96a7e3c063a405a`.
Every checked tool matched stage two, and the final fixed-point and behavior
counts repeated the candidate result.

The promoted Windows manifest is 2,118 bytes with SHA-256
`4d0f4f21ee307a5758b64a2fea163319f79f58287da68bb5bdc78b333cf0aad8`.
Its promoted-seed reproof report is 35,283 bytes with SHA-256
`8f33fd00e909ed076f1b3387b420af1489392b314ee407c50ec0166422e1c246`.
Every checked tool matched stage two, and its final fixed-point and behavior
counts also repeated the candidate result.

Direct checked-seed regressions accept a valid linked image and reject a branch
into the middle of an instruction on both hosts. The production transaction
module covers the successful call order, local-target status and output
failures, runner failure, missing linked inputs, and drift during the linked
check. The generated build-graph audit and its deterministic replay both pass.

The first normal publication attempt preserved the prior raw kernel when the
combined linked-image call reached its old 300-second limit. Isolated native
Windows seed measurements passed the pass-one ELF in 177.774 seconds and the
final ELF in 180.771 seconds. Their 358.545-second total identifies the shared
budget as the failure, so the production and audit contracts now lock the
600-second combined limit.

A source-consistent normal build then compiled the full active graph, linked
both kernel ELFs, and completed broad, linked, and flat-image validation in
3,785.83 seconds. The exact-size gate correctly caught a 28-byte reduction in
the raw kernel after the embedded manual was corrected. With that measured row
updated, the fourteen-artifact contract passed twice at that checkpoint.

A first consistency pass corrected four active embedded manuals, rebuilt their
CupidObj wrappers, and relinked both kernels. Production publication repeated
the broad, linked, and flat checks in 560.05 seconds. Its 9,271,332-byte policy
row passed the fourteen-artifact contract twice before the image was restaged.
A final provenance pass corrected two more active manuals, rebuilt their
wrappers, and repeated publication in 562.55 seconds. The final 9,271,380-byte
policy row passed the contract twice before another image restage. The final
pass-one ELF, final ELF, and raw kernel have SHA-256
`106980d97475d36b7835395a5bbfb43eb1e71484cea631d80dfe47be1acc2ac3`,
`b8e4a34844190b22faf5840a06d32ef961b6835c3af028cc78e34352ffc6bf6d`,
and `e1801128cceeb5a510671684cded5a0aef04220dfafe90fa686df963e7abf37f`.

The final private four-vCPU boot smoke passed the SMP runtime check and the raw
callback marker with one initialized call, one parameter call, one cleared
state, one reassignment, and three calls in total. Its 32,981-byte log has
SHA-256
`502152c8ae22fdb6b4a32159276de58c9368fa5c3a47a1803c2e0ca1da4873f7`.
Audit mutation tests also prove that duplicating or hiding the linked call,
weakening its timeout, suppressing runner failure, or making a status, output,
seed-drift, or input-drift guard nonterminating is rejected.

## Rejected alternatives

Promoting only CupidDis was rejected. A checked seed is one provenance-bound
tool cohort, even when four tool images happen to reproduce unchanged.

Validating only the final ELF was rejected. The pass-one symbol-discovery image
is executable input to the second link and deserves the same structural rule.

Running the linked check after flattening was rejected. At that point the
publisher has discarded the ELF program-header structure the rule needs, and a
failure would come too late in the transaction.

Folding local-target validation into the broad scan was rejected. The 431-input
cohort includes raw files and relocatable objects with different explicit
policies. The kernel publisher owns the narrower linked-image request.

## Consequences

Both checked CupidDis images now carry local-target checks for raw images,
relocatable objects, and linked executables. The normal kernel build rejects an
invalid direct target in either linked image before raw-kernel publication.
This promotion is not evidence that standalone CupidC gained the private raw
callback parser; only CupidDis changed bytes in the five-tool cohorts.

The production path still uses Python for orchestration and transaction
guards. This promotion removes no remaining host coordinator and does not make
the toolchain Python-free. It adds no GCC, NASM, host linker, or object-tool
dependency. `TempleOS/` remains read-only reference material.
