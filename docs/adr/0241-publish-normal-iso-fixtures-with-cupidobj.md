# ADR 0241: Publish normal ISO fixtures from checked CupidObj

## Status

Accepted on 2026-08-05.

## Context

The checked five-tool seed carries CupidObj's deterministic `iso-fixture`
operation, but the normal `test_iso/hello.iso` recipe still asked Python to
author the same bytes. Source capability and seed carriage were complete; the
last open boundary was a guarded production handoff.

The handoff cannot give a freestanding tool responsibility for native host
paths. The production publisher must still reject links, junctions, special
files, aliases, input drift, and overlapping writers. It must also preserve an
existing image when the checked command fails or disagrees with the independent
renderer.

## Decision

The normal Make recipe depends on the fixture manifest, every declared member,
`tools/hostbuild.py`, the bootstrap runner, the seed manifest, and all five seed
images. Its `build-iso` command requires both manifests.

Hostbuild now performs the transaction in this order:

1. Verify the complete five-tool seed and reject outputs that alias a seed
   input or sit anywhere inside the seed directory.
2. Resolve the fixture tree, validate the exact manifest membership, and
   capture every regular file before tool execution.
3. Acquire the lock for the resolved output before reading its initial bytes.
4. Copy the frozen files into a secure temporary workspace under ordinal native
   names. Logical guest paths remain separate arguments, so host path rules do
   not change the ISO namespace.
5. Run checked CupidObj `iso-fixture` first and require a regular complete
   candidate.
6. Render the same frozen tree through the independent Python implementation
   and require byte-for-byte equality.
7. Recheck the seed, manifest, fixture tree, and live output, then reuse an
   identical image or replace it atomically.

CupidObj is the production byte author. Python remains the host-side freezer,
native-path guard, parity oracle, drift detector, lock owner, and publisher.
It is not a fallback ISO author: a checked failure or byte mismatch stops the
transaction.

## Evidence

The first production test failed against the pre-handoff API because
`build_iso` did not accept a seed manifest. The completed path reproduces the
tracked 61,440-byte image with SHA-256
`40359c1cec72219f21e87ce71b31e621209036042440e1b38c5e59de157e0fb6`.
The normal Make target runs the promoted seed and reports a byte-identical
reuse.

The host-build suite passes all 90 tests, with one expected skip when the host
filesystem cannot create case-only siblings. Its ISO coverage includes checked
execution before the Python renderer, missing and non-file candidates, command
failure, byte disagreement, seed and manifest drift, output and input aliases,
seed-directory containment, output preservation, timestamp-preserving reuse,
and competing publishers. Ruff and Python bytecode checks pass for every
changed Python module.

The graph audit classifies `test_iso/hello.iso` as
`package_iso9660_image` with `cupid_object` and `host_python` participants and
the complete checked-seed input set. A focused synthetic test locks that
classification, and the complete 68-test graph-audit module passes in 561.462
seconds. The supported-root totals remain 719 active inputs, 449 transforms,
255 feature records, and 25 accounted unreachable files.

The canonical audit regenerated in 58.012 seconds, and its stale check passed
in 58.515 seconds. The 2,558,748-byte JSON has SHA-256
`a588d3e4ffc59891d3526a6a3d57cbc895f2be1e43d902787c981823471d797c`.
The 12,196-byte summary has SHA-256
`cffff93104e890b2a7f62abf4d0003ba6a51ba10a6f4eb63e8228136b549178a`.

The source-current normal build completed in 502.232 seconds. It produced a
209,715,200-byte private image with SHA-256
`3f8c84cea61e5e8bfc4e6a5fc09a030a4d6451d258a4ca2ea6486a923d1d08e3`
and a 33,554,432-byte partitioned USB fixture with SHA-256
`057e0c86874090c99095f0558e9fa604bd7f1929f4da357da2c1baca949bb2bb`.
The image embeds the unchanged 61,440-byte ISO fixture hash above, and the
five-tool seed still verifies afterward.

A private four-vCPU e1000 frontier passed from that image in 496.479 seconds.
Its 111,548-byte serial log has SHA-256
`7a396b57e758044ceca8cbd7deb2fdff3f9b9786632794a243710f36e12c7c02`.
It contains `PASS feature17_readdir names=6 long=long_named_file.txt`,
`PASS feature17_iso`, and the following CupidC JIT completion marker, with no
panic, fatal, assertion, exception, or triple-fault marker.

## Rejected alternatives

Running the Python renderer first was rejected because it would leave CupidObj
as a validator for Python-owned production bytes. The checked command must be
the first byte author.

Keeping checked-author scratch beside the public output was rejected. Those
bytes are never renamed into place, and the placement made an unchanged-image
check depend on destination write access. A secure host temporary directory is
enough; only the final atomic candidate belongs beside the output.

Relying on drift checks without a publication lock was rejected. Two
cooperating hostbuild processes could otherwise both pass the final output
comparison and let the later replacement undo the earlier publication.

Rejecting only existing seed-file aliases was also rejected. A new `.elf`
output inside the seed directory could pass the last verification and then
invalidate the trust unit at replacement time. The final rule protects the
whole seed directory before any output parent is created.

## Consequences

CupidObj participation rises from 187 to 188 transforms. The 438-output root
graph now has 437 Cupid-owned transforms, and the Doom input manifest is its
only Python-only output. Python still participates in all 449 supported-root
transforms because it launches checked tools and retains the host-side safety
work described above.

No C or assembly source changes ownership in this handoff, so no `.c` to `.cc`
rename is due. `TempleOS/` remains untouched reference material.
