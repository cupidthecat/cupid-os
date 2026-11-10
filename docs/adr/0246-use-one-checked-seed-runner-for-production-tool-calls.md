# ADR 0246: Use one checked-seed runner for production tool calls

## Status

Accepted on 2026-08-08.

## Context

ADR 0190 defines the checked-seed invocation used by root Cupid tools. The
runner freezes the manifest-bound five-tool cohort, executes one private
image, and checks the live manifest and all five images again after the
command returns. ADR 0244 added a caller-supplied frozen capture so one
transaction could use the same verified seed snapshot for several checks.

The production kernel compiler still carried its own Linux and WSL executor.
The generated-install and user compiler imported that executor, while the
user linker called its runner directly. These paths staged private tools, but
they did not all apply the live post-run five-image check. A successful tool
could therefore publish after the public seed changed during execution.

The duplicate executor also owned a second copy of WSL discovery, `wslpath`
translation, private shell staging, subprocess policy, and native Linux
execution. That split made the trust contract depend on which wrapper launched
the tool.

## Decision

The checked branches of the kernel compiler, generated-install and user
compiler, and user linker delegate execution to `run_seed_tool`. Each wrapper
passes the five-tool capture it already froze, so the runner does not open a
second trust unit. The selected private image runs directly on Linux or
through WSL on Windows.

Before returning a result, the runner reloads the live manifest, verifies all
five images, and requires the manifest identity to match the frozen capture.
A changed live cohort rejects the command even when the private tool returned
success. This includes drift in a tool that the current command did not
execute.

Host filesystem operands use absolute `Path` values. `ToolRunner` translates
those values for WSL. Logical Cupid paths such as `/kernel/...`, `/.output/...`,
and `/user` remain strings and enter the tool unchanged. Native Windows oracle
branches continue to use `NativeToolExecutor` and do not enter the checked
Linux-seed runner.

Each wrapper keeps its existing source snapshot, ELF validation, output-path
checks, drift checks, and atomic publication. Timeout and launch failures on
the checked paths retain the source or input name from the former wrapper
diagnostics.
Native oracle paths retain their own tool-snapshot checks.

The active build audit checks the optional injected-runner seam, the ordered
execution and live-cohort validation in `run_seed_tool`, each wrapper's
caller-owned frozen capture, the native/checked branch split, and the order
from freeze through publication. It requires direct, reachable assignments,
single execution bindings, and a raising manifest-mismatch branch. The audit
identifies the production root from stable build inputs rather than mutable
domain prose, so a documentation edit cannot disable the contract.

## Evidence

The initial four-test red gate ran in 0.387 seconds. The shared runner rejected
the injected `runner` keyword, and the three production wrappers accepted a
successful result after the live seed changed. The completed gate rejects
CupidC or CupidLD drift and preserves each existing object or executable.

The shared-runner tests also execute CupidASM while changing live CupidLD.
The complete five-tool reload rejects that unselected-tool drift. Focused
negative tests preserve source-aware timeout and launch-error diagnostics for
kernel CupidC, user CupidC, and user CupidLD.

A separate gate changes only the live manifest's whitespace after the private
tool returns. The parsed tool inventory remains valid, but the raw manifest
identity differs and the runner rejects the result. Compiler and linker tests
also retain absolute `Path` operands for host filesystem roots, inputs, and
outputs while logical Cupid paths remain strings.

Audit mutations keep the expected calls as dead markers, replace the raising
manifest check with `pass`, rebind or mutate the frozen cohort, shadow the
runner and freezer, decorate the exported function, turn a wrapper into a
generator, suppress transaction failures, and publish through an early alias.
The audit rejects each form. Removing mutable domain documentation does not
turn off the production-root gate.

The Windows kernel-wrapper module passed all 34 tests in 91.876 seconds. The
Windows production compiler and linker module passed all 53 tests in 30.361
seconds. The same 53 tests passed under WSL in 77.510 seconds. The immutable
production-input module passed all three tests in 0.050 seconds, the Doom
production module passed 41 tests in 46.890 seconds with one platform-specific
skip, and the freestanding code-generation module passed all four tests in
4.908 seconds.

The complete checked-seed bootstrap module passed all 49 tests in 903.145
seconds. The complete build-graph audit module passed all 75 tests in 640.680
seconds. `make verify-bootstrap-seed` accepted all five checked images in
0.312 seconds.

The regenerated audit retains 719 active inputs, 447 transforms, 255 feature
records, and 25 accounted unreachable files. Its active-source digest remains
`69f8f0b9bc264f338f445781f92792b24e91f0d641950d3b57f55f74841ae46e`.
The 2,565,353-byte JSON has SHA-256
`51ace6a254ee3b7234eea1d7839d26ded488e93708c097d446173a453dfb4c4c`.
The unchanged 12,197-byte summary has SHA-256
`3562338bc156774b3dcbfcd32d13ae1114b2c582cd12827669af2fe9dc03dce5`.
`make bootstrap-audit` regenerated those files in 64.205 seconds, and
`make check-bootstrap-audit` accepted them in 64.645 seconds.

The normal OS build passed twice, first in 1,584.955 seconds and then in
1,509.787 seconds after the documentation changes. Both runs produced the
same artifacts:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf` | 9,077,256 | `b776e84adfe060afe2a2fec69a8a817c6af90c258199b6db91ac845bf640a355` |
| `kernel/kernel.bin` | 8,869,076 | `309c590836f191e261769b9417917facc7e09b90fdc2759decd4e874f342d1ab` |
| `cupidos.img` | 209,715,200 | `7abb6c8734f15852962435f9cf201548d1ffd746596186df9b9522e0884bad66` |

A private boot smoke of those exact image bytes passed in 47.666 seconds.
CupidC compiled `/bin/ls.cc` to 911 bytes of code and 71 bytes of data, then
completed its JIT execution. The 27,819-byte boot log has SHA-256
`15dbac4b481fbfcecb23979fe0ca11b1cbfafd3894c07350ae203e6dffffbe40`.
The checked user compiler and linker build also passed in 5.954 seconds.

## Rejected alternatives

Keeping the duplicate kernel executor would leave WSL and subprocess policy
split between two modules and would not add the missing live-cohort check to
the compiler and linker wrappers.

Adding a separate `verify_seed_inputs` call to every wrapper would duplicate
the trust rule and make later callers responsible for preserving its order.

Executing a live seed image would allow a replacement between verification
and process creation. The runner executes only the private captured image.

Freezing again inside `run_seed_tool` would separate tool execution from the
seed capture already owned by the wrapper transaction.

## Consequences

One runner owns direct Linux execution, WSL staging on Windows, and the live
five-tool recheck for root tools, checked production CupidC, and checked user
CupidLD. A successful compile or link cannot publish when that post-run check
detects live seed drift.

This change transfers no artifact ownership. The graph stays at 447
transforms, with CupidC at 245, CupidASM at five, CupidObj at 189, CupidLD at
five, CupidDis at one, and Python at 447. The three Python-only supplemental
outputs remain. Python and WSL are still part of the host control plane, and
native Windows tools remain optional oracles.

No ordinary C or assembly source changes ownership, so no `.c` to `.cc`
rename is due. `TempleOS/` remains untouched reference material.
