# ADR 0244: Publish the Doom profile manifest with checked CupidObj

## Status

Accepted on 2026-08-08.

## Context

ADR 0242 defines CupidObj's bounded `profile-manifest` operation and the
`CUPROF1` input envelope. ADR 0243 carries that command in the checked
five-tool seed. The normal Make target still used Python to write the
69,366-byte Doom profile manifest, which left one root output without a Cupid
tool owner.

Production adoption has a host-filesystem boundary that does not belong in a
freestanding object tool. The publisher must discover the live profile
closure, translate native paths for WSL when needed, reject unsafe path
aliases, detect concurrent changes, preserve a valid old file on failure, and
avoid changing the timestamp when the bytes are identical.

## Decision

The normal Make rule depends on `$(CHECKED_SEED_INPUTS)` and passes
`$(BOOTSTRAP_SEED_MANIFEST)` to the profile publisher. The wrapper resolves
the requested output inside the repository, captures the output directory,
and takes an adjacent, no-follow file lock before reading the existing output
or profile inputs. It checks the directory identity again after taking the
lock. Lock files, candidates, and existing outputs must be regular,
single-link files rather than links or junctions.

The wrapper captures the exact Doom source membership and header bytes once.
It derives both the bounded `CUPROF1` snapshot and an independent canonical
JSON oracle from that capture. The wrapper verifies the five-tool seed and
copies it into a private directory. CupidObj runs from that exact frozen
capture and writes a private candidate before Python accepts any new bytes.

Publication requires all of these checks:

1. CupidObj exits successfully and leaves one complete regular candidate.
2. The snapshot retains the same identity and bytes while CupidObj runs.
3. CupidObj's bytes match the independent Python oracle exactly.
4. The checked seed, live profile membership, and captured header bytes remain
   unchanged.
5. The output directory and any existing output retain their captured
   identities.
6. Every retry rechecks the candidate, output, and directory before the
   atomic replacement.

If the checked candidate already matches the public file, the wrapper returns
without replacing it and preserves its timestamp. Otherwise it publishes the
candidate with `os.replace` and verifies the resulting bytes. CupidObj authors
the production bytes. Python owns discovery, native-path checks, seed
freezing, parity, drift detection, locking, and the host transaction.

The atomic replacement is the commit point. The wrapper reports a failed
post-publication check but does not overwrite the committed path again to
restore old bytes. Another writer may own the path by then, and an automatic
rollback would risk erasing that writer's state.

The graph audit independently derives the recursive profile header closure,
requires the exact Make delivery and checked-seed inputs, checks the
publisher's ordered control flow, and refuses a complete rollback of the
production boundary.

## Evidence

The active `CUPROF1` snapshot is 796,337 bytes with SHA-256
`2c22f2dd26a9fdcc41d5972b91c863d103c564c04f74860a0fc500d1fe684941`.
Checked CupidObj reproduces the existing 69,366-byte JSON file with SHA-256
`47ba35158cac0a7df253a0056235223e62fee24df74701800f88763e588611c2`.
The real Make target reports that the file is unchanged and leaves its
timestamp intact.

`python -m unittest -v tests.test_doom_cupidc_production` ran 41 tests in
42.510 seconds on Windows and passed, with the POSIX directory-swap case
skipped because its open lock prevents the rename. The same module passed on
Linux, with only the Windows junction case skipped. The suite covers one
frozen input capture, execution from the exact frozen seed, byte parity,
failed and malformed candidates, header and membership drift, competing
publishers, linked publication state,
candidate and output drift during retries, output-directory replacement,
cross-process locking, the post-publication commit point, unchanged timestamps,
CLI forwarding, and the checked Make prerequisites.

The checked-seed runner tests prove that a supplied frozen capture is executed
without a second freeze while the live trust unit is still checked after the
tool returns. The complete checked-seed module passed all 46 tests in 853.126
seconds. The complete checked CupidC kernel-wrapper module passed all 32 tests
in 92.658 seconds. Python bytecode and Ruff checks pass for the changed Python
modules.

The regenerated audit completed in 61.084 seconds, and its independent stale
check passed in 60.909 seconds. The delivery has 300 exact inputs: 291 profile
headers and nine control files. Its operation is `generate_profile_manifest`,
with `cupid_object` and `host_python` as participants. The graph remains at
719 active inputs, 449 transforms, 255 feature records, and 25 accounted
unreachable files. Its active-source digest remains
`69f8f0b9bc264f338f445781f92792b24e91f0d641950d3b57f55f74841ae46e`.
The 2,566,111-byte JSON has SHA-256
`9e8dc28b6b0b6ba611b53d8bbe67930495d4cfc6bb509b1333d3da0082c23289`,
and the 12,197-byte summary has SHA-256
`2bb3d92e713fa98ab5c2c6cb9ad0f2beb212f00c73a6623d71a3bd170a003f0e`.
The complete graph-audit module passed all 73 tests in 630.147 seconds,
including structural and whole-feature rollback mutations.

A complete normal `make all` passed in 1,537 seconds. The working image reused
its existing FAT data and staged the checked ISO. A private-image `ls` boot
smoke then passed in 49.080 seconds; its 27,819-byte serial log has SHA-256
`8d75f89996654d2d11fff625ad83dcc3d9043e89319db46b9cf04dc78e6e67b9`.

## Rejected alternatives

Keeping Python as the sole normal author would leave the final Python-only
root output in place after the checked seed had gained the required command.

Letting freestanding CupidObj walk native paths would mix host discovery,
link handling, WSL translation, and publication policy into an object tool.

Trusting CupidObj without an independent byte oracle would make a bad seed
capable of publishing a plausible but incorrect manifest. Running Python
first and treating CupidObj as a validator would leave Python as the byte
author.

Publishing without a per-output lock and final drift checks would allow two
writers or a late input change to replace a valid manifest with stale bytes.

## Consequences

Across the three supported roots, CupidObj participates in 189 transforms.
Every one of the 438 root outputs has at least one Cupid tool owner, so no root
output is Python-only. Python still participates in all 449 transforms as the
checked-tool launcher and host-side safety, parity, and publication layer.

This handoff does not change ownership of ordinary C or assembly source, so no
`.c` to `.cc` rename is due. `TempleOS/` remains untouched reference material.
