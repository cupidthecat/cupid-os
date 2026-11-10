# ADR 0428: Bind retained observers to publication boundaries

## Status

Implemented and tested on 2026-10-05. Seed
carriage, the complete ISO publisher and normal recipe handoff remain open.

## Context

The ISO capture owns immutable manifest and payload views while borrowing a
filesystem observer. A caller's unchanged check before publication leaves the
transaction's final rename checks unaware of that observer's directory
membership and payload observations. The transaction already rechecks its
frozen inputs, output parent, owner lock and candidate at those boundaries.

## Decision

Add `cupidbuild_host_transaction_borrow_observer`. It binds one successful
observer to a transaction with the same retained repository-root identity.
The caller keeps the observer alive until transaction close; the transaction
never closes or takes ownership of it. Null, poisoned, different-root,
repeated or binding after any publication attempt fails, including equal-output
reuse. Failed binding forbids publication
for that transaction and cannot be replaced by another observer.

Every ordinary publication boundary now rechecks a bound observer, including
the check after installing a candidate. File metadata and captured payload
digests, retained ancestor bindings and exact directory memberships remain
strict. The observed root uses identity rather than directory size/time
because the transaction itself writes publication names there. Explicit root
membership remains checked. The ordinary read-only observer interface retains
its complete root metadata check.

Rollback's existing recovery checks skip source/discovery requirements,
including the borrowed observer. A source-only observation failure can
therefore restore the verified old output and clean transaction state. The
existing recovery rules for ambiguous output, parent or candidate identity
remain unchanged. Observation binding does not replace frozen-input alias
checks or selected seed authority.

## Evidence

All ten new methods pass with native and Cupid-built callers on Windows and
Linux, with no skips. They cover publication, equal-output timestamp reuse,
unobserved namespace writes, wrong roots, null/poison/repeated binding,
same-size payload drift with restored timestamps, added members, empty-directory
drift, explicit publication checks and successful fresh recovery. Binding after
changed publication and after equal-output reuse both fail; the reuse case
preserves the original output timestamp.

Two native race methods pass per host. Each injects directory drift for an
existing and an absent output at the actual before-mutation or after-install
checkpoint. All eight fault cases reject publication, restore exact prior bytes
and timestamp or absence, clean transaction names and permit recovery. Linux
also rereads the newly installed candidate while paused. Windows retains its
candidate handle against external opens until that boundary finishes.

The existing observer and complete 528-input tests also pass through both
adapters on both hosts. Independent evidence checks 302 selected methods,
292 executions and ten platform skips, along with actual closed logs, source
copies, caller images, i386 objects and unchanged installed parents. The
Cupid-built caller objects are identical: 27,788 bytes, SHA-256
`3211597da4a6f2eef2ce55387ab9ee305add3017363550ec4bc9a6df3c9b6973`.
Evidence is `iso-bound-observer-lifecycle-paired-independent.json` under
`cupid-native-iso-proof-20261005`.

The first race fixture reused one directory across subcases and attempted an
external Windows read of the retained installed candidate. Fresh subcase
directories and verification after rollback fix those test assumptions. A
direct imported TestCase also selected unrelated tests; importing its module
keeps the race selection to its two intended methods. The original failed
invocation and fixture remain retained.

The first Windows checked regression times out during the existing high-word
metadata payload rejection after ten seconds. A complete 65-method retry passes
with the same caller bytes and unchanged bounds; its cause remains unproven.
This failure does not authorize changing the production observer limit.
The final lifecycle suite and complete observer regression pass on both hosts
without retries. The earlier eight-method binding receipt remains specific to
its captured source; it does not qualify the final lifecycle guard.

The full 39-method native preprocessing suite passes on each host. The measured
conditional inventory and unknown-token rejection also pass on each host,
for 82 further executions without skips. The added root-handle branch raises
the inventory to 427 `#if` and twenty `#elif` occurrences, with 61 distinct
expressions and 64 directive/expression pairs. The active root count remains
415. Independent evidence is
`iso-bound-observer-preprocessing-paired-independent.json`. The complete graph
still records 775 active sources and 452 transforms, with 447 CupidBuild actions
and five Python actions.

The 161,127-byte observer-binding manual passes fresh installed-seed
kernel/image and strict private four-CPU max/e1000 runtime checks with completed
ls and SMP verification. Independent rereading checks 1,589 source controls,
all 429 objects and sixteen artifacts. Only the manual wrapper changes, the
accepted FAT16 object stays identical and FAT data from sector 20,480 remains
intact. The raw kernel measures 9,595,256 bytes; final/pass-one ELF sizes stay
9,822,652 and 9,691,580 bytes. Only the raw-kernel policy row changes. The 200 MiB
image has SHA-256
`4250559828b4a1553b1d32bcbe042c91ed20662a3a5287f7d571273d45a192f4`.
Evidence is `manual-independent-windows.json` under
`cupid-native-iso-bound-observer-manual-20261005`. This installed-seed manual
acceptance does not qualify or install the new source cohort.

## Consequences

The ISO publisher can retain its capture observer through the transaction's
last checks without adding a callback interface or transferring ownership.
Callers must still freeze all ordinary publication inputs, reserve the complete
input table before retaining borrowed frozen-path strings, validate the paired
seed authority and independently check the authored candidate. Installed tools
and the normal ISO recipe remain unchanged. `TempleOS/` remains read-only and
outside all progress counts.
