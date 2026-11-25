# ADR 0235: Transfer JPEG acceptance to CupidObj

## Status

Accepted on 2026-08-04.

## Context

The checked CupidObj seed carries transactional `wrap-jpeg` support. The
normal JPEG recipe still ran the Python marker validator first and then asked
CupidObj to perform an ordinary binary wrap. That path proved cross-host byte
identity, but Python still made the production acceptance decision.

The recipe already froze the seed manifest and JPEG input, rejected unsafe
output paths, checked live-input drift, and published through a sibling
temporary directory. Those boundaries must remain in place when CupidObj
takes ownership.

## Decision

Run checked CupidObj `wrap-jpeg` first on the frozen JPEG snapshot. Preserve
the original source path as the wrapped symbol identity and write the candidate
inside the output's sibling temporary directory.

Accept a successful tool result only when the candidate is a regular,
non-symbolic file. After that check, run the Python JPEG validator against the
same frozen snapshot. Python must accept the input and reproduce the captured
bytes exactly. A disagreement rejects the candidate as a parity failure.

Recheck the live seed manifest and source bytes before publishing the checked
candidate with `os.replace`. A tool failure, missing or symbolic-link
candidate, Python disagreement, failed private oracle copy, byte drift,
live-input drift, or publication error leaves the preceding object unchanged.
Report an oracle write failure as an I/O problem rather than an acceptance
disagreement.

Keep the explicit native `--object-tool` route as a Python-first development
oracle. The normal Make rules and audit attribution remain unchanged: CupidObj
owns the object transformation, while Python snapshots inputs, checks parity
and drift, and controls publication. CupidObj still participates in 186 normal
transforms.

## Evidence

The focused JPEG asset class passes all 16 tests. The original transfer first
failed nine ownership cases against the Python-first implementation. A later
negative test exposed the misleading oracle-write diagnostic before the
implementation distinguished it from a semantic mismatch. The cases cover
exact tool, oracle, and publication order; the private source snapshot;
original identity; checked-tool failures; progressive and malformed rejection;
Python acceptance disagreement; oracle copy and byte failures; seed and source
drift; missing and non-file inputs; missing and symbolic-link candidates;
unsafe outputs; and publication rollback.

The complete hostbuild module passed 61 tests with one host-filesystem skip.
The complete CupidObj module passed 18 tests, including the active JPEG and 21
malformed-input contracts. The checked-seed JPEG carriage test also passed.

The real normal recipe rebuilt `file_example_JPG_1MB.jpg.o` through the
promoted seed. The object remains 800,860 bytes with SHA-256
`74ab86d88302c90385bb0b858632b0d6c4ac983d6be28c976dd1a3a348204b3e`.

The canonical audit regenerated in 62.634 seconds and passed its independent
drift check in 63.923 seconds. It retains 719 active inputs, 449 transforms,
255 feature requirements, and 25 accounted unreachable files. The summary and
JSON remain byte-identical because the existing graph already attributed this
transform to CupidObj and Python.

All 68 audit tests passed in 754.071 seconds. A fresh four-job root and
partitioned-USB build passed in 605.631 seconds. The final ELF is 9,069,064
bytes with SHA-256
`c1f48fe9383d4c210bb36ab6c4ab7007abf238bad6a3fc50bf0823b18918d944`.
CupidObj flattened it to an 8,862,444-byte kernel with SHA-256
`7b8abed8182c644040fb7fdb1263a4d83cad8635322350baa0c93248f2e94280`.
The 209,715,200-byte image has SHA-256
`c71fd7f5a03a4e55f4de45e6b93d4284375fb5600f4df3cda62b7f4043c33b33`.

Fresh private-image four-vCPU boots passed the complete runtime frontier with
e1000 in 545.151 seconds and RTL8139 in 536.668 seconds. The e1000 run changed
94,495 pixels and captured 21,537,672 AC97 and 76,810 PC-speaker frames. The
RTL8139 run changed 100,166 pixels and captured 21,064,744 AC97 and 79,268
PC-speaker frames. Both audio paths were non-silent, and neither log contains a
panic, fatal error, assertion failure, exception, or triple-fault marker.

## Rejected alternatives

Keeping Python first was rejected because it would leave production JPEG
acceptance under the host coordinator.

Removing the Python validator was rejected for this transfer. Its independent
implementation still gives a useful parity veto while the staged bootstrap
depends on Python for snapshots, checked-tool launch, drift checks, and atomic
publication.

Running a second ordinary CupidObj `wrap` after `wrap-jpeg` was rejected. The
source, hosted, and checked-seed contracts already prove byte identity between
the two operations. A second checked invocation would add latency and another
input-drift window without adding an independent check.

Converting progressive source during the build was rejected. The repository
keeps the accepted sequential bytes, and unsupported input must fail with a
useful diagnostic instead of selecting a host image converter.

## Consequences

Checked CupidObj now owns the normal JPEG acceptance and object bytes. Python
checks CupidObj-accepted input for parity and coordinates publication.

FFmpeg, `jpegtran`, `djpeg`, and `cjpeg` remain outside the normal root build.
Python and WSL orchestration remain open bootstrap dependencies.

No ordinary C or assembly source changes owner in this transfer, so no `.c`
to `.cc` rename is due. `TempleOS/` remains untouched reference material.
