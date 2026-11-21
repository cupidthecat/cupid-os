# Check complete captured ISO images independently

Date: 2026-10-05
Status: Implemented as a source capability; production integration pending

## Context

CupidObj already authors the complete deterministic ISO fixture. Python still
captures its inputs, compares the authored image with a separate implementation,
rechecks live state and publishes the result. ADRs 0191, 0239 and 0241 retain that
format and transaction. ADRs 0420 and 0421 supply native inventory validation and
retained kind discovery. Transferring publication also needs an independent
complete image check.

## Decision

Add `cupidbuild_iso_image_validate` in a separate module. It consumes the captured
manifest, typed CupidObj inventory and candidate image as immutable views. It
first validates the inventory, then derives the expected layout independently
of the producer. It compares every candidate byte, including the system area,
primary descriptor, terminator, both path tables, all directory records, Rock
Ridge continuation, captured payloads and zero padding.

Identifier allocation follows source-name order; records follow allocated
identifier order. Directory numbering and extent placement use breadth-first
order. File extents use the complete case-folded source path order. Empty files
have extent zero. Both byte-order copies, PX modes and link counts, fixed dates,
SP/TF/CE/NM/ER entries and the legacy descriptor identifiers remain exact.

The checker uses the Toolchain arena for bounded temporary nodes and index
arrays, then rewinds it on success or failure. It retains no mutable global
state and creates no second complete image. Inputs must remain immutable and
precede the arena mark; mutable result and diagnostic storage must be disjoint.
The whole expected image must fit the existing 32-bit source view. Layout
overflow fails before any payload comparison. Failure clears the result and
terminates a diagnostic when capacity is nonzero; zero capacity permits NULL.

The module performs no filesystem access, tool launch or publication. It shares
the request types with CupidObj and the inventory validator, not the author's
layout or byte-generation implementation. The future transaction must run the
frozen checked author first and pass its candidate to this checker before
publication.

## Validation and failed approaches

The 17-method suite passes with native compilation and with checked CupidC
callers on Windows and Linux: 68 selections, 68 executed and no skips. Checked
callers execute as native PE32 and ELF32. Their core, inventory, checker and
caller objects form four byte-identical pairs. The active fixture and a mixed
identifier-collision fixture match both checked CupidObj and the Python oracle.

Cases cover reversed requests, manifest line endings, empty members, identifier
collisions, breadth-first layout, exact/spanning payload blocks, full 512-file
and 512-directory inventories, maximum depth and component size, mutations
throughout every format region, truncation, extra bytes and reordered extents.
They also check bounded diagnostics, null arguments, allocation failure, arena
restoration after repeated success/failure, overflow before payload reads and
concurrent independent jobs. Checked callers repeat the operation, preserve a
pre-existing arena allocation and run null-argument and overflow checks.

The first native compile omitted explicit arena alignment arguments. The fixed
calls use eight-byte node and four-byte index alignment. The first fixture
harness included unsupported punctuation and later omitted source-name sorting
in its in-memory oracle. Portable names and canonical sorting fix the harness;
the collision fixture now also runs checked CupidObj. Native Windows compilation
uses the usual CRT compatibility define for standard `fopen`. The first isolated
checked capture omitted an included header. A later adapter rejected entry 513
before the validator could test it; one extra test-only slot preserves that
negative case. Original failures remain recorded.

The ordinary package-style unittest invocation also exposed an unqualified
sibling import. A qualified `tests` import fixes it on both hosts. Both package
and discovery selections pass; unchanged compiled callers are rehashed before
reuse and run through the corrected suite.

Evidence is `iso-image-paired-independent-v2.json` under
`cupid-native-iso-proof-20261005`. It rereads sources, terminal command receipts,
suite logs, objects, both image formats and unchanged checked seeds.

## Remaining work

Integrate retained manifest/tree capture, checked author execution and this byte
check with CupidBuild's guarded candidate publication. Then qualify the producer
matrices, release consumption, real ISO regeneration and feature 17 before
changing the normal recipe. The installed seeds and current 447 CupidBuild/five
Python ownership counts stay unchanged in this step.

The updated manual passes an isolated installed-seed kernel/image build and
strict four-CPU runtime smoke. Independent verification checks the exact manual,
all 429 objects and sixteen artifact rows, with only its wrapper object changed.
This qualifies the documentation packaging, not native ISO production ownership.
