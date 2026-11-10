# ADR 0348: Complete raw-map v2 and transactional hosted publication

## Status

Accepted on 2026-08-25.

## Context

Hosted CupidASM already returned source-resolved control edges and wrote
`cupid.raw-map.v2`. The in-OS adapter kept only raw bytes and ranges, then wrote
v1 maps. An in-OS rebuild of the bootloader or SMP trampoline would therefore
discard the edge evidence used by strict CupidDis validation.

The hosted `cupidasm -f bin --map` command also wrote the public image before
the map. A map failure could leave new bytes beside an old or missing map even
though the command returned failure.

## Decision

Carry the shared assembler's borrowed raw-edge array and count through the
public in-OS artifact result. Validate the complete edge shape before rendering
canonical v2 text. Local, external, and unprovable rows must agree with source
order, image bounds, code ownership, instruction mode, address, and far
segment. Edge storage and its count must be present together, including the
zero-edge case. A validation or buffer-growth failure rewinds the output.

Make the hosted command publish its image and map as one recoverable pair. It
writes both adjacent candidates, then writes a linked v2 pending record beside
each member before moving either target. Backups preserve old files, while
validated absent-file records distinguish a first publication from an
interrupted replacement. After both targets move, the publisher upgrades the
records to v3 one at a time. One exact linked v3 record is the commit witness
for the pair. A v2 record remains pending and cannot use a matching legacy v1
record as commit evidence. Exact recovery inspects the whole pair, independent
of marker order, before it restores the old state or finishes committed
cleanup. It removes nonwitness markers first and the last valid v3 witness only
after private cleanup succeeds. A nonmatching record can clean only the
trusted entry beside its own marker and cannot touch an unrelated private
pair. Recovery replaces a backup over the public target directly, so a failed
replacement leaves the readable target in place. Version 1 remains readable
only for recovery of publications created by the earlier protocol.

The native Windows fixed-point plan links the publication startup and runtime
objects into CupidASM and admits their four Kernel32 imports. This keeps the
source-head command's recovery wrappers inside the reconstructed tool image.
The Linux fixed point's native Windows behavior evidence uses the same closure
and import profile.

Malformed records and tombstones fail before a public mutation. Reserved
private suffixes, canonical absolute paths, exact record contents, and pair
membership prevent a recovery file from naming an unrelated public target.

## Evidence

The in-OS contract covers local relative and far edges, an external far edge,
and an indirect unprovable edge. Negative cases cover missing storage,
ordering, code ownership, target address, mode, classification, rollback, and
recovery. The focused kernel module passes all seven tests, all nine direct
artifact and ELF cases pass, and checked-seed CupidC builds
`kernel/lang/as.o`.

The hosted regression first showed that a failed map write replaced the image.
The hosted and kernel modules pass 41 tests. They cover candidate failure,
repeat publication, old pairs, both-absent and mixed prior state, stale commit
cleanup including a single surviving pair marker and a blocked-cleanup retry,
malformed state, hostile records, rollback, and recovery. Fault seams cover
publish, commit conversion, backup cleanup, backup restoration, marker
corruption, and marker removal, and production builds compile those seams out.
Mixed v1 and v2 records are tested in both marker orders and restore the old
pair because neither ordering contains a v3 witness. A locked backup forces
replacement failure on Windows and proves that the public target survives.
Successful hosted image bytes and v2 map text remain unchanged. The in-OS map
intentionally moves from v1 to v2 and adds the retained edge rows. The
hosted-adapter self-host contract and `git diff --check` also pass.

The first fresh native Windows fixed point rejected stage-two CupidASM because
`cupid_windows_delete_file` was unresolved. The derived-plan contract failed
with the same missing closure. Adding the publication objects and imports made
that contract pass before the fixed point was repeated.

The repeated proof built and relinked the expanded image, then found that the
behavior check still validated CupidASM against the old twelve-import tool
profile. That stale expectation placed the import address table four entries
too early and reported a noncanonical layout. A focused policy test now binds
the relink validator to CupidASM's plan-derived import profile.

The first fresh Linux proof also found a separate native Windows evidence plan
that still linked CupidASM without the publication closure. Both compared
linkers rejected unresolved symbol 111, but their diagnostics named different
stage object paths. The bounded mismatch evidence exposed the common failure.
A second policy test now requires the publication objects and linker import
profile in that plan.

## Consequences

Hosted and in-OS CupidASM now preserve the same source-resolved raw control
edges. Both public adapters reject a partial raw artifact instead of silently
publishing a mismatched image and map.

The two renderers still implement the schema separately, so later work should
consider a shared bounded raw-map view to reduce drift. The hosted publisher
does not add a concurrent-writer lock or durable filesystem transaction. No
normal recipe, checked seed, or source owner changes here.
