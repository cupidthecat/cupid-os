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
segment. A validation or buffer-growth failure rewinds the output.

Make the hosted command publish its image and map as one recoverable pair. It
writes both adjacent candidates before moving either target. Backups preserve
old files, while validated absent-file records distinguish a first publication
from an interrupted replacement. A versioned commit record names only the
canonical backup, marker, and absent-record paths for the pair. Recovery either
restores the old state or finishes committed cleanup before another
publication begins.

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
The completed suite passes 41 focused tests. It covers candidate failure,
repeat publication, old pairs, both-absent and mixed prior state, stale commit
cleanup, malformed state, hostile records, rollback, and recovery. Successful
image bytes and v2 map text remain unchanged. The hosted-adapter self-host
contract and `git diff --check` also pass.

## Consequences

Hosted and in-OS CupidASM now preserve the same source-resolved raw control
edges. Both public adapters reject a partial raw artifact instead of silently
publishing a mismatched image and map.

The two renderers still implement the schema separately, so later work should
consider a shared bounded raw-map view to reduce drift. The hosted publisher
does not add a concurrent-writer lock or durable filesystem transaction. No
normal recipe, checked seed, or source owner changes here.
