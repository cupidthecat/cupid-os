# ADR 0415: Validate artifact policy from reviewed seed observations

Date: 2026-10-04

Status: Source capability; paired release installation pending

Native artifact verification already captures the release, both manifests and
all twelve images. The shared seed reader validates their supported plans and
release identities, and the image readers validate actual formats and imports.
The subsequent CUPSIZE2 policy parser repeats manifest validation through an
older implementation capped at 78 inputs. It rejects the 82-input ABI cohort.
Its historical parent rules also cannot consume arbitrary reviewed new parents.

Keep the CUPSIZE2 API and its historical checks. Add a separate immutable-byte
policy API for CUPSIZE3 observations. Its request contains the policy, selected
logical manifest paths, six Linux sizes, six Windows size/digest pairs, six
Windows image observations and sixteen artifact observations. Seed facts use
the shared reader's role order. The policy module validates this envelope and
the complete existing policy rules; it does not validate release authority,
manifest provenance, executable contents or file lifetime.

CupidBuild constructs this request only after its retained release passes the
complete paired seed validator, both release-aware manifest readers and every
image check. Alternate Linux execution cohorts use the same release-aware
reader. Input and directory observations remain held through final revalidation
and close. The command keeps its interface and success report. No new parent
hashes are embedded in either C reader.

Keeping the policy request separate prevents hosted policy-only contracts from
acquiring the checked runner's filesystem, hashing and process dependencies.
Existing CUPSIZE2 callers continue to validate their historical manifests.
The two entry points reject each other's request magic and clear results on
failure. Allocation failure, bounded diagnostics, concurrent calls and recovery
remain part of their contracts.

The bootstrap log records host and Cupid-built checks separately from staged
producer evidence. The earlier ABI producer capture is immutable; it does not
qualify these changed sources. Fresh committed producer and consumption proofs,
OS acceptance, seed installation and the ABI Make handoff remain required.
