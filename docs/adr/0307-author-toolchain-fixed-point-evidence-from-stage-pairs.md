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

The Cupid-built author runs before Python performs any of the four publication
equality comparisons. The generic checked-seed bootstrap keeps its default
stage-three and stage-four comparison for every public call. A private
manifest-author path builds the same stages inside the publisher's temporary
workspace and labels its report `pending-fixed-point-author`. The public API
cannot select that path. The publisher rejects any other status and rejects a
report that already contains comparison results. Python compares all four pair
inventories only after the author accepts the request, then checks its results
against the provisional stage-four facts.

Python opens each stage file only after `lstat` confirms that the path names a
regular file. It uses a no-follow descriptor where the host provides one,
checks the descriptor identity before and after reading, and checks the path
identity again after capture. Python also keeps safe request creation, process
launch, live drift checks, private staging, rollback, and atomic publication.
The author still runs as a static Linux ELF built by converged stage-four
CupidC, CupidASM, and CupidLD, so Windows uses WSL for this step.

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

A review then found that the publisher's mocked order test could not see work
inside `bootstrap_from_seed`. That function compared the bootstrap stages
before the author ran. The first repair added a public comparison switch. A
standards review rejected that draft because a caller could publish a report
marked `pass` without convergence. The public API now always finalizes the
fixed point, and a negative test proves that it rejects the old switch. Only
the private author path may produce the pending report.

A second test passed a symlink into the pair capture and failed because
`Path.read_bytes` followed it. Further tests replace the file before open,
alter the descriptor identity during capture, and replace the path after close.
Each failure is followed by a valid capture in the same process. The build
audit checks the real Python syntax tree, including the public finalizing path,
the private pending path, the author call, four later oracle calls, the
fixed-point summary assignment, and descriptor-pinned file capture.

The publisher suite passes 62 tests in 7.266 seconds. The direct author suite
passes 40 tests in 40.828 seconds, including a checked stage-four build and run
of the Cupid-built author. The pinned verifier runner passes 25 tests in 32.773
seconds, with three expected POSIX-only skips on Windows.

A complete `CUPMAN4` publication passed before that review in 4,437.131
seconds. It wrote a 27,069-byte schema v3 manifest with SHA-256
`47a1e271acd22089a51c2cb23695abd466e6628a5f0c32d44cb67fc886563d9c`.
That result proves the raw-pair protocol but predates the corrected publisher
order and descriptor capture.

The first source-current rerun passed the author, all four Python comparisons,
and atomic publication, then failed after 3,976.96 seconds when its final
read-only verifier imported an unrelated installed package named `tools`.
ADR 0311 pins both checked contract launchers to this checkout. The next
unmodified-environment rerun passed in 3,989.13 seconds, including the final
`CUPMAN2` verifier. Its 27,071-byte schema-v3 manifest has SHA-256
`615cdfd4095d684f31684b9887ba9610c033513580e7332d2d153841947c9311`.

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
evidence before Python makes the same publication decisions independently. The
producer-trusted object-comparison gap from ADR 0304 is closed without changing
schema v3 or publication ownership.

The request is larger and is read twice by the current contract path, so
memory use and WSL execution remain practical limits. Python remains required
for filesystem safety, orchestration, the independent oracle, and atomic
publication. A Python-free fixed-point driver and native author execution on
Windows remain open. `TempleOS/` stays outside the build graph.
