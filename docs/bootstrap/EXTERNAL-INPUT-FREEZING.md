# External frozen inputs

The separate source under `C:/Users/admin/cp7/external-freeze-source2` adds
`cupidbuild_host_freeze_external_input_wide`. It requires a previously borrowed
external observer, a safe relative logical path and one nonempty private name.
It uses the existing fixed-block frozen-file writer and retains source snapshots,
complete digests, frozen metadata and output-alias checks through publication.
The repository-only interface keeps its existing authority.

The transaction borrows the external observer through close. Its frozen input
record retains that observer separately from previous-output and private-output
records. Input revalidation reads through the retained external authority.
External roots keep complete metadata checks, including at the final publication
boundaries. Failed or late capture clears results and forbids publication.

## Native evidence, 2026-10-08

Each host selects 32 methods. Windows executes 30 and retains two declared POSIX
skips; Linux executes all 32. Both corrected selections pass. Windows takes
69.868 seconds and Linux 76.553 seconds. Complete 65 MiB and 200 MiB payloads
transfer through frozen range reads into the owned candidate store and guarded
publication. Linux retains a 32 MiB address-space limit. Small cases keep
20-second bounds, the 65 MiB control has 180 seconds and the complete active
disk extent has the original full-publication 600-second bound.

Cases cover empty data, Unicode roots, equal-output timestamps, maximum explicit
extent, unauthorized and poisoned observers, wrong lifetimes, missing and unsafe
paths, directories, private names, duplicate identities, output aliases, late
capture and restored-time payload changes. Negative cases preserve complete
prior bytes and timestamps. The native tests retain complete source/output facts,
namespaces, commands, limits and diagnostics.

Evidence uses `external-freeze-native-windows3` and
`external-freeze-native-linux2` under `C:/Users/admin/cp7`. Earlier runs remain
failed evidence. The first Windows copy omitted the native UTF-8 helper. The
first implementation opened a metadata-only Windows handle before reading;
requesting payload access repairs that failure. Linux accepted an empty label
for an anonymous file; the external API now validates a nonempty component.

## Remaining acceptance

The checked builder captures 117 source controls and 106 producer paths, with
unchanged qualified parent tools and 360/120/180-second producer bounds. Its first
invocation lacked supporting Python modules in the copied source bundle. The
corrected build compiles the host implementation, then stops because the ordinary
contract caller uses undeclared `strtoull`. The shared hosted runtime also lacks
that interface. Extending the runtime is the next requirement; the caller remains
ordinary C source.

Cupid runtime verification, existing observer/capture regressions, independent
whole-product checks, optional publisher integration, normal CLI support,
qualification and recipe adoption remain open. This is private capability work.
The normal disk recipe and two SDK coordinators still belong to Python.
