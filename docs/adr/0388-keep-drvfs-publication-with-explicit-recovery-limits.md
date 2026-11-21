# ADR 0388: Keep DrvFS publication with explicit recovery limits

Date: 2026-09-19

Status: Accepted

CupidBuild continues to support publication on WSL Windows-mounted filesystems,
including `/mnt/c`. Recovery depends on the filesystem's retained-directory
semantics. A filesystem that preserves relative lookup through a renamed
directory must allow the rejected publication to restore the exact previous
output and timestamp. If lookup no longer reaches that directory, CupidBuild
must fail, preserve the verified previous file as recovery evidence, and leave
the foreign successor and ambiguous transaction state untouched.

## Evidence and boundary

A deterministic post-install profile test exposed this distinction on DrvFS.
The publisher had installed a candidate after parking the old output as a
verified hard link. A concurrent directory rename then caused `openat` through
the retained parent descriptor to return `ENOENT`. Candidate verification
failed, so the publisher did not attempt restoration through that ambiguous
binding. It returned an error and retained the old backup, candidate, lock,
and reservation. The old backup kept its file identity, exact bytes, and mtime.

An independent fixture reproduces the underlying filesystem behavior without
CupidBuild. After replacing a directory, lookup through its original descriptor
can read the replacement's child while `fstat` still reports the original
directory identity. `O_PATH` and reopening `/proc/self/fd/N` do not restore the
required semantics on the tested mount. Native Linux filesystem controls retain
access to the original child, and the same CupidBuild executable restores the
previous output there.

The tests therefore probe retained-directory lookup using a separate, owned
fixture on the publication filesystem. They do not infer behavior from a WSL
environment variable, mount name, or operating-system label. Native semantics
retain the strict old-output assertion. Limited semantics require one verified
recovery backup with the previous identity, bytes, and timestamp, an untouched
foreign successor with unchanged parent identity and mtime, nonzero publication
and cleanup results, and retained
transaction evidence. A compact ISO-pattern case exercises the same shared
publisher without the full Doom discovery fixture.

The focused five-case recovery and failure group passes on Linux and Windows,
with the separate POSIX rename case skipped on Windows. The profile case still
requires Windows to block that rename. Both native Linux restoration and
DrvFS backup retention also pass the compact replay with non-symlink recovery
targets and unchanged foreign-parent identity and mtime.

This qualifies the restore wording in
[ADR 0377](0377-add-typed-doom-profile-publication-to-cupidbuild.md). A readable
old backup is recoverable state, not successful restoration of the public
name. Recovery after namespace interference requires inspection; CupidBuild
must not delete ambiguous files or automatically promote an unchecked backup.
The existing same-user namespace limit remains. This is not a crash-durable
filesystem transaction or a guarantee against concurrent namespace replacement.

## Alternatives

Rejecting every `/mnt/c` publication would discard ordinary supported builds
because of a recovery limit triggered by concurrent parent replacement.
Reopening a reported path or checking the old directory identity alone does
not repair the observed lookup semantics. Weakening all Linux assertions would
hide regressions on filesystems that do support exact restoration.

No production-source repair is required for this recovery boundary.
The publisher already fails closed and retains the verified old bytes when
it cannot establish the output binding. This decision changes test coverage
and documentation, not OS behavior, source ownership, seed provenance, or the
toolchain's build dependencies.
