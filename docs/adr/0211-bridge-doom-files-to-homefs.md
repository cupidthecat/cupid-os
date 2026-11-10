# ADR 0211: Bridge Doom files to homefs

## Status

Accepted on 2026-08-02.

## Context

The Doom port sent relative config and save names toward `/home/doom`, but
several pieces of the path were placeholders. `remove`, `rename`, and `mkdir`
reported success without changing the filesystem. The active config routines
were disabled outside `ORIGCODE`, and a failed save could delete the previous
slot before the temporary file was committed. Each vendored translation unit
also had its own static `errno`, so a helper such as `M_FileExists` could not
see an error raised by `dg_fopen`.

The VFS had no native rename operation. Its generic copy-and-unlink fallback
could not give Doom the same-filesystem replacement rule it needs, and it
could leave a partial destination. HomeFS and RamFS also allowed an open node
or directory iterator to outlive unlink or replacement, which made their file
descriptors point at freed storage.

HomeFS persists its whole tree inside the FAT16 `HOMEFS.SYS` container. The
old FAT overwrite order freed the previous cluster chain before its directory
entry named the replacement. A failed cache write or power loss in that
window could destroy the only reachable copy. Cache flushes did not return
device errors to their callers, so the upper layers could not tell whether a
commit reached the disk.

Cache misses could also overwrite a victim before the incoming disk read had
succeeded. FAT16 collapsed a missing file, an exhausted handle pool, and an
I/O failure into the same result. It did not bind an open handle to the exact
directory entry it had read, so replacement and deletion could invalidate a
live reader. The VFS accepted paths outside the one-level 8.3 namespace that
the driver can actually represent.

Finally, both `/home` and `/disk/HOMEFS.SYS` could mutate the same container.
A malformed existing container fell through to a fresh import, and a second
HomeFS mount could create another live owner. The asset-free diagnostic made
these costs visible: every small mutation rewrote the complete container.

## Decision

Keep one `dg_errno` object in dglibc and expose it through the compatibility
header. Resolve the active config directory, `.savegame` directory, config
names, temporary save name, and slot save names beneath `/home/doom`.
Absolute paths keep their existing meaning. Reject traversal, unsupported
nested virtual save paths, and names that do not fit the resolver buffer.

Implement dglibc file operations on the VFS with the real access flags.
Opening a directory as a stream fails with `EISDIR`. Append seeks to the end
for every write, not only at open. Read, write, seek, tell, get, put, flush,
and close errors reach the stream and shared `errno`. Element-count and
allocation arithmetic is checked before either the VFS or allocator sees a
wrapped size. Static standard streams may be closed without freeing static
storage.

Add native rename to the VFS contract. A mount may replace a non-directory
destination only after both parents and the source have been resolved.
Cross-mount rename returns `EXDEV`; there is no copy-and-unlink fallback.
HomeFS and RamFS retain the original tree if validation fails, reject a busy
source or destination, and treat same-node aliases as success. Nodes carry an
open count so unlink and replacement cannot invalidate a live file or
directory descriptor.

Enable Doom's production config load and save routines for Cupid OS. A save
writes a sibling temporary file, checks every output and close, then renames
it over `default.cfg`. The previous config remains visible if any step fails.
Loading uses a bounded line parser instead of an `fscanf`/`feof` loop. Integer
conversion checks `ERANGE`, strings remain bounded and quoted, and each new
Doom session restores the registered defaults before reading the file. The
port owns and releases config strings and generated directory names that
upstream normally leaves to process teardown.

Use the same transaction for game saves. `g_game.cc` closes and validates the
temporary stream, then asks native rename to replace the slot. It no longer
removes the old save first.

Make homefs serialization and import fail closed. Size arithmetic is checked,
deserialization uses subtractive bounds, names reject embedded NUL, slash,
reserved components, and duplicate siblings, and a tree may be at most 32
levels deep. Import callbacks retain enumeration, allocation, and read errors
instead of sealing a partial container. A failed mount, flush, close, or
unmount keeps enough live state to retry or report the error.

Return block-cache writeback status from `blockcache_flush_all` and
`blockcache_sync`. A dirty victim is clean only after its old bytes have been
written successfully. An incoming block is read into scratch storage, so a
failed device read cannot attach partially changed bytes to a new LBA. A live
mock-device self-test injects both failed reads and failed writes at this
boundary.

FAT16 writes and syncs a new chain before publishing its directory sector. If
publication cannot be flushed, it restores and flushes the original sector
before releasing the new chain. Only a durable new entry allows the old chain
to be released. Deletion publishes and flushes the deleted entry before it
frees the detached chain. Directory creation initializes and flushes the new
directory before its parent entry makes it reachable. Every failed
pre-publication path releases its unpublished chain. A failed cleanup may leak
old clusters, but it cannot make the newly published file unreachable.

