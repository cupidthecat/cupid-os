# ADR 0416: Carry authorized Linux plans into behavior fixtures

Date: 2026-10-04

Status: Source fix; committed full qualification pending

The Linux release qualification rebuilds the correct stages but fails its
first checked CupidObj runner case. Materialization replaces tool identities,
source identity and parent lineage while retaining the parent's Linux build
plan. The ABI candidate has 29 C sources; its parent has 27. The native reader
correctly rejects that old plan digest against the reviewed candidate release.
Windows execution manifests carry their native plan through provenance fields
and do not contain the Linux manifest's root plan fields.

Capture the verified candidate Linux plan as immutable encoded bytes when the
release request authorizes the rebuilt cohort. Linux behavior materialization
parses that retained plan, checks its digest against the selected release and
writes both plan and digest into the fixture. It preserves the parent lineage,
tool bytes, source identity and release payload. Windows fixture layout stays
unchanged. A wrong retained plan fails before creating the seed directory.

The actual prepared Linux runner reproduces the failure twice. A minimal
single-call replay fails in under one second. Comparing its boundaries finds
only the plan mismatch; all six images, release bytes and provenance fields
match. Replacing only the fixture's plan and digest with the independently
verified preparation plan makes that replay pass. The fixed coordinator then
passes the original complete checked CupidObj runner helper.

Regression tests exercise the real authorization/materialization seam, verify
both manifest formats, retain the plan independently of mutable caller data,
and reject wrong plans and duplicate JSON keys before publication. Earlier
unit checks observed source/provenance fields but omitted the Linux root plan,
so they did not catch this failure.

This changes qualification coordination, not compiler source or tool images.
The 82-input compiler snapshot remains unchanged. Preparations bind that
snapshot, plans, parents and actual artifacts; they remain unqualified. Fresh
committed control sources, release authoring, complete behavior qualification
and consumption remain required. Original frozen source captures, failed logs
and reviewed records retain their bytes.
