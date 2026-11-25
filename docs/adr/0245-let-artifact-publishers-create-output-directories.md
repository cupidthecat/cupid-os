# ADR 0245: Let artifact publishers create output directories

## Status

Accepted on 2026-08-08.

## Context

The supported `user:all` and `toolchain:all` graphs each included a Python
transform whose only result was an empty build directory. The user Makefile
created `$(BUILD)` before invoking the checked CupidC publisher. The Toolchain
Makefile created `toolchain/build` before invoking the contract-cohort
publisher.

Those directory nodes did not author an OS object or executable. They also
split responsibility for one artifact between Make and the publisher that
already validates and commits it. Removing the user directory target must not
narrow the public `BUILD` override. Existing one-level and nested paths below
`user/` must continue to work.

## Decision

The checked user compiler and Toolchain contract publisher create the
directories needed for their outputs.

The checked user compiler validates the source and requested output pair
before changing the filesystem. It then opens each directory component below
the repository through a pinned parent identity. POSIX uses
`O_DIRECTORY | O_NOFOLLOW` descriptors and creates missing children with
`dir_fd`. Windows opens children relative to pinned directory handles through
`NtCreateFile` with `OBJ_DONT_REPARSE` and rejects reparse points. The wrapper
keeps those descriptors or handles open until the normal output resolver
proves that the resolved output still matches the original lexical request.
This keeps default,
alternate, nested, and pre-created frontier build paths available without
letting directory preparation escape the repository.

The user Makefile no longer has a standalone `$(BUILD)` recipe or an
order-only dependency on that directory. The checked compiler publisher owns
directory preparation.

The Toolchain contract publisher already validates its dedicated
`cupidc-contracts` destination and creates `output.parent` before opening its
private build workspace. Its Make rule no longer has the redundant
`toolchain/build` order-only prerequisite. The optional native-oracle rules
keep the general `$(BUILD_DIR)` target for their own use.

The active graph counts artifact and verification work. It no longer reports
these two empty-directory preparations as separate Python transforms.

This decision covers safe directory creation at publisher entry. The pins
close after the final lexical and resolved path comparison. The user compiler
does not hold them through the compiler run or publication. A hostile parent
replacement after preparation remains a separate transaction-hardening task.

## Evidence

`python -m unittest -v tests.test_cupidc_production` passed all 49 tests in
28.286 seconds on Windows and 72.331 seconds under WSL. The suite covers a
missing default directory, one-level and nested `BUILD` overrides, a
non-directory collision, output aliases, a redirected resolution, rejected
output pairs and compiler modes, the existing frontier paths, and the
poisoned-host alternate-build gate. Its replacement test proves that a pinned
Windows handle blocks renaming `user/`. On POSIX, child creation stays below
the opened directory and the changed public path is rejected before CupidC
runs.

`python -m unittest -v tests.test_cupidc_toolchain_contracts` passed all 32
tests in 2.576 seconds. Its new failure-path case proves that the publisher
creates its missing parent before bootstrap work and leaves no partial cohort
after the injected failure. The Make contract tests require both
directory-only graph nodes to stay absent.

`python -m unittest -v tests.test_build_graph_audit` passed all 73 tests in
611.219 seconds. The final `make bootstrap-audit` completed in 64.373 seconds,
and `make check-bootstrap-audit` completed in 62.135 seconds. The
`make verify-bootstrap-seed` check accepted all five tools.

The current graph contains 719 active inputs, 255 feature records, 447
transforms, and 25 accounted unreachable files. Root `all`, `user:all`, and
`toolchain:all` contain 438, seven, and two transforms. Python participates in
all 447 transforms. Its three Python-only outputs are the user syscall-ABI
verifier, the Toolchain aggregate verifier, and the Toolchain contract
manifest.

The active-source digest remains
`69f8f0b9bc264f338f445781f92792b24e91f0d641950d3b57f55f74841ae46e`.
The 2,565,353-byte audit JSON has SHA-256
`571cd015cd56c1c0d0ec00b109ab2591dc463a1f8e497ff905745c27d031e306`.
The 12,197-byte summary has SHA-256
`3562338bc156774b3dcbfcd32d13ae1114b2c582cd12827669af2fe9dc03dce5`.

## Rejected alternatives

Keeping both Make directory recipes would preserve graph nodes that describe
host setup rather than artifact ownership.

Allowing the wrappers to rely on pre-created directories would keep the split
transaction and would break clean builds once the Make nodes were removed.

Restricting user output to `user/build` would break the documented `BUILD`
override and the existing frontier workspaces.

Creating the complete requested path without checking each component would
allow an internal link or junction to redirect filesystem changes.

Checking each component by pathname before creating the next one still leaves
a replacement window. A red race test moved `user/` after its check and made
the first draft create a directory outside the repository. Parent-relative
creation through pinned descriptors or handles closes that preparation race.

## Consequences

The three-root graph falls from 449 to 447 transforms. The user graph falls
from eight to seven, and the Toolchain graph falls from three to two. The
number of Python-only transforms falls from five to three.

Cupid ownership does not change. CupidC participates in 245 transforms,
CupidASM in five, CupidObj in 189, CupidLD in five, and CupidDis in one. Every
one of the 438 root outputs still has a Cupid tool owner. Python remains the
launcher and host transaction layer for all 447 transforms, and Windows still
runs the checked Linux seed through WSL.

No ordinary C or assembly source changes ownership, so no `.c` to `.cc`
rename is due. `TempleOS/` remains untouched reference material.
