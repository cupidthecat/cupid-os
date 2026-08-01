# ADR 0201: Generate installation source with CupidObj

## Status

Accepted on 2026-08-01.

## Context

The normal build installs CupidC programs, browser fragments, headers,
manuals, image assets, and Cupid ASM demos through three generated `.cc`
tables. CupidObj already wraps every listed source or asset for the kernel,
and CupidC compiles each generated table. Python still wrote the table source.
That left a small but real code-producing host boundary between two Cupid
owned steps.

The three tables have different shapes. The bin table carries programs,
headers, and browser fragments. The docs table carries manuals, one docs
asset group, and four home asset formats. The demos table installs each
source in two directories. A replacement must preserve the current bytes,
including list order, symbol spelling, installed paths, and final newlines.

## Decision

CupidObj exposes `CTOOL_OBJ_GENERATE_INSTALL_SOURCE` through its public
request API. The request selects `bin`, `docs`, or `demos` and supplies typed
path lists for that table. The hosted command spells the same boundary as
`cupidobj install-source MODE` with the matching list options.

Paths use repository-relative forward slashes and the exact extension for
their category. Symbol stems accept letters, digits, and underscores. Manual
and asset stems also accept hyphens, which become underscores only in C
symbols. Installed filenames keep the original hyphen. Duplicate paths,
mixed request categories, an empty inventory, malformed paths, and more than
512 total entries produce a specific diagnostic.

CupidObj emits entries in caller order. Make already sorts each discovered
source list, so preserving that order keeps Windows and Linux output equal.
The operation does not inspect file contents. Make prerequisites remain
responsible for the complete source inventory, while the first listed path
anchors the hosted invocation and its diagnostics.

The shared output buffer remains the transaction boundary. Any validation or
capacity failure rewinds bytes written by the operation and zeros its result.
The hosted invocation commits the destination only after the operation
succeeds without an error diagnostic.

Python remains available as a byte-for-byte oracle during the ownership
transfer. The normal Make recipes do not change in this capability step. The
checked seed must first carry the new command before those recipes can depend
on it.

## Evidence

The first demos CLI test returned the old usage text. The bin and docs tests
then failed for the same reason until each public mode existed. The public
contract later exposed an accepted mixed-category request and an unbounded
inventory before those checks were added.

The native CLI now generates the full live inventory twice and matches the
Python oracle exactly:

| Table | Bytes | SHA-256 |
| --- | ---: | --- |
| bin | 46,335 | `3af136af46726ae1a594169d12da2dbe1035f17d992fd5f08b2139e4787ab85a` |
| docs | 9,794 | `cff3fc8943d4b1999869653b14a882d21a463471452e429b2d742d47107b13fc` |
| demos | 12,845 | `0d1f7ee032b13abbbe1767d75fe32c6f1ffa8b7014db44ae35c9d4c47ebb8305` |

### Correction recorded with ADR 0204

The bin hash above came from culture-sensitive PowerShell ordering. GNU Make
uses ordinal ordering for the production inventory. The pre-transfer file,
checked CupidObj command, and Python oracle agree on 46,335 bytes with
SHA-256
`c79edeeaf909d6c204690acd31dd56ca91be4f65ed148fa8e5e9768ac8dc1d8f`.
The docs and demos hashes above remain correct.

### Contract correction after production review

The first implementation checked the 512-path total inside the demos emitter.
Bin and docs reached their validation loops without that guard, and summing
large counts there could wrap. The shared operation now checks every category
before mode dispatch. It rejects any single count above 512 and accumulates
the combined total only after proving the subtraction safe.

The docs emitter also grouped home assets by extension. That happened to match
the active Make inventory, but it did not honor the caller-order decision
above for an interleaved request. CupidObj and the Python oracle now emit the
home externs and installation entries in the supplied order. The active files
remain byte-identical because their input is already grouped.

