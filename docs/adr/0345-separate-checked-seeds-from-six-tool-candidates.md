# ADR 0345: Separate checked seeds from six-tool candidates

## Status

Accepted on 2026-08-25.

## Context

The checked Linux and Windows manifests describe five trusted tools. They do
not contain CupidBuild, so they cannot prove a build that depends on a checked
CupidBuild image. At source head, however, CupidC can compile CupidBuild and
the hosted runtime can link it without a host C library.

Changing the v1 tool inventory in place would break verification of the
promoted seeds and make their recorded build plans false. It would also blur
two different roles: the five images trusted as bootstrap inputs and the six
images being tested as the next candidate generation.

## Decision

Keep the v1 seed verifier and checked-tool runner limited to the exact five
manifest tools. Derive a separate candidate plan from the verified Linux build
plan. The candidate adds `cupidbuild.cc`, `cupidbuild_host.cc`, and
`cupidbuild_main.cc`, then links CupidBuild beside the existing five tools.

Use that candidate plan for public Linux and native Windows bootstrap runs and
for the private bootstrap used by the Toolchain manifest author. Compare all
six stage-three and stage-four tool images. The initial seed-to-stage-two
comparison remains limited to the five images that the checked manifest
actually names.

The Linux candidate freezes 58 source inputs and builds 22 C objects, one
startup object, and six tools. The native Windows plan adds its publication
runtime and three startup objects. CupidBuild's Windows link uses its exact 29
`KERNEL32.dll` imports plus `NtSetInformationFile` from `NTDLL.dll`.

Extend the Toolchain publication contract to include
`cupidc-cupidbuild.elf`. Its fixed-point evidence covers 22 artifacts and 62
stage pairs: 17 contract objects, 16 contract executables, 23 bootstrap
objects, and six bootstrap tools. Publication verification recreates the
candidate plan from the checked v1 plan before it recaptures the 58-file live
source closure.

Do not change either checked seed, its schema, artifact-size policy, or any
normal OS recipe in this step.

## Evidence

The candidate-plan tests cover exact Linux and Windows inventories, reserved
source names, Windows imports, a real CupidBuild startup assemble-and-link
call, six-tool stage construction, fixed-point mismatch rejection, behavior
selection, and manifest-author reporting. Both checked v1 manifests still
verify with five tools.

The publication coordinator passes 65 tests, and the Cupid Toolchain manifest
contract passes 40. A focused red test first showed that publication
verification recaptured the five-tool plan and rejected a valid candidate
source inventory. The verifier now derives the candidate plan, and the live
source, backdated-drift, and retained-manifest cases pass.

An earlier review also found that the first Windows candidate import list
omitted `FindClose`, `FindFirstFileA`, and `FindNextFileA`. The exact import
contract and real link test now cover those procedures.

The complete Linux candidate passed with all 22 C objects, the startup object,
and six tool images equal between stages three and four. Its behavior inventory
was 21 failures, six help cases, and 22 successes before the guarded-command
review follow-up. The report binds the 58-file
closure with SHA-256
`497cd80f8491d6952ae6c86c12f4838db05b4a4f9a542d3bfd5755be21304878`
and candidate-plan SHA-256
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`.
The report itself has SHA-256
`382f81574d57442c796f926123ab85468d1765d5fea18c90180ab88d6f3f8312`.

The native Windows candidate also passed. Its final stages match across 23 C
objects, three assembly objects, and six PE images. Its earlier behavior record
had ten failures, six help cases, and nine successes. It binds the same source closure and
candidate-plan SHA-256
`7a2122156d60d4dfc67319018e6ae922b117a7fa135224e86e4f9a912228bed9`.
Its report has SHA-256
`722b615a6ec5c237668ed05dcb6e8b32b016ba215c988906d2cda77509b2ca97`.
The final CupidBuild images are 260,064 bytes on Linux and 280,576 bytes on
Windows.

The active-build audit first rejected its five-tool behavior and publication
locks. After extending those locks, the fail-closed fixed-point sweep, the
candidate-artifact closure test, `make bootstrap-audit`, and
`make check-bootstrap-audit` all pass. The generated audit records 22 C
objects, startup, six images, and both candidate behavior inventories.

Review then found that those inventories ran only CupidBuild's CLI cases. Both
drivers now run its real guarded object transaction with the two compared
images, require matching relocatable output, and prove missing-source rollback.
Fresh complete Linux and native Windows fixed points pass with those checks.
The expanded behavior inventories are 22/6/23 and 11/6/10 for failure, help,
and success cases. The Linux report has SHA-256
`3a606c9daa865c610f2e07ce9bf77c60bd6ac884b0cc223d4f2e557dae353bc3`;
the Windows report has SHA-256
`6023b001f1e9441db44b0b988e22b1ead69cb484a2d640d0dea1054c88afcbae`.
The audit also binds candidate-plan derivation in both drivers and publication
recapture. It requires one live module-scope definition for the candidate
source and link constants, so a later conditional reassignment also fails
closed. The helper must defensively copy every checked source, retain the five
checked link lists, and write the finished candidate's `sources` and `links`
fields exactly once. Negative mutations replace both initializers and add
later overwrites.

The complete Toolchain publication then passed. The Cupid author and Python
oracle agreed on all 62 stage pairs, and the final verifier accepted all 22
artifacts. The published cohort binds 75 publication inputs, the 58-file
candidate closure, 17 contract-object comparisons, 16 contract-executable
comparisons, 22 bootstrap C objects, one startup object, and six tool images.
Its 29,270-byte manifest has SHA-256
`d2215c289025cf78cb36e6f309bca0f7aaa056ff844d607e665e20efa73d4d0e`.
This publication carries `cupidc-cupidbuild.elf`; it does not add CupidBuild to
the checked input manifest.

## Consequences

Source head can construct and compare a six-tool candidate without pretending
that CupidBuild was already trusted as an input. The Toolchain publisher can
carry CupidBuild and its fixed-point evidence in the same atomic cohort as the
other hosted tools.

This is preparation for promotion, not a promotion. A new manifest contract
must describe six-tool trust without making CupidBuild attest to provenance
that depends on the same image. Both normal guarded object recipes still use
the Python publisher, and host Python still coordinates the fixed point.

No active `.c` source is eligible for renaming in this change. All three new
candidate roots already use `.cc`. `TempleOS/` remains untouched reference
material.
