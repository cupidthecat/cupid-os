# ADR 0462: Retain optional source-file selections

## Image-stage follow-up, 2026-10-10

The separate [image-stage owner](../bootstrap/NATIVE-SOURCE-STAGE.md) supplies the normal
command's complete missing-path predicate without changing this optional
owner. Its 312 new calls and both complete repository replays pass. Full
discovery, physical-root interning and present-stage capacity remain open.

## Status

Implemented as a held patch on 2026-10-10. Four native/Cupid callers pass all
382 new contracts and both complete repository replays pass. The normal source
cohort and recipe ownership remain unchanged. Complete native image discovery
and publication remain open. The later [manual acceptance](../bootstrap/SOURCE-OWNER-MANUAL.md)
records these held APIs in the OS manual after paired builds and boots.

## Context

The required source-file owner retains a regular leaf and its original parent
request. Image stages also need missing inputs whose immediate parent does not
exist. The owner must retain the first absence beneath an existing directory
without losing the complete requested suffix or granting publication authority.

Ordinary root validation compares complete directory metadata. Borrowed source
validation checks directory identity while retaining descendants, first
absences, payload digests and captured memberships. Optional file views need
that source boundary so an unrelated write in their selected parent can pass.

## Decision

Add `cupidbuild_source_file_open_optional_quota` to the existing opaque owner.
Require a kind output and return `FILE` or `NONE` on success. Failure clears
the owner and reports `NONE`. Retain the five-field view, original lexical
rules, no-follow leaves, quota checks and 8,191-byte path bounds.

Resolve the nearest existing parent through the original directory binding,
then observe the complete logical suffix. Keep the first absent component.
Copy names before caller storage can be released and revalidate before exposing
any borrowed view. Missing-path appearance and original-binding drift reject.

Expose `cupidbuild_host_observer_require_source_unchanged` for the existing
source-validation boundary. Optional construction and views use it. Required
selection and ordinary validation keep their complete root-metadata checks.
Captured membership remains exact. Retained streaming adds payload custody;
metadata alone cannot detect restored-time edits.

## Evidence and limits

The [implementation record](../bootstrap/NATIVE-SOURCE-OPTIONAL.md) retains
all 382 optional calls, 232 unchanged required calls, 1,128 preceding API calls,
complete legacy/alias methods and both fresh repository replays. All 37 distinct
whole checked objects and twenty programs equal their originals. Thirteen
complete cross-host object pairs and all 59 strict checks pass. New runtime
commands keep sixty seconds and Linux's 32 MiB/10,240-descriptor bounds.

The original Windows share-root oracle, checked host-profile failure, full UNC
timeout and Python boundary diagnostic failure remain archived. Corrected
builders use the actual manifest host defines. The complete UNC retry and all
four unchanged-capacity controls pass; the first timeout's cause is unproven.

Python skips children of broken parent aliases on both hosts and children of
regular-file parents on Windows. The held owner rejects both forms. Native
image integration must resolve those compatibility differences. A later
[twenty-call diagnostic](../bootstrap/evidence/native-source-missing-revalidation-20261010.json)
also finds that Python permits absent ancestors to appear while the final
leaf remains missing. The held first-absence contract rejects that change.
Those calls retain the existing owner and observer behavior and evaluate
the publication predicate without claiming complete image publication.
The three Python coordinators and normal 99-input closure remain unchanged.
Complete image ownership remains open. TempleOS remains read-only and
excluded.