Give checked FAT opens distinct missing-file, handle-pool, I/O, invalid-name,
and busy results. Each live handle remembers its directory sector and slot;
replacement and deletion reject that exact entry until the reader closes.
The FAT VFS exposes only the root or one directory plus a canonical 8.3 name,
and it returns the checked error rather than guessing that every failure means
the file is absent.

Reserve `HOMEFS.SYS` when HomeFS mounts. A pre-existing raw FAT handle makes
the mount busy. While mounted, raw writes, deletion, and write-capable VFS
opens through `/disk/HOMEFS.SYS` are rejected; HomeFS alone may use the private
reserved-write path. A corrupt existing container now fails the mount instead
of triggering a fresh import, and a second live HomeFS mount is rejected.

Add nestable HomeFS mutation batches. Mutations remain visible in the live
tree and mark it dirty, while intermediate flushes defer publication. The
outermost end performs one container write and returns its result. An
unmatched end, depth overflow, or unmount during a batch fails. The dglibc
diagnostic wraps its related probes in one batch, which removes repeated
whole-container rewrites without weakening the durable boundary.

## Evidence

The checked seed compiles every changed filesystem and Doom translation unit.
The exact 80-root Doom-tree frontier passes twice, including deterministic
locks for the changed `i_video.cc`, `info.cc`, and `g_game.cc` objects. The
three compatibility roots also match between the hosted and Cupid-built
compilers and across two checked-seed runs. The closed profile covers all 83
sources and 290 visible headers.

The 67,155-byte active dglibc source produces a 93,332-byte object with
SHA-256
`e2496b01c93a7858a0c035b53aea0ad834d95d2be3f7ae49574d1759ebec34d6`.
The libc-stub object is 17,084 bytes with SHA-256
`a2cef82df789e5770dc91bbe5bb7b4a41dfcbe788f587eec6fc0f6265433c319`.
The platform object is 10,352 bytes with SHA-256
`53537aabdaaa5de1db63403f569253f6be829b59387bebbe853347b825050c8a`.
The closed profile manifest is 69,366 bytes with SHA-256
`e77c8a0dc238b1a6f2257f273cf3367dba930c914e6a5806adf058621bbff4a4`.

`dglibc_test` covers native rename boundaries, busy open nodes and directory
iterators, config parsing and round trips, repeated session defaults, a failed
temporary write that preserves the prior config, checked integer parsing,
short copy behavior, injected block-cache failure, the exact RamFS size limit,
FAT collisions, clamped reads, state-aware handle exhaustion, busy
replacement, one-level 8.3 paths, HomeFS ownership, exact depth, and the outer
batch publication. Source contracts pin the FAT publication and rollback
order, cache identity, checked-open statuses, container reservation, and the
end-of-directory marker fix.

Two e1000 and two RTL8139 guest runs use the same checked-seed image. One run
per NIC launches two missing IWAD paths in a single shell and completes the
expanded dglibc diagnostic. A second run per NIC keeps the swap feature's FAT
handle open, then completes the full stateful frontier, including framebuffer,
audio, speaker, storage, desktop, terminal, and in-OS CupidC checks. The
stateful run is important: it proves that handle-exhaustion diagnostics count
the handles they actually acquire instead of assuming an empty global pool.

## Rejected alternatives

Successful no-op filesystem stubs were rejected. Doom uses these calls as
commit operations, so pretending they worked is data loss.

Copy-and-unlink rename was rejected. It cannot make replacement atomic, it
changes cross-mount semantics, and a short write can leave two misleading
paths.

Deleting the old save or homefs chain before publishing the replacement was
rejected. It creates a window with no reachable valid copy.

Flattening any path containing words such as `save` or `config` was rejected.
Only Doom's known relative names belong under `/home/doom`; unrelated names
must keep normal VFS meaning.

Rewriting the vendored parser or save format into a smaller Cupid-specific
format was rejected. The toolchain and compatibility layer now support the
active behavior instead.

## Consequences

Doom's active config and save code now reaches homefs through checked VFS
operations. Repeated shell launches begin from clean defaults, and a failed
replacement leaves the prior reachable file in place whenever the original
directory sector can be restored.

HomeFS has one live owner for its FAT container. Corruption stops the mount,
and explicit mutation batches reduce write amplification while keeping the
outer publish error visible. FAT handles and cache entries retain their exact
identity across failures instead of turning a resource or I/O error into an
accidental create.

The source under `kernel/doom/src/` is no longer byte-for-byte upstream:
`d_main.cc`, `g_game.cc`, `i_system.cc`, and `m_config.cc` contain deliberate
Cupid OS lifecycle and storage corrections. They remain full Doom sources and
still compile through the closed CupidC profile.

No injected power-cut test has yet proved the FAT ordering on a real device,
and there is no crash-recovery journal for a failed rollback. The repository
also has no staged IWAD in this checkout, so gameplay, input, game audio,
menu-driven save/load, and persistence across reboot remain open.
`TempleOS/` remains untouched reference material.
