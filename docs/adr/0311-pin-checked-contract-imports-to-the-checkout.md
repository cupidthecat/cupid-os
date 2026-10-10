# ADR 0311: Pin checked contract imports to the checkout

## Status

Accepted on 2026-08-21.

## Context

The artifact-size and Toolchain manifest contracts use Python only for their
filesystem, process, and publication boundaries. Each launcher imports the
contract policy and checked-tool runner from this repository's `tools` package.

Direct script execution sets Python's first search path to the script's
directory, not the repository root. On the WSL host used for the complete
Toolchain gate, Python found an unrelated installed package also named `tools`.
The Cupid-built author and Python oracle had already agreed on all 58 stage
pairs, and the cohort had been published, but the final read-only `CUPMAN2`
verifier stopped at import time.

## Decision

Before either checked contract launcher imports `tools`, derive the repository
root from the launcher's resolved `__file__` path and put that directory first
in `sys.path`. Keep the existing sibling-module fallback for a launcher copied
outside the repository package layout.

Apply the same rule to `artifact_size_contract.py` and
`toolchain_manifest_contract.py`. Both are normal-build trust boundaries and
must resolve the policy and bootstrap modules from the checkout being checked,
regardless of the current directory, user site packages, or `PYTHONPATH`.

## Evidence

A subprocess regression for each launcher places a package named `tools` on
`PYTHONPATH` whose initializer raises immediately. Each test starts the real
script from `toolchain/` and requires a clean help response without importing
the shadow package. The first versions followed the same wrong-package
resolution path as the full WSL gate. Both pass after the repository-root pin.

The artifact-size runner passes 16 tests in 1.102 seconds with four expected
Windows skips. Its policy module passes 13 tests in 2.333 seconds. The Toolchain
publisher passes 62 tests in 7.266 seconds. The direct manifest contract passes
40 tests in 40.828 seconds, including the checked stage-four author, and the
pinned verifier runner passes 25 tests in 32.773 seconds with three expected
Windows skips.

The first source-current `make -C toolchain all` attempt reached a valid
21-artifact publication but failed its last read-only verifier after 3,976.96
seconds because WSL loaded the installed `tools` package. Running that exact
verifier after the fix printed `Cupid Toolchain manifest: ok (21 artifacts)`.

The complete unmodified-environment rerun passed in 3,989.13 seconds. The Cupid
author and Python agreed on all 58 stage pairs, the hosted runtime passed, live
inputs remained frozen, the cohort published atomically, and the final
`CUPMAN2` verifier accepted all 21 artifacts. The 27,071-byte schema-v3 manifest
has SHA-256
`615cdfd4095d684f31684b9887ba9610c033513580e7332d2d153841947c9311`.

## Rejected alternatives

Do not depend on the caller's current directory or an inherited `PYTHONPATH`.
The normal build must work from its own Make directory and under a developer's
ordinary Python installation.

Do not catch every `ImportError` and fall back. That could hide an error inside
the repository package or silently accept modules from another location. The
launcher chooses the checkout before importing instead.

Do not move policy or verification back into Python to avoid the launcher. The
launchers retain only the host work already assigned to them; the checked CupidC
contracts continue to make their semantic decisions.

## Consequences

Both checked Python launchers resolve their policy, bootstrap, and publisher
modules from the repository that contains the invoked script. An installed
same-name package can no longer change a normal build's trust boundary.

This adds no build owner, manifest field, seed promotion, guest ABI, or host C
dependency. Python remains responsible for filesystem safety, process launch,
the independent oracle, and atomic publication. `TempleOS/` remains read-only
reference material.
