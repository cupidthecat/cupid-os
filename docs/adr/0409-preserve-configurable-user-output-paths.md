# ADR 0409: Preserve configurable user-output paths

Date: 2026-09-29

Status: Accepted; paired seed carriage and normal user compilation handoff verified

## Decision

Keep the existing user compilation path contract while moving coordination into
CupidBuild. Lexical normalization runs before source/output approval. The output
keeps the source basename with an `.o` suffix and remains below a permitted child
of `user`. Paths preserve UTF-8, spaces and nested configurable directories.
Windows syntax includes drive roots and UNC shares; the underlying retained
observer still requires a drive-rooted Windows repository.

Directory preparation has a separate opaque lifetime. It validates every
component before creating any directory, retains the root and full parent chain,
and opens children relative to retained handles. Linux uses `mkdirat` and a
no-follow directory open; Windows uses relative `NtCreateFile` with
`FILE_OPEN_IF` and rejects reparse points. A file collision fails. Directory
identities and each parent/name binding are rechecked; sibling metadata changes
are allowed. The read-only observer keeps its stricter root metadata rule.

Created directories persist after failure and close. POSIX creation and the
first retained open are separate operations, so an ordinary replacement in that
interval may become the baseline. This does not establish ownership of a newly
created directory. Once retained, Windows blocks replacement and Linux checks
reject it. A transplanted original leaf does not excuse a replaced ancestor.

The dedicated transaction opener attaches the preparation before lock
acquisition and source capture. Publication boundaries check the complete chain,
including equal-output publication. The transaction borrows the preparation;
close the transaction first. Binding to an already opened transaction cannot
retroactively protect its opening.

## Windows file boundary

Resolve ordinary names once with `GetFullPathNameW` before adding an extended
prefix. Use the shared UTF-16 helper for normalized absolute drive and UNC paths.
Preserve explicit device names and keep command text, mode strings and
environment text separate. The extended result and its terminator are bounded
by 32,767 units. The codec checks capacity and malformed scalar sequences.

Native adapters enable this file handling. A checked long-path plan explicitly
selects `CUPID_WINDOWS_LONG_PATHS` and the matching imports. Ordinary tools add
one `GetFullPathNameW` thunk; publication and build roles already have it.
Historical plans and installed seed validation retain their exact profiles.
Explicit long-profile source capture freezes all 77 inputs and retains its
selection through live and frozen revalidation. Missing or changed resolver
shims fail; invalid mode selections fail before directory creation. Full
bootstrap CLI selection, manifest carriage, paired proofs and seed promotion
must accept the new plan before production can use it.

Long file names do not establish long process working-directory support. A
direct explicit-application probe on this Windows host rejects a 361-character
working directory with error 267, with both ordinary and extended spellings.
Long repository arguments and executable names work from a shorter process
directory. The user coordinator must retain the wrapper's launch behavior and
check that boundary during integration.

## Earlier prerequisite evidence

Four retained-parent caller configurations pass 49 methods each. Lexical path
and user ELF caller suites pass seven methods per host. Native and checked
CupidC pass five real long-path cases and reproduce their short-path object
bytes. Checked adapter failure cases pass on both hosts. The bootstrap log
records skips, failures, input hashes and reproduction logs.

At this earlier prerequisite checkpoint, the retained-parent APIs did not
implement the closed compiler bundle or separate user-link transaction, and
the recipe handoff had not been accepted. Preserve
the existing user profile, syscall ABI check, checked cohort, candidate validation,
input rechecks, recovery behavior and all three real programs during integration.

Windows incremental OS acceptance also passes artifact, user-program and private
four-CPU disassembly/shell/SMP checks. The updated manual produces a
9,572,436-byte raw kernel. The bootstrap log records the retained-object input
proof, fresh Doom transactions, exact-size calibration and independent reread.

The follow-up checked-compiler probe rejects inherited long working directories
with error 87. Short explicit child directories work from both parent directories
and produce identical objects. Keep the project root as the wrapper launch
directory and pass the long private source root through `--root`.

Source-head `compile-user` now connects the retained-parent API to the existing
closed compiler transaction. Its two-record bundle contains the selected
example and `user/cupid.h`. Both host caller suites and the shared staged helper
pass; the bootstrap source record retains exact evidence and recovery limits.
At that source checkpoint, the normal recipes still awaited paired bootstrap and handoff.

## Accepted user compiler handoff, 2026-10-01

The paired `78e71bd6` seeds carry the closed two-record compiler transaction,
and normal Make invokes it for all three user examples. The CLI manifest is
repository-relative; the seven seed prerequisites retain their paths from
the user Make directory. The syscall ABI check remains order-only. Both hosts
pass default, nested, lexical and accented output directories, wrapper-oracle
byte comparisons, equal-object timestamps and separate private four-CPU boots.
Complete paired seed, publication and final OS evidence is linked from the
bootstrap handoff and proof records. Earlier sections retain the original
prerequisite implementation state. User linking remains separate.
