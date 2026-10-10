# Owned candidate stores

The separate prototype adds a transaction-owned fixed-extent file store.
Its open, extent, read, write and finish operations retain all offset and extent
bits on i386. Ordinary and bounded transaction capacities keep their existing
limits. A range can transfer at most 65,536 bytes and cannot extend the file.
Invalid or failed operations preserve the public output and forbid publication.
Finish requires a successful file-data flush, complete digest capture and the
final precise metadata checks. Its failure clears the returned snapshot.

[ADR 0448](../adr/0448-write-candidates-through-owned-wide-stores.md) records the
lifetime, platform calls, timestamp guard and durability scope. The candidate
still needs independent format/content validation before guarded publication.
Normal disk-image ownership remains Python-owned.

## Matched callers and complete publication, 2026-10-08

The final eighteen-method small selection passes through native and matched
Cupid-built callers on both hosts. Linux's precise interposition method remains
a declared skip for the hosted Cupid caller. Checked-tool launch controls prove
that tools may run before the store opens and are rejected before launch after
open or finish. The marker executable is captured before opening the store;
rejected calls leave its marker absent and preserve the original public output.
All host store phase checks remain in the shared launch boundary.

The matched Windows Cupid caller publishes a complete 4,294,967,361-byte output
in 2,878.918 seconds. Independent rereading compares every zero and marker byte,
the complete digest, original 15-byte sentinel identity, namespace cleanup,
compiler, actual compiled inputs, artifacts and runtime control bindings.
The complete output has SHA-256
`141890df4f02a73711df13417bd65510415641a73e97ede449a746c327a83b6e`.
Receipts are `candidate-checked-large-windows5` and
`candidate-checked-large-windows5-independent`; the exported complete file
remains under `candidate-checked-large-products`.

Both final Linux cases pass in 10,763.289 seconds, including complete publication
under a 32 MiB address-space limit. They retain the original 18,000-second bound
per invocation. Independent rereading takes 40.430 seconds and compares every
zero and marker byte in both complete outputs, with the same digest as Windows.
It also checks the original sentinel, namespace cleanup, compiler, source,
artifacts and command bindings. Receipts are `candidate-checked-large-linux4`
and `candidate-checked-large-linux4-independent1`; detailed evidence is
`candidate-checked-large-linux4-independent1-products.json`.
A first matched
build mistakenly selected the old installed compiler and rejects the required
wide pointer source. The corrected builders select and verify the fully
qualified wide-pointer compiler explicitly; original failed builds remain.

All five archived Windows import profiles retain their exact old plans. Five
new prototype profiles add the required precise-time and wide-seek imports.
Windows passes 134 profile methods and Linux passes its 191-method selection
with three declared skips, including historical
plans, mixed/duplicate imports, exact counts and useful rejection controls.
These new profiles remain unapproved by the installed C reader. The
[retained disk bridge](RETAINED-DISK-IO.md) uses these private callers without
changing the normal image recipe or claiming complete native ownership.

## Development evidence and failed approaches

The first strict Windows compile rejected a const transaction passed to an
ownership check that can record interference. Its internal parameter now keeps
that mutable contract. The first diagnostic compile also rejected mismatched
DWORD format arguments; the retained diagnostic caller uses explicit formatter
types.

The first Windows runtime selection fails twelve cases. Its trace shows a
directory timestamp about one millisecond behind the retained writable handle.
The repaired metadata path queries both precise times from that handle while
checking the directory entry's name and identity. The first repair mistakenly
used the existing directory-only validation predicate for a regular file and
fails 27 cases. The corrected regular-file predicate passes the complete small
selection. Original logs remain under `C:/Users/admin/cp7/`.

Receipts are `candidate-native-small-windows1`,
`candidate-native-trace-windows2`, `candidate-native-trace-windows3`,
`candidate-native-small-windows2` and `candidate-native-small-windows3`.
The fourteen-method Windows selection also passes independent modification-time
and change-time mutations in `candidate-native-small-windows4`, and passes after
the metadata storage reduction in `candidate-native-small-windows5`.
Linux passes the original twelve-method selection in
`candidate-native-small-linux1`. The fifteen-method Linux selection passes in
`candidate-native-small-linux2`, with two declared Windows skips; it includes
the precise mutation at publication-alias creation. A later correction keeps
the finish result private until all final checks pass, with explicit failure
clearing required by that race fixture.

The current fixture also covers empty files, hash/block boundaries, full-width
capacities with small files, equal-output identity and timestamp reuse, invalid
read/write ranges, a second open, result clearing, unfinished capture, a forced
flush rejection and a write after finish. Complete publication above four GiB
and a Linux 32 MiB address-space control are defined. The later matched Windows
publication, both Linux large controls and all small matched controls pass as
recorded above. New Windows import acceptance, source
integration, full qualification and native recipe ownership remain open.
