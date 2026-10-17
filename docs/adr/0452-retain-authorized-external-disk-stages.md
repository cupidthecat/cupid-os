# ADR 0452: Retain authorized external disk stages

## Status

Implemented and tested privately on 2026-10-08. Publisher/CLI integration,
committed producer qualification and normal recipe adoption remain open.

## Context

The normal disk recipe can select payloads outside the repository. The private
host API can borrow an external observer and freeze a regular file beneath it.
Optional stage capture needs to retain that same authority for present files
and missing paths, and distinguish identical logical names under different roots.

## Decision

Append an optional external observer to each stage request. A null value keeps
the repository observer. Require the transaction's exact primary observer and
the previously borrowed external observer before optional source discovery,
including absence. The host requirement revalidates both lifetimes and forbids
publication on an invalid, changed or late request. It grants no new authority.

Key capture reuse by selected observer and logical spelling. Freeze present
external files through the existing bounded API; retain missing-path observations
under their selected observer. Every explicitly registered request keeps its
complete size/digest check. The caller retains all observers through transaction
close. External roots keep complete metadata checks; the primary root keeps its
existing publication rules. Capture still owns frozen paths and destinations.

The next private publisher handoff grants the observer authority explicitly
selected by each stage after seed capture binds the primary observer. It borrows
each distinct pointer once and keeps the existing rejection of repeated physical
roots. External stages skip automatic repository kernel/required-file reuse.
All four native and Cupid full-image selections pass, with 172 executions and
four platform skips. Complete independent image/FAT checks also pass. The
execution compiler still uses the recorded, unqualified mixed width extension;
source integration, qualification and recipe adoption remain open.
`docs/bootstrap/EXTERNAL-DISK-PUBLISH.md` records this separate source and its
current acceptance scope.

## Evidence and limits

The 34 new external and 21 repository methods pass through native and Cupid
callers on both hosts: 220 selections, 212 executions and eight POSIX skips.
Both complete 65 MiB and 200 MiB controls pass their original bounds, with 32 MiB
on Linux. Independent checking covers 119 source controls, actual products,
parent tools, strict execution profiles, complete external before/after files,
timestamps, namespaces and all original command limits.

The first fixtures used invalid guest destinations, an excessive input
reservation and a non-ASCII retained FAT alias. Corrected positive cases and
separate useful rejections preserve the existing FAT and 528-input contracts.
The original failed source copies and products remain retained.

The capture fixture publishes a small replacement output. It does not establish
integration into the optional FAT publisher or normal disk command.
`docs/bootstrap/EXTERNAL-DISK-STAGES.md` records the exact evidence, fixture
repairs, metadata-only oversized-file rejection and remaining ownership work.
