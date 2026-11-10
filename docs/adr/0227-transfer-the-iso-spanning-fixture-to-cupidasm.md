# ADR 0227: Transfer the ISO spanning fixture to CupidASM

## Status

Accepted on 2026-08-03.

## Context

`test_iso/fixtures/big.bin` is a 4,096-byte byte lane used by Feature 17 to
exercise reads across ISO sectors. The normal Make recipe asked Python to
author those bytes directly. ADR 0191 made that transform explicit, but it
left one small binary producer outside Cupid tooling.

CupidASM already supports raw output, `TIMES`, data directives, the current
address expression, and truncation to a byte. It can express the fixture as
`times 4096 db $` with `ORG 0`. The emitted bytes repeat `00` through `ff`
sixteen times.

## Decision

Track the source as `test_iso/big_pattern.asm`. The normal Make recipe freezes
that source and the complete checked-seed trust unit, then asks checked
CupidASM for a private raw candidate. Python independently constructs the
expected 4,096 bytes, checks exact parity, rechecks the live source, manifest,
and destination, and publishes with `os.replace`. A failed tool, missing or
nonregular candidate, byte mismatch, live-input change, or destination change
leaves the previous output in place. Equal bytes keep the existing file and
timestamp.

Classify the transform as `assemble_flat_binary` with `cupid_assembler` and
`host_python` as participants. Keep Python as the transaction coordinator and
parity oracle rather than claiming a Python-free build.

NASM evaluates `$` once for the whole `TIMES` statement, while CupidASM
reevaluates it for each emission. The optional NASM equality check is disabled
only for this migrated fixture. A contract locks that single exception, and
all other production assembly keeps its NASM oracle when NASM is available.

## Evidence

The hostbuild ISO suite passes 23 tests with one platform skip. Positive
coverage checks the frozen source handed to checked CupidASM, the exact
candidate, and timestamp-preserving reuse. Negative coverage checks a failed
assembler, parity drift, live source drift, live destination drift, and
preservation of the published fixture.

The active CupidASM source suite passes all three tests. It reproduces the
4,096-byte artifact with SHA-256
`c8f5d0341d54d951a71b136e6e2afcb14d11ed8489a7ae126a8fee0df6ecf193`.
A real forced Make run uses the checked seed and reuses the identical tracked
fixture. The regenerated graph has 719 active inputs, including 28 assembly
sources, and records five CupidASM transforms. The total remains 449
transforms because ownership changed without adding an output.

The complete normal image build passed through this recipe in 959.77 seconds.
A private four-vCPU e1000 frontier then passed the ISO read checks, Browser
self-test, USB lifetimes, framebuffer change, and both audio captures in
353.79 seconds.

## Rejected alternatives

Keeping Python as the byte author was rejected because the existing assembler
already expresses the complete artifact.

An explicit 256-byte list repeated sixteen times was tested and rejected. It
preserved optional NASM equality, but it made the maintained source harder to
inspect solely to match an oracle that does not own the build.

Removing Python from this recipe was deferred. Input freezing, independent
parity, checked-seed execution, drift detection, and atomic publication still
belong to the current host coordinator.

## Consequences

Checked CupidASM now authors the normal ISO spanning fixture. Python still
participates in every supported transform, and the repository ISO image writer
also remains Python-owned. Native Windows execution and a Python-free build
stay open. No `.c` file entered the supported Cupid graph, so no `.cc` rename
was due. `TempleOS/` remains untouched reference material.
