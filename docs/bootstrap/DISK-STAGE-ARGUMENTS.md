# Native disk stage argument interpretation

The current [reproducible argument owner](NATIVE-IMAGE-ARGUMENT-OWNER.md) retains
the stage splitter and WAD alias behavior in an unapplied C patch with complete
actual-host oracles. Its four callers pass every paired record; both installed
seed replays also pass. The historical `cp7` source copies described below are
absent from the current worktree and do not establish a current normal handoff.

The separate source under `C:/Users/admin/cp7/disk-stage-arguments-source1`
implements the stage splitter and WAD destination naming needed by the normal
image command. Linux retains the same source under
`/var/tmp/disk-stage-arguments-source1`. The existing command and publisher
continue to own their current recipes.

The subsequent [image option owner](NATIVE-IMAGE-OPTIONS.md) uses these same
functions to interpret all required values, repeated groups and stage order.
It passes separate four-caller and independent checks. Discovery, observer
lifetimes and normal publication remain outside both argument interfaces.

## Source and interface

`toolchain/cupidbuild_disk_stage_arguments.cc` and its header provide two
operations. `cupidbuild_disk_stage_argument_parse` borrows source and guest
destination views from a UTF-8 `SRC:/guest/path` argument. It uses the existing
drive-colon rule: an ASCII drive letter's colon belongs to the source when a
later colon exists; otherwise the first colon separates the paths. Empty sources
remain empty views for subsequent host Path interpretation. The destination
must start with `/`; complete FAT destination validation remains with capture.
The splitter neither resolves a path nor grants source authority.

`cupidbuild_disk_wad_destination` takes the basename selected by the caller's
host path rules and the WAD's original position. It retains the existing
case-insensitive Freedoom/Doom aliases and their precedence; unknown names use
`/wads/wadINDEX.wad`. Every unsigned 32-bit index is represented. The caller
still places explicit stages before WAD stages, preserving sequential replacement.
Both operations use the existing UTF-8 codec and reject embedded NUL or invalid
Unicode. Destination output supports a length query, requires terminator space
and clears its recorded length and first writable byte on failure.

The two other new paths are
`toolchain/tests/cupidbuild_disk_stage_arguments_contract.cc` and
`tests/test_cupidbuild_disk_stage_arguments.py`. No existing producer file changes.
All 99 normal compiler inputs retain snapshot
`5a2d30853cc1e064c93a2795428c7511822f8833e03085e5e044efc895f0fe9b`.

## Execution evidence

Native Clang and the new normal-source stage-four Cupid compilers build and run
the actual contract on both hosts. All eight methods pass per caller, for
32 method executions and 464 actual program invocations. The test compares
valid stage results and WAD destinations with the unchanged image command's
`_parse_stage` and `_wad_dest` functions. It also checks both host basename
profiles, alias collisions, Unicode, full-width indices, 10,000-byte basenames,
invalid encodings, borrowed views and output queries/capacity boundaries.
The first Python fixture used an invalid raw Windows string ending in a
backslash. Ordinary escaped quotation repairs the spelling before any native
or Cupid build runs; the runtime cases remain unchanged.

Build and runtime labels are
`disk-stage-arguments-{native,checked}-{build,contracts}-{windows,linux}1`.
Their build receipts retain 108 source/support controls and actual programs.
Cupid builds use two workers, 360-second compilation and 180-second linking
bounds. Every checked object passes the strict disassembler. Each actual
program invocation keeps sixty seconds; Linux also keeps a 32 MiB address-space
limit. Native compilation enables the existing C11 warning and error checks.

`disk-stage-arguments-independent1/closed.json` passes in 6.229 seconds. It
rereads every source copy, command, complete program and result sequence;
independently recomputes stage and alias results using the original Python
functions; and compares all three checked object pairs byte for byte. Complete
output sequences agree across all four callers. All 99 normal compiler inputs
and fifteen installed seed files remain unchanged. The actual compilers and
borrowed startup/runtime objects match their retained stage preparations, and
checked executables keep their exact static profiles.

## Remaining native image command work

This module supplies stage interpretation and alias naming. The complete image
option parser, absolute host-path resolution, external-root deduplication and
observer lifetimes still need integration with the accepted publisher. That
integration must preserve missing optional observations, stage order, prior
image reuse, force-format behavior, complete geometry and existing publication
guards. The accepted 2,048, 2,050 and 4,096 MiB geometries retain their own full
template and image acceptance requirements.

The normal integration host still lacks public root-identity comparison for
caller deduplication. A separate [retained observer comparison](OBSERVER-ROOT-IDENTITY.md)
now passes 52 actual native/Cupid executions and complete independent checking.
It preserves ordinary root, payload and absence checks before comparing captured
physical identity; command and publisher integration remain open. The publisher
rejects distinct observer pointers for one physical root, so textual path
equality alone cannot supply the authority map.
The normal integration observer still requires a drive-rooted Windows path.
A separate [UNC observer implementation](OBSERVER-UNC-ROOTS.md) now passes
48 native/Cupid UNC methods and 54 actual calls on real local shares, retaining
metadata, payload, absence and no-reparse checks. Its source and complete
ordinary-root/Linux output checks also pass independent review. Full discovery
and publisher integration remain open; the stage splitter alone does not grant
that filesystem authority.

The new module is still in the separate source copy. Committed source
integration, full producer qualification and seed/recipe carriage remain open.
Normal ownership remains 449 CupidBuild and three Python actions. Full Doom
runtime acceptance and the existing standalone Windows cleanup failure retain
their separate requirements. TempleOS remains read-only and excluded.
