# ADR 0352: Define non-self-referential six-tool seed manifests

## Status

Accepted on 2026-08-26.

## Context

The checked Linux and Windows seeds still use their v1 schemas and contain five
tools. Source head can build a sixth tool, CupidBuild, and the fixed-point
drivers can compare six-tool candidates, but the retained CupidBuild images
understand only the v1 manifests. Promoting those candidates would create a
checked seed whose CupidBuild could not validate the manifest that selected
it.

A six-tool manifest also cannot require CupidBuild to contain the exact source
revision or source snapshot recorded in that same manifest. Changing either
value changes CupidBuild's source, which changes the candidate image and the
snapshot again. The compatibility boundary must fix the trusted parents and
build plans without embedding its own final identity.

## Decision

Add source support for `cupid.bootstrap-seed.v2` and
`cupid.execution-seed.v2` while retaining the existing v1 contracts. This
change does not alter either checked manifest or any checked seed image.

A v2 manifest contains exactly six artifacts: CupidASM, CupidC, CupidDis,
CupidLD, CupidObj, and CupidBuild. CupidBuild is not a producer. CupidC,
CupidASM, and CupidLD remain the producer set.

The Linux v2 provenance names the current v1 Linux manifest,
`b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`,
and its source revision,
`a17c9465911da41d59b7ada71733d36c39faa5ea`. Its six-tool build plan has
SHA-256
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`.

The Windows v2 provenance names both v1 parents. The execution parent is the
Windows manifest
`751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef`,
and the plan parent is the Linux manifest
`b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`.
Both parent revision fields must equal
`a17c9465911da41d59b7ada71733d36c39faa5ea`. The Windows record also fixes
the Linux candidate-plan digest above and the native Windows plan digest
`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`.

Both v2 schemas require 58 source inputs. Their candidate `source_revision`
and `source_snapshot_sha256` fields must be lowercase hexadecimal strings of
40 and 64 characters respectively. The verifier checks their shape rather
than comparing them with constants embedded in CupidBuild. The manifest
produced by a later promotion will bind the actual values observed for that
candidate.

Manifest strings use their literal UTF-8 spelling. The host reader rejects
JSON string escapes, matching CupidBuild's token checks. This prevents the two
validators from assigning different meanings to the same manifest bytes.

The native Windows driver accepts either the two v1 seeds or the two v2 seeds.
It rejects a mixed generation and requires the execution and plan manifests to
carry the same source revision and source snapshot. A Windows v2 manifest also
records the lower-case SHA-256 of the Linux v2 plan manifest. CupidBuild checks
the field's shape without embedding the future digest, and the host driver
compares it with the supplied plan manifest bytes. Individually valid seeds
from different candidate cohorts cannot be paired.

Promotion must start from a named commit that contains this compatibility
code. The v1 seeds build a fresh six-tool candidate from that commit, and the
following stages must converge before their artifacts can enter v2 manifests.
Retained candidates built before this decision are evidence for the earlier
fixed point, but they are not promotion inputs because their CupidBuild images
accept only v1.

## Alternatives considered

Promoting the retained six-tool candidates directly was rejected. Their
CupidBuild images cannot consume a v2 seed, so the resulting cohort would fail
its own checked execution path.

Replacing the v1 schemas or changing the checked manifests in this step was
also rejected. The existing five-tool manifests are immutable reviewed inputs,
and this source-only change has not produced a new candidate from its own
compatibility commit.

Embedding the future compatibility commit and source-snapshot digest as exact
CupidBuild constants was rejected because those constants participate in the
snapshot they would describe. Structural lowercase hexadecimal validation
breaks that self-reference while the parent identities and plan digests remain
exact.

## Intended verification

The focused bootstrap tests exercise unchanged v1 verification and six-tool
v2 verification on Linux and Windows. They cover the exact artifact inventory,
the non-producer CupidBuild flag, parent manifest and revision drift, both plan
digests, lowercase source identities, escaped strings, exact plan-manifest
pairing, six-tool checked execution, and the Windows execution profiles for
CupidASM and CupidBuild.

CupidBuild's hosted tests exercise both schema generations through the guarded
object transaction. A bad parent, malformed source identity, missing or extra
artifact, changed producer flag, wrong plan, or execution-profile mismatch
must fail before publication and preserve the previous output.

Before promotion, a fresh Linux and native Windows fixed point must be built
from the named compatibility commit. Stages three and four must match across
all objects and six tools, and their behavior and strict CupidDis checks must
pass. The resulting v2 manifests must then reproduce the promoted cohort from
their own checked inputs.

## Consequences

Source head can validate a future six-tool checked seed without weakening the
current five-tool trust roots. The exact v1 parents and build plans define the
transition, while the future candidate manifest records its own observed
source identity.

This decision does not promote a seed, transfer a normal Make recipe, change
the artifact-size policy, or remove Python coordination. No active source
ownership changes, so there is no `.c` file to rename. `TempleOS/` remains
read-only reference material.