The core contract rejects 513 bin paths, 513 docs paths, a 256 plus 257 bin
and header request, and an extreme unsigned count. A hosted interleaved docs
case checks PNG, JPEG, BMP, JPG, and BMP order while retaining oracle parity.
All seven native contract modes pass, and all eleven hosted CupidObj tests
pass. ADR 0205 promotes the corrected 245,220-byte CupidObj image with SHA-256
`e9631e8b9377a17497bc87418c56282d97f91b8d1cd43e4670130e5e54334747`.

### Symbol-domain correction after final review

A later review found that raw-path uniqueness was not enough. The wrapping
rules replace a hyphen with an underscore, and the bin and browser prefixes
can overlap. Distinct valid paths could therefore declare the same linked
symbol even though neither path was duplicated.

CupidObj now compares the complete emitted symbol names across every typed
list before it writes output. It rejects a bin and browser prefix collision,
hyphen-to-underscore collisions within a manual or asset list, and a collision
between documentation and home assets. The one deliberate exception is an
exact BMP path listed once for documentation and once for home. Those entries
refer to the same wrapped object and remain valid. The Python oracle applies
the same rule before it opens its destination.

The core contract covers the three collision shapes, the exact BMP alias, and
the inclusive 512-path success boundary before the existing 513-path failure.
The hosted suite checks the native command and Python oracle against all three
collision shapes and verifies that each sentinel output survives. All seven
native contract modes, all twelve hosted CupidObj tests, and all 39 production
tests pass. The active installation tables and their objects remain
byte-identical. The source head carries this correction; the checked CupidObj
seed still needs promotion before the normal recipes enforce it.

The public contract pins a complete two-demo source as a literal. It also
checks repeated output, a wrong extension, a mixed category, the 512-path
limit, output-limit rollback, a zeroed failure result, and recovery in the
same job. The CLI contract keeps an existing destination after a wrong path
or duplicate and then succeeds with a valid request.

Stage-three CupidC compiled that contract into a 49,812-byte object with
SHA-256
`f6178558abe258c97542ed7536f847efd5086e0c3933fe4bf905244ef8c9bafc`.
CupidLD linked a 261,652-byte static executable with SHA-256
`bf4b50bf6731e6bc84313d1a3f34b374369a125a30a427ad9fb0548d302154dc`,
and its `install-source` selector passed under WSL.

The checked seed compiled all 19 fixed-point sources into stage two, and
stage-two CupidC repeated them for stage three. All 19 objects, the startup
object, and all five tool images matched. Both stages passed five help cases,
ten successes, and six failures. The private source snapshot contains 41
files and has SHA-256
`7d5589ba377fcebf6295cd6c58157b098e5fc5f0c5131574e3ba31bc5927c502`.
The resulting CupidObj image is 245,132 bytes with SHA-256
`d39fe725cec9c3c968d9abe33281d34dd9a192f5e3d5f77bb6a9dbc13e935b43`.

That stage-three CupidObj generated all three live tables twice through WSL.
Every output matched the native Python oracle and the hashes above.

## Rejected alternatives

Keeping Python as the permanent generator was rejected because these tables
are active C source and CupidObj already owns the source and asset packaging
domain.

Adding a sixth bootstrap command was rejected because it would enlarge the
fixed point for a responsibility that fits CupidObj's existing object and
packaging boundary.

Checking in generated tables was rejected because source discovery is part
of the build contract. A checked table could become stale when a program,
manual, asset, or demo changes membership.

Building source text in the Make recipe was rejected because shell quoting
and line handling would create a larger cross-host behavior surface.

## Consequences

CupidObj can now produce every installation table without a host language
generator. The capability is self-hosted and byte-compatible with the current
Python output. Errors preserve the previous destination, and callers can
retry in the same job.

This step does not move the normal Make recipes or retire Python. A following
seed promotion and ownership transfer can switch the recipes while retaining
Python as an optional parity oracle.
