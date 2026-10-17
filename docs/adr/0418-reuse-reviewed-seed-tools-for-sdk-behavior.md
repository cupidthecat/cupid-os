# ADR 0418: Reuse reviewed seed tools for SDK behavior

Date: 2026-10-04

Status: Authority boundary implemented; SDK integration pending

The ordinary Windows Toolchain publication retry reaches behavior checks, then
fails because its checked CupidObj runner has no release authority for the new
parent tuple. The unchanged production helper reproduces this failure in
0.397 seconds. Supplying the already reviewed release makes the real runner
and complete helper pass. The failure is separate from the earlier frontend
timeout.

Add `capture_seed_behavior_release` to the release coordinator. It captures an
explicitly selected regular file through the existing bounded reader, validates
all twelve artifact identities and provenance fields against independently
reviewed pins, and rechecks the selected Linux manifest and images through the
shared seed reader. It retains the exact release bytes and Linux plan bytes.

Its request authorizes behavior only when both rebuilt Linux tool sets match
the reviewed six-tool cohort exactly. Wrong membership, bytes, selection,
format or plan fails before materialization. Release and selected seed drift
also fail. The existing fixture materializer and runner wrapper supply and
recheck the immutable release through real child execution.

This reuses evidence for the same tool bytes. It does not certify a new source
inventory, compare rebuilt objects, or replace the publication author's native
stage comparisons. The SDK's source inventory remains its own observation;
the behavior fixture describes the reviewed cohort whose tools it contains.
`ReleaseRequest` remains the separate path for qualification of a newly
prepared producer cohort.

Both host boundary selections pass 37 methods, with two declared POSIX skips
on Windows. Real unchanged SDK helpers pass through the new request on both
hosts using qualified images as both stage tool sets. They retain the actual
81-input source inventory alongside the reviewed 82-input seed provenance and
reject a changed staged tool. These are helper probes, not SDK-generated stage
proofs or ordinary publication acceptance.

The updated embedded manual also passes incremental kernel/image acceptance,
all sixteen artifact checks, the complete source audit and a strict private
four-CPU boot with completed `ls`. Only its wrapped object changes among 429
objects; compiler producer inputs and installed seeds remain unchanged.

SDK integration must explicitly select the release, capture the coordinator
and pin-validator dependencies, preserve the native author's comparisons, and
retain release observations through final publication. The current SDK path
and its 92-input publication inventory remain unchanged. Normal publication,
seed installation and the native ABI recipe handoff remain pending.
