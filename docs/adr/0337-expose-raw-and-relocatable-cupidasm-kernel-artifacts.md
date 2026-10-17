# ADR 0337: Expose raw and relocatable CupidASM kernel artifacts

## Status

Accepted on 2026-08-24.

## Context

The in-kernel CupidASM adapter already used the shared assembler for JIT and
linked AOT programs. Its file command exposed only the linked executable. Raw
images and unlinked ELF32 objects were available from the hosted command, but
an in-OS caller could not request those artifacts without bypassing the public
kernel adapter.

This gap matters for self-hosting work. Boot code needs mixed 16-bit and
32-bit raw output with the source-derived code and data map. Object-oriented
workflows need an `ET_REL` file whose undefined symbols and relocations remain
available to a later link. Reimplementing either path in the shell would split
Cupid ASM semantics between adapters.

Raw publication also has two outputs: the image and its map. Replacing one and
then failing to write the other would leave a stale pair that CupidDis could
misinterpret.

## Decision

Add one typed artifact request and result seam to `kernel/lang/as_elf.h`. The
request selects `bin`, `elf32`, or `exec` and carries the existing mode,
origin, definition, include, symbol, and executable-layout policy. All three
formats call the shared `ctool_asm_assemble` operation. `bin` returns the raw
bytes, origin, and borrowed typed ranges. `elf32` returns the unlinked object
without requiring an entry point. `exec` assembles an object with the existing
entry candidates and passes it to the in-kernel CupidLD adapter.

Render raw metadata as the existing `cupid.raw-map.v1` format. The renderer is
transactional: it requires an empty output buffer and rewinds the buffer if
validation or growth fails.

Use the same command parser for `as` and `cupidasm`. The public forms are:

```text
as SOURCE
as -f bin --map MAP -o OUTPUT SOURCE
as -f elf32|exec -o OUTPUT SOURCE
cupidasm -f elf32|exec -o OUTPUT SOURCE
cupidasm -f bin --map MAP -o OUTPUT SOURCE
```

`as SOURCE` remains the JIT spelling. The older `as -o OUTPUT SOURCE`,
`cupidasm SOURCE -o OUTPUT`, `cupidasm -o OUTPUT SOURCE`, and source-only
`cupidasm SOURCE` forms continue to select a linked executable. Source-only
`cupidasm` still derives the output name. An explicit `-f` requires `-o`.

Publish each artifact through adjacent candidate and backup names. For raw
output, write both candidates before moving either target. If a write fails,
neither target moves. If a later replacement fails, remove any new target and
restore each previous target from its backup before cleaning the private
files. If restoration itself fails, the old bytes remain under the backup name.
A later command treats that backup as authoritative: it removes any partial
target, restores the old bytes, and only then writes a new candidate.

After both targets move, write the same pair-level commit record beside each
target before deleting old backups. The versioned record names every backup
and commit marker in that publication. A later command can therefore discover
the old transaction by reusing either the artifact path or the map path. If
backup deletion fails, both records stay beside the completed pair. The next
command reads either record, removes the exact stale backups and markers, and
keeps the new targets. Only an uncommitted backup is authoritative during
rollback. The
`.cupid-as-new`, `.cupid-as-old`, and `.cupid-as-done` suffixes are reserved
for this operation.

A marker write can report an error after creating only part of the file. The
publisher reads the file back and accepts it only when the complete record
matches the current pair. A partial or different record is removed before the
old pair is restored. If the immediate existence check fails, the command
leaves the new targets, old backups, and possible marker in place. It does not
guess whether to commit or roll back. The next command either reads a complete
record and finishes the committed cleanup or finds no marker and restores the
backups.

Recovery records accept only canonical absolute paths under the reserved
`.cupid-as-old` and `.cupid-as-done` suffixes. Embedded NULs, backslashes,
parent components, empty components, and other suffixes reject the record
before a filesystem mutation.

This is command-level rollback, not a crash-atomic two-file transaction. A
power loss between the two target replacements can expose one new file, and a
failed rollback operation can prevent restoration. Cupid OS VFS does not yet
provide a durable multi-file commit primitive.

## Evidence

The kernel adapter contract covers a mixed-mode raw source with instructions
and data, its exact image bytes, origin, typed ranges, and canonical map. An
equ-only source also keeps a zero-byte raw result and a map with no ranges. The
relocatable case keeps an undefined global symbol and two relocations across
`.text` and `.data`. Command tests cover all three formats, the compatibility
spellings, bad formats, missing or conflicting options, and recovery.

Publication tests inject failures into the first and second raw candidate
writes, the commit-marker write, and after the artifact target has moved. The
previous image and map are restored in each case, and the same store accepts a
later valid publication.

If an injected restoration also fails, the old map remains under its backup
name. The test leaves a partial map at the public target as well; the next
command removes that partial target and restores the old map before an injected
candidate-write failure.

Another test fails deletion of an old backup after the pair commits. The new
targets stay public, and the next command removes the retained backup and
marker without restoring stale bytes.

The cleanup tests also retain an old map backup, publish the same artifact with
a different map path, and then return to the first map. Another case changes
the artifact path while reusing that map. Because both members carry the same
record, neither route can treat the stale map backup as rollback state. A
separate case makes marker creation return an error and makes the immediate
existence check fail. The next command resolves the retained record and
preserves the committed pair. Partial and embedded-NUL records fail without
removing a public path. A pair whose artifact and map paths are both near
`VFS_MAX_PATH` publishes its complete four-path record. Requests whose backup
and marker stems do not match fail before mutation.

The single-file path has the same rollback and recovery check. Existing linked
object, layout, error, and executable contracts remain in the normal cohort.
The DEBUG kernel self-test now assembles a literal mixed 16/32-bit source and
checks its exact raw bytes and map through the public adapter.

The focused native kernel-adapter module passes all seven tests. The checked
seed publication reached its existing 360-second stage-two frontend timeout
before it could run this contract. The final checked-seed build, image
publication, and guest exercise are therefore deferred to the consolidated
integration lane; no guest-observed claim is attached to this decision yet.

## Rejected alternatives

Do not add a second raw assembler or object writer to the kernel. The shared
CupidASM core already owns parsing, layout, encoding, range classification,
and ELF32 serialization.

Do not make the shell infer a mixed-mode map from emitted bytes. The assembler
knows whether each source range is code16, code32, or data; byte inspection
does not recover that information reliably.

Do not publish the raw image before creating its map. A stale map can be
structurally valid while describing different bytes.

Do not change the historical AOT spellings to mean `ET_REL`. Existing scripts
expect those commands to produce an executable that `exec` can load.

## Consequences

Cupid OS can now author raw images with their typed range maps and unlinked
ELF32 objects from the in-OS shell. The linked AOT and JIT paths keep their
previous meanings. The adapter stays thin because format selection, map
rendering, and publication policy sit around the shared assembler and linker.

The in-OS publisher does not lock paths against concurrent commands, pin
directory identities, inspect artifacts with CupidDis, or provide crash-safe
multi-file replacement. The checked host production transactions retain those
stronger duties. This change moves no normal build owner, removes no host
dependency, and renames no source. `TempleOS/` remains read-only reference
material.
