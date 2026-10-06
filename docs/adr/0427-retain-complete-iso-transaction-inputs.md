# ADR 0427: Retain complete ISO transaction inputs

## Status

Implemented and tested on 2026-10-05. Guarded ISO
publication, seed carriage and the normal recipe handoff remain pending.

## Context

The ISO capture and bundle contracts preserve all 512 fixture entries. The
generic hosted transaction previously accepted 512 frozen files in total,
including its initial source. A complete file fixture therefore exceeded the
transaction before its manifest and checked cohort could all be retained.

## Decision

Extend the generic input table's maximum to 528. This covers one manifest,
512 fixture files, two seed manifests, twelve tool images and one reviewed
release. `transaction_open` freezes the initial source into the first slot.
The existing growth and reservation implementation remains; allocating the
complete table is optional. Input 529 and reservations above 528 fail with
the existing bounded-input diagnostic.

Keep file-identity rejection, live and frozen snapshots, output-alias checks,
publication locks, final revalidation and cleanup for every added slot.
Directory discovery and profile input bounds are separate and retain their
existing values. No new interface or reduced ISO request limit is needed.

## Evidence

Two new positive methods fail against the original 512 bound on each host.
The ordinary freeze loop rejects fixture input 512 because the manifest
already occupies one slot; reserving the complete table also fails.

All eight new methods then pass with native and Cupid-built callers on both
hosts, with no skips. They read each frozen payload, recheck the full table,
publish through a frozen writer and preserve the timestamp of equal output.
The fixture models the cohort's fifteen slots with regular files, including
that writer. Seed-authority validation remains a separate contract.
Negative cases reject input 529, a 529-slot reservation, a shared identity
at input 513, an output alias at input 526 and a same-size edit at input 526
with its timestamp restored. Failed cases preserve output bytes and timestamp,
clean private state and permit a subsequent complete transaction.

The existing observer suite also passes with both adapters on both hosts.
Across all suites, 258 methods are selected, 248 execute and ten platform
cases skip. Both Cupid-built caller objects are 21,352 bytes with SHA-256
`9d353582db3bcb053841ab01d27b4c87bee2358edf7df7599b18e025ae40cb59`.
Independent evidence rereads actual closed commands, logs, caller images,
source copies, i386 relocatable objects, PE32/ELF32 structure and unchanged
installed parents. The receipt is `iso-input-capacity-paired-independent.json`
under `cupid-native-iso-proof-20261005`.

The first independent collector used a nonexistent seed-record attribute.
Reading the declared `tools` field corrects the collector; no capability or
test changed. Windows Git cannot be executed directly in the current WSL
session. The native source control instead uses a Windows-captured tracked
inventory and rereads every copied file before testing.

## Consequences

The transaction can retain a complete ISO file request and paired cohort
without pruning fixture data. The ISO publisher still needs to bind its
borrowed observer, check the authored image and retain authority through the
last publication checks. Installed tools and ownership remain unchanged at
447 CupidBuild and five Python actions across 452 transforms. New source
already uses `.cc`; no suffix migration is due. `TempleOS/` remains untouched
reference material and is excluded from all progress counts.
