# ADR 0304: Author Toolchain publication manifests with CupidC

## Status

Accepted on 2026-08-20.

## Context

ADR 0302 moved Toolchain manifest verification into a strict C11 contract,
but Python still assembled the manifest bytes. The publication transform was
the last Python-only transform in the supported graph. Verification could
prove that a finished manifest matched captured observations without proving
that Cupid code could author the same document from independent raw facts.

The author must cover 21 artifacts, 70 publication inputs, 50 bootstrap source
inputs, and 17 object comparisons. It also has to preserve the existing
filesystem protections against links, replacement, membership drift, seed
drift, and partial publication.

## Decision

Add an `author` mode to `toolchain_manifest_contract.cc`. Its length-prefixed
`CUPMAN3` request contains observations rather than a draft manifest. The
lanes carry artifact facts, publication-input facts, bootstrap facts and their
snapshot digest, the Linux seed path and raw manifest, five seed image
observations, seventeen object observations, and the fixed-point generation
facts. Schema `cupid.toolchain-contracts.v3` records each of the 70 publication
inputs and 17 object comparisons as an object with exact `sha256` and `size`
fields. `CUPMAN2` rejects live input observation size drift, and Python checks
the report with an independent oracle. The `CUPMAN3` author requires each
object-comparison size to be nonzero and binds its record into canonical
output. Those records remain producer evidence because the author does not
independently read the object bytes. The contract also fixes the exact names,
counts, kinds, generation pair, and build plan. It rejects a missing, extra,
duplicate, unsafe, malformed,
truncated, or trailing fact before emitting canonical JSON.

The author is strict C11. It is always built and run as a static Linux ELF by
the converged stage-four Linux CupidC, CupidASM, and CupidLD tools. Windows
runs that ELF through WSL. The separate `CUPMAN2` verifier is the only
Toolchain manifest contract selected for the host, so Windows builds and runs
it as a native PE. The author includes the artifact-size policy contract under
a renamed entry point so the two contracts share the checked JSON, SHA-256,
observation, and file helpers instead of keeping a second parser.

Python keeps the native filesystem and process boundary. It pins the complete
cohort, builds and runs the private author, rechecks the live seed and inputs,
renders an independent Python oracle from the same captured facts, and
requires byte-for-byte agreement. Only the authored bytes enter private
staging. Python verifies the complete staged cohort before one atomic directory
swap and restores the prior publication on failure.

Make declares both contract sources as publication inputs. The publication
inventory therefore contains 70 files. The Make and audit closures name the
same input and seed cohorts.

## Evidence

The standalone contract module passes all 29 tests in 39.068 seconds. It
includes a real build and run with the promoted Linux seed and compares the
authored bytes with an independent oracle. Separate negative tables cover
wrong, missing, and extra artifact facts; bootstrap count, path, size, and
digest changes; seed path, raw bytes, artifact size, and digest changes;
missing, duplicate, and zero-size object comparisons; and wrong generation or
count fields.

The publisher suite passes all 59 tests in 3.518 seconds. The pinned verifier
runner suite runs 24 tests in 27.752 seconds, with three POSIX-only cases
skipped on Windows. The focused fail-closed graph-drift case passes in 236.549
seconds.

The source graph retains 739 active language inputs, 452 transforms, 255
feature requirements, and 25 accounted unreachable files. Its ownership model
is CupidC 250, CupidObj 192, CupidASM 9, CupidLD 9, CupidDis 6, and four
Cupid-built semantic contracts. Python still participates in all 452
transforms for coordination and safety, but no transform is Python-only. Root
`all` remains 443 transforms, all with a Cupid participant.

The schema v3 `make -C toolchain all` passed in 4,273.533
seconds. Every stage-three object and executable matched its stage-four
counterpart. The hosted runtime passed, and the live inputs stayed frozen
through publication. The publisher wrote all 21 artifacts. Its
27,069-byte manifest has SHA-256
`69c5b8e62c1e61a8f1a2823d18edff794ae03239be71c881ddd8a190f1377c91`.
It records 70 publication inputs, 50 bootstrap files, 17 object comparisons,
and Linux seed manifest
`51c8244aa51fce8ccaf7f2eb24df848f02d9269109599cdbdfb0f1f699b5ee65`.
The native Windows `CUPMAN2` verifier printed
`Cupid Toolchain manifest: ok (21 artifacts)`. The final post-CTXT
`make bootstrap-audit` passed in 71.299 seconds, and
`make check-bootstrap-audit` passed in 72.051 seconds. It records 739 active
sources, 452 transforms, 255
feature requirements, and 25 accounted unreachable files. The active-source
digest is
`6ebbbbf7e10e349ba703fc335e87ba5ba40f241d477155f879f2b86b879efd22`.
The 2,700,372-byte JSON has SHA-256
`98adc224910ec61661878fde98ddb335073a0c8e95779b4765c34ebf39499bce`,
and the 12,502-byte Markdown summary has SHA-256
`094200553d690746387801ffd42ed970b1c0ba13a2ac24ad14ed9ed4ea73db70`.
The 684.260-second poisoned-host OS build and 64.601-second private smoke remain
preceding checkpoint history. The first post-documentation fully poisoned
build reached only the expected size mismatches for the final ELF and raw
kernel after 680.281 seconds. The artifact group then ran 45 tests in 2.582
seconds with four expected Windows skips. The definitive fully poisoned build
passed in 708.912 seconds. It checked all fourteen artifacts, preserved the FAT
contents, and staged `test_iso/hello.iso`. Its 209,715,200-byte image has
SHA-256
`8a7a67e3da4dd8e256bbe1f69d511b59dc9f669cb6026acbeca055c998889195`.

The strong full private frontier used e1000, four `max` vCPUs, SMP, a private
image, and the USB fixture. It passed in 801.490 seconds. The framebuffer
changed 96,925 pixels at 640x480. AC97 produced 32,722,102 stereo 44,100 Hz
frames with a peak of 25,600, and the PC speaker produced 73,533 stereo 44,100
Hz frames with a peak of 8,415. The direct-call,
named-callback, typedef-callback, overall feature-14, and JIT markers each
appeared once and in order. The 150,376-byte log has SHA-256
`73f77abc06357bf5d7185b40825d9d197e9954014ccf09362e9a1d219cc30f02`
and no rejection markers. The source image stayed unchanged at SHA-256
`8a7a67e3da4dd8e256bbe1f69d511b59dc9f669cb6026acbeca055c998889195`.
Publication, audit, the OS build, and the guest frontier are complete.

## Rejected alternatives

Do not give the author a draft manifest and ask it to normalize or approve the
same claims. The request must carry independently captured facts so the output
is not a tautological copy.

Do not remove the Python oracle when adding the Cupid author. Agreement between
independent implementations is part of the publication boundary.

Do not let the contract walk the live repository. The hosted runtime does not
replace the pinned, no-follow filesystem code used on Windows and POSIX.

Do not publish the author output before the complete cohort verifies. A
successful process exit is not an atomic publication transaction.

## Consequences

Toolchain manifest production now has CupidC, CupidASM, CupidLD, a Cupid-built
semantic contract, and Python participants. The author always uses the static
Linux stage-four tools and ELF execution path. Windows therefore needs WSL for
authoring. Only `CUPMAN2` verification follows the host and runs as native PE
on Windows. The supported graph has no Python-only transform.

Python still owns filesystem capture, directory membership, process launch,
the independent oracle, private staging, and atomic publication. Windows still
uses WSL for the full Linux contract cohort. Inventory or schema changes must
update the fixed C contract. This is a Toolchain manifest author, not a general
JSON author. `TempleOS/` remains outside the graph.
