# ADR 0307: Author Toolchain fixed-point evidence from stage pairs

## Status

Accepted on 2026-08-21.

## Context

ADR 0304 introduced the `CUPMAN3` Toolchain manifest author. That protocol
gave the Cupid-built author digest and size claims for seventeen contract
object comparisons. Python had already made those comparisons, so the author
could bind the claims into schema v3 but could not check the underlying stage
bytes. Python was also the only component that compared the sixteen contract
executables, nineteen bootstrap C objects, startup object, and five tool
images.

Filesystem traversal and publication still need the host boundary. The
semantic equality decisions do not. Moving those decisions into the checked C
contract gives the Cupid-built author raw evidence while preserving Python as
an independent implementation and transaction coordinator.

## Decision

Replace the author request with `CUPMAN4`. The request carries four exact
inventories of paired stage-three and stage-four byte streams:

- seventeen contract objects;
- sixteen contract executables;
- nineteen bootstrap C objects and one startup object; and
- five tool images.

Each side of a pair has a file-kind fact and a length-prefixed byte stream. The
author requires a regular, nonempty file on both sides, equal lengths, and
byte-for-byte equality. It hashes both streams independently and requires the
two SHA-256 results to agree. Names and counts are fixed by the contract, so a
missing, extra, duplicate, or unexpected pair is rejected.

The seventeen published object-comparison records are derived from the raw
stage-four streams after equality succeeds. Contract executable pairs are
also checked against the corresponding artifact digest and size facts. The
bootstrap pairs determine the fixed-point summary from the contract's exact
inventories. No caller `all_equal`, generation, or count assertion follows the
pair lanes. The output remains canonical `cupid.toolchain-contracts.v3`; this
protocol change does not add a manifest field or alter the published inventory.

The Cupid-built author runs before Python performs any stage comparison.
Python then compares all four pair inventories independently and checks that
its results agree with the provisional stage-four facts. Python keeps pinned
no-follow capture, safe request creation, process launch, live drift checks,
private staging, rollback, and atomic publication. The author still runs as a
static Linux ELF built by converged stage-four CupidC, CupidASM, and CupidLD,
so Windows uses WSL for this step.

The author timeout is 360 seconds. The current request is about 67 MiB because
it contains both generations of every paired file. The contract reads that
framed request into bounded memory and rejects truncation or trailing bytes.
Each individual framed byte stream uses a 32-bit length and therefore remains
limited to less than 4 GiB.

## Evidence

The first object-pair test failed against `CUPMAN3` with
`request magic differs from CUPMAN3`. After the minimal raw-pair change, the
matching object case passed. Mismatch, targeted truncation, wrong kind,
duplicate name, recovery, and size mismatch cases were then added one at a
time and passed.

The first 58-pair test failed with `request byte string is truncated` because
the author stopped after the seventeen object pairs. Adding the executable,
bootstrap-object, and tool-image lanes made that test pass. Separate negative
cases now alter each of those three lanes. Another case proves that matching
executable bytes cannot disagree with the artifact digest or size fact.

The first request that ended after the 58 pairs failed with
`request is truncated while reading a 32-bit value` because the author still
expected Python's fixed-point assertion block. Removing that block made the
request pass, while appending the old `all_equal` word now fails as trailing
input. Changing every caller-side summary value does not change the constants
derived by the author.

The direct manifest contract suite passes 40 tests in 43.019 seconds. It
includes a checked stage-four build and run of the Cupid-built author. The
publisher suite passes 60 tests in 7.144 seconds, including the required order
of author decision followed by four independent Python comparisons and prior
publication recovery after author failure. The pinned verifier runner passes
24 tests in 28.302 seconds, with three POSIX-only cases skipped on Windows.

The complete `make -C toolchain all` publication was not rerun for this source
checkpoint. The latest complete publication therefore remains the preceding
`CUPMAN3` result: a 27,069-byte schema v3 manifest with SHA-256
`69c5b8e62c1e61a8f1a2823d18edff794ae03239be71c881ddd8a190f1377c91`.

## Rejected alternatives

Keeping Python as the only stage-equality authority would leave the manifest
author dependent on producer claims. Passing only two digests would move the
claim format without giving the Cupid-built contract the bytes it is meant to
judge. Adding new schema fields would expose an internal evidence change in a
stable publication format. Letting the C contract walk the repository would
duplicate mature platform-specific filesystem safety and weaken the existing
transaction boundary.

## Consequences

The Cupid-built author now makes all 58 fixed-point equality decisions from raw
evidence before Python makes the same decisions independently. The
producer-trusted object-comparison gap from ADR 0304 is closed without changing
schema v3 or publication ownership.

The request is larger and is read twice by the current contract path, so
memory use and WSL execution remain practical limits. Python remains required
for filesystem safety, orchestration, the independent oracle, and atomic
publication. A Python-free fixed-point driver and native author execution on
Windows remain open. `TempleOS/` stays outside the build graph.
