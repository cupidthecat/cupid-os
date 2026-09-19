# ADR 0385: Restore Windows candidates through a retained directory bridge

Windows publication rollback must use a verified directory handle without
`DELETE` access while moving the candidate back into private storage. The
ordinary retained private-directory handle has `DELETE` access for cleanup;
keeping it open makes NT's rename-back fail with `STATUS_SHARING_VIOLATION`.
The old output then cannot return to its occupied name. A source-drift test
exposed this sequence after a candidate was installed.

Open the bridge relative to the retained repository root and require its
identity to match the private directory before closing the cleanup handle.
Rename the retained candidate through the bridge. Reopen cleanup authority
relative to the repository root, then verify the directory identity and its
public binding before releasing the bridge. An uncertain binding still fails
closed and leaves recovery evidence.

This changes only failed-publication recovery. Ordinary directory sharing,
frozen-input sharing, and successful publication remain unchanged. Removing
`DELETE` access permanently was tested and rejected: rename-back succeeds,
but cleanup loses the authority it needs. Broadening delete sharing was also
rejected because it would weaken protection throughout the transaction.

The shared Windows publisher serves guarded assembly, JPEG embedding, kernel
symbols, kernel flattening, profile manifests, and the new ISO pattern
transaction. Its recovery test covers both an existing output and an initially
absent output without depending on a checked tool generation. The original
profile source-drift regression also requires restoration of the previous
manifest and removal of known private state.

The two seed-independent tests pass for existing and absent outputs. A real
directory-replacement attempt during the bridge is denied by retained child
handles; the test requires unchanged private identity and foreign contents,
then exact rollback and complete cleanup. It does not release frozen handles
to manufacture a replacement window. The original profile regression passes,
and the shared-publication group passes 13 cases with one older-seed skip.

Checked CupidC compiles the repaired host adapter for both targets. The Linux
object remains byte-identical because this repair is Windows-specific.

The checked seeds predate this repair. Paired reconstruction and promotion
must carry it before the normal profile-manifest recipe changes owner. No OS
source is simplified, no C translation unit changes owner, and `TempleOS/`
remains reference material.
