# ADR 0402: Capture reviewed seed release identities

Date: 2026-09-23

Status: Accepted

## Context

ADR 0400 supplies the release byte reader. A native artifact verifier still needs
an independently reviewed record against which to check both installed cohorts.
Embedding CupidBuild's finished digest in its own source would make that check
self-referential. Moving identities out of Python must preserve the current
release checks and semantic JSON acceptance.

## Decision

Track `bootstrap/seeds/release.json` outside both six-tool seed directories.
It uses `cupid.seed-release.v1` and records source, parent, plan and twelve
artifact identities. Repository review supplies its authority; parsing it does
not authenticate an untrusted release. The existing independent Python pins
remain mandatory during migration.

`tools.seed_release_identity` checks bounded UTF-8 bytes against those pins.
It rejects duplicate decoded keys, missing or extra fields, noninteger sizes,
unknown or repeated roles, and changed identities. Key order, artifact order
and equivalent JSON escapes do not change meaning. The C reader and Python
checker both accept the tracked record. Windows remains bound to the actual
selected Linux manifest bytes, rather than a canonicalized representation.

The authoring command validates both installed promoted cohorts, their pairing
and their live bytes before creating a new review candidate. Exclusive creation
preserves an existing destination. It never derives trusted identities from
manifest claims, replaces the reviewed record, or promotes seeds.

The production Python artifact runner captures the record and verifies it
before launching its Cupid-built contract. Its 26 captured build inputs also
include the author/checker module and Python package marker. Existing final
payload, path, directory and root checks cover these inputs. Make and the graph
audit require the same closure. No executable seeds or production owners change.

## Evidence and remaining work

The release, shared C reader, artifact runner and policy suites pass 104 methods
on both hosts, with four Windows skips. Rejections cover missing and linked
records, malformed and duplicate fields, every changed release identity,
same-size edits with restored timestamps, stale pair bindings, historical seed
schemas, changed tools with matching manifest digests and existing candidates.
Real checked Cupid contracts pass on both hosts against all sixteen preceding
accepted OS artifacts; copied inputs and original artifacts remain unchanged.

The first isolated Linux replay omitted the Python package marker and imported
an unrelated installed package. The marker is now captured explicitly. The
first Windows replay passed its contract but failed the harness's LF-only
output assertion; the harness now checks native line endings. Both failures
remain in the private evidence.

All ten graph contracts and deterministic audit checks pass. Final Windows and
Linux image builds, user builds and four-CPU max/e1000 SMP/disassembly/shell
smokes pass. Independent verification rehashes 1,499 inputs, sixteen artifacts
and 431 link inputs per host. Both 200 MiB images match and remain unchanged
through the smokes; all three user executables match the preceding acceptance.
The embedded manual grows by 415 bytes; only its object and the two kernel ELF
link inputs change. The flat kernel measures 9,558,372 bytes, and its exact-size
policy is updated to that measurement. Final evidence is recorded in the
bootstrap log. Native command integration and eventual seed promotion remain
separate work. The twelve Python-coordinated operations remain in place.
