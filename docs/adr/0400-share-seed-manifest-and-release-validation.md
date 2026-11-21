# ADR 0400: Share seed manifest and release validation

Date: 2026-09-22

Status: Accepted (2026-09-23)

## Context

Native artifact verification needs to validate both seed manifests and bind
their artifact identities to an independently reviewed release. CupidBuild's
manifest reader was private to its execution transaction and selected only the
host format at compile time. Copying it into another command would leave two
implementations of the same seed rules.

## Decision

Move the reader into `seed_manifest.cc`. Its byte API takes an explicit format
and returns owned, role-ordered filenames, sizes and SHA-256 identities. Failure
clears the entire result. Diagnostics are caller-owned and bounded. Both formats
are available on either host; CupidBuild still selects its own host format for
execution and retains its existing capture, launch and publication checks.

Decode JSON string values and keys before comparing them. Equivalent escaped
forms are valid; duplicate decoded keys, wrong decoded values and embedded NULs
remain invalid. Keep the historical five-tool v1 contract and installed six-tool
v2 contract. Manifest input is limited to 1 MiB and artifact sizes to the existing
64 MiB image limit on both 32-bit and 64-bit callers.

Add `seed_release.cc` for an external `cupid.seed-release.v1` record containing
the reviewed source, parent and plan identities and all twelve artifact
size/digest pairs. This record avoids embedding a finished CupidBuild digest in
the source that produces that same executable. Release parsing and matching
are byte-level operations; they do not authenticate the release file or prove
the lifetime of any file observation.

The pair API runs both structural readers and release matches. It also computes
the SHA-256 of the actual supplied Linux manifest bytes and checks the Windows
manifest's binding to those bytes. A caller-supplied digest is insufficient.
Production links use CupidBuild's existing SHA implementation.

The candidate plan appends three C modules: `seed_manifest`, `seed_release` and
`contract_parse_internal`. Its source inventory has 66 files and its Linux plan
has 25 C sources; Windows adds its publication runtime for 26. The installed
59-input release and its original plan remain independently pinned. The shared
reader admits the 66-input candidate only with the complete new plan; mixing
installed and candidate plan identities is rejected.

Toolchain publication must distinguish the installed seed's plan from the
candidate's plan. The manifest author validates the former and reports the
latter after checking the reviewed candidate inventory and stage pairs. The
publication inventory has 80 files and checks 65 stage pairs.

## Evidence and remaining work

Before integration, the private byte callers passed 4,546 cases with checked
Cupid tools on Windows and Linux. The private hosted execution suites passed
148 methods per host, with twelve Windows and eight Linux skips. Complete
Cupid-built adapters passed seventeen execution methods and four additional
escaped-manifest publication cases per host. Captured files, objects, executable
bytes and logs were independently rechecked. These runs establish the private
adapter, not the final candidate plan's fixed point.

The integrated API suites currently pass 46 methods on both hosts, including
candidate-plan positives and altered count, source and link negatives. The
active Make-built CLI passes escaped schema/key publication. Full integrated
publication and manifest suites pass 107 methods on each host. Checked callers
pass 4,694 byte cases per host, with identical shared objects and result streams.
The isolated graph audit passes all contracts. Full integrated CLI suites pass
148 methods per host, with twelve Windows and eight Linux skips. Seed-image and
user-ELF suites pass sixteen methods per host, including Cupid-built callers.
Production coordinator regressions pass 47 methods on each host, with one
Linux skip. Independent checks bind both captures to the active 66 compiler
inputs, six test/profile files and eight imported bootstrap helpers and types.
The fixed-point audit mutation suite and both retained staged proofs pass.
All 32 Linux and 35 Windows stage-three/stage-four artifact pairs match.
Linux covers 25 C objects, startup and six tools; Windows covers 26 C objects,
three assembly objects and six tools. Independent rehashing binds all three
retained stages on each host and both reports to the same active 66-input
source snapshot. Both hosts pass final ABI validation, all sixteen artifact
checks, three user builds and private four-CPU disassembly and shell smokes.
Independent post-run inspection confirms each host's final inputs and unchanged
smoke image. Paired verification matches all sixteen artifacts, three generated
objects, three user executables, the ABI report and the 200 MiB image.

The first Linux publication attempt exposed an oracle error: Python reported
the installed plan where the Cupid author reported the candidate plan. The
publisher now uses `candidate_build_plan_sha256`. The regression distinguishes
the two plans and rejects missing or malformed candidate hashes while preserving
the previous publication. All 67 Toolchain contract methods pass on each host.
The corrected Linux acceptance agrees on all 65 stage pairs and publishes the
complete contract cohort. Failed attempts and their replacements are retained
under `build/bootstrap/native-release-6f2fe0e9/`; the final paired record is
`paired-reader-acceptance-v2-verification.json`.

No release file has been adopted as production authority, no seed pair has been
promoted, and none of the twelve Python-coordinated operations has moved. Native
read-only filesystem capture, release publication and final drift checks remain
necessary before replacing artifact verification. TempleOS remains outside the
source inventories and builds.
