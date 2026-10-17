# Retained external disk stages

The private source under `C:/Users/admin/cp7/external-stage-source5` extends
optional stage requests with an external observer. A null selection retains
repository staging. External requests require the transaction's exact primary
observer and an already borrowed external observer, including when the source
is absent. `cupidbuild_host_transaction_require_external_observer` checks that
authority and revalidates both lifetimes before optional discovery. A failed,
changed or late requirement forbids publication.

Capture reuse uses both the selected observer and logical path. Equal spellings
under the repository and external roots, or two external roots, retain separate
payloads. Repeated requests under one observer reuse a capture. Every registered
request still checks complete size and digest against the independently observed
source. Present files use the bounded external freeze API. Missing files retain
the first absent component under their selected observer. All borrowed observers
remain alive through transaction close, and external roots keep full metadata
checks. Frozen spellings and destinations remain owned across input-table growth
and changes to caller storage.

## Evidence, 2026-10-08

The new external module has 34 methods; the existing repository module has 21.
Both modules pass through native and Cupid-built callers on Windows and Linux:
220 selections, 212 executions and eight declared POSIX skips. Native runs take
76.476 seconds on Windows and 48.712 on Linux. Actual Cupid runs take 375.836
and 290.328. Small commands keep twenty seconds; the 65 MiB control keeps 180,
and the complete 200 MiB control keeps 600. Every Linux external command runs
under a 32 MiB address-space limit. Both complete large payloads pass.

Cases cover separate root authority, repeated capture reuse, missing and empty
files, registered inputs, owned views, UTF-8 paths, exact primary binding,
unborrowed equal-root observers, poison, late requests, destination validation
before discovery, directories, unsafe paths, links, FIFOs and the FAT16 extent
limit. Publication checks reject restored-time payload edits, missing-file or
parent appearance, and external root metadata drift. Negative publication cases
preserve complete prior bytes and timestamps. The fixture publishes a small
replacement output; this scope does not produce a staged FAT image.

Both caller builds use six qualified `a1cc8f3a` parents, two workers and the
original 360/120/180-second compile, assembly and link bounds. They retain 119
source controls and 106 private producer paths. Windows takes 73.420 seconds and
Linux 72.880. Strict PE import checking includes the separately declared NTDLL
procedure needed by the external adapter. All four caller images and actual
i386 objects are retained beneath `external-stage-checked5` in each source root.

`external-stage-quad-independent2/closed.json` under `C:/Users/admin/cp7` checks
all captured source bytes, objects, callers, parent tools, execution profiles,
commands and original bounds. The new module retains before/after file digests,
prior output times, exact namespaces, complete stdout/stderr and native source
and program facts. The repository module keeps its original preservation
assertions and invocation records; its initial timestamps are not separately
archived. Oversized four-GiB rejection retains metadata rather than reading the
payload. It does not prove acceptance of a four-GiB file.

## Fixture repairs

The first fixture omitted the required leading slash from guest destinations.
Its complete matrix failed before useful source capture, while repository
regressions passed. The corrected fixture then requested 1,024 input slots,
above the existing 528-slot bound, and used a guest name whose retained short
alias contained a non-ASCII character. A direct projection probe confirmed that
the latter rejection follows the existing captured Unicode/FAT policy.

The final fixture tests growth to 528 slots and rejection at 529. Its positive
Unicode destination follows the existing ASCII short-name projection; a separate
negative method retains the non-ASCII alias rejection. Production allocation,
FAT projection and all process bounds remain unchanged. Both failed source copies
and their products remain available. The first checker assumed one timeout field
for the existing repository mutation records; the corrected checker checks their
separate twenty-second handshake and runtime bounds.

## Remaining handoff

The source is separate from the integrated 99-input hosted-library cohort and
the older optional publisher. The next [publisher handoff](EXTERNAL-DISK-PUBLISH.md)
passes both native 44-method selections with complete eight-MiB images; Cupid
execution and independent image/FAT review remain open. Normal external WAD
discovery and command parsing, complete large-geometry
templates, integration, paired qualification and recipe adoption remain open.
The earlier Windows publisher cleanup failure keeps its own unresolved evidence.
The normal disk recipe and two SDK coordinators still belong to Python.
TempleOS remains outside implementation and progress counts.
