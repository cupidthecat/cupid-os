# ADR 0379: Bring kernel CupidASM publication to linked recovery

Date: 2026-08-30

Status: Accepted

## Context

The hosted CupidASM command already used linked v2 pending records, v3 commit
witnesses, and absence tombstones when it published a raw image with its map.
The kernel publisher had a weaker protocol. It wrote v1 completion records
only after both public targets moved, so an interruption before that point
left recovery to infer the old state from backups. It could not distinguish a
target that was originally absent from one whose backup was lost. Backup
recovery also unlinked the public target before replacing it, which made a
failed restore turn a readable target into a missing file.

The two publishers therefore made different recovery decisions for the same
artifact pair even though they used the same CupidASM core and artifact
formats.

## Decision

Use the hosted linked-record model at the public kernel publication boundary.
Each member now has five paths: public target, candidate, backup, absence
tombstone, and transaction record. The publisher writes both candidates and
matching v2 records before it moves either target. A validated tombstone
records each target that did not exist when the transaction began. Existing
targets move to backups.

After both candidates become public, the transaction records advance from v2
to v3 one at a time. One exact v3 record commits the pair. Cleanup removes
nonwitness records first and the final witness last. Recovery treats an exact
v2 transaction as pending and restores the old state. It restores backups
directly over their targets and removes targets backed by valid absence
tombstones. An exact v3 witness preserves the new pair and finishes private
cleanup. Either member can supply the surviving witness. A v2 record beside a
legacy v1 record is still pending, regardless of which marker is read first.

Recovery continues to accept valid v1 records created by the earlier kernel
publisher. A linked committed record from an overlapping transaction cannot
authorize a new pair. The publisher first finishes the trusted member's
cleanup, retains a v3 witness, and returns an error.

The public request supplies two separate bounded scratch spans. Recovery keeps
one parsed record stable while it reads a peer record. The in-kernel caller
also derives `.cupid-as-absent` paths for the artifact and optional map. The
public contract accepts only normalized absolute targets whose candidate,
backup, tombstone, and record paths are the exact target plus
`.cupid-as-new`, `.cupid-as-old`, `.cupid-as-absent`, and `.cupid-as-done`.
Recovery therefore cannot apply a valid private record to another target.

## Evidence

The public `as_artifact_publish` contract covers old/old, absent/absent, and
mixed prior state; either surviving v3 witness; repeated publication; v2 and
v1 marker order; a standalone valid v1 marker beside either member;
overlapping committed transactions; malformed records and tombstones;
private paths for another target; identical and partially overlapping scratch
spans; and failures during candidate, pending-record, tombstone, target-move,
v3-conversion, cleanup, and recovery phases. Ambiguous tombstone writes recover
on the next call. A restore-failure case requires the partial public target and
its backup to remain readable, which proves that recovery no longer unlinks
the target first.

The combined hosted and kernel CupidASM modules pass 41 tests. Checked-seed
CupidC builds `kernel/lang/as.cc` and `kernel/lang/as_elf.cc` with the new
request layout.

## Consequences

Hosted and in-OS CupidASM now use the same recovery decision for paired
artifact publication. A pending transaction can restore an originally absent
member, and a committed transaction can finish from either surviving witness.
Corrupt private state fails without guessing at a public replacement.

This is still not a durable two-file filesystem transaction. The VFS has no
publisher lock, so concurrent shell writers remain outside the supported
contract. Repeated guest publication remains a separate runtime proof.
