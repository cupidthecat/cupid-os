# ADR 0433: Stage FAT16 files through bounded sector I/O

The remaining disk publisher stages files into a persistent 200 MiB image.
Give the native FAT16 writer caller-owned sector and payload readers instead
of extending the whole-file store to retain several complete image buffers.
The writer owns filesystem geometry, directory entries, cluster chains and
padding; the publisher owns captured sources, flushing, independent validation
and the lifetime of the private candidate.

The interface accepts padded 8.3 guest components. Host path resolution remains
caller work; a separate module projects UTF-8 guest names. It retains the current writer's
root and first-cluster directory capacity, lowest-free allocation order, empty
file convention, parent creation and dot entries. Every FAT copy participates
in reads and writes. Old chains are checked for invalid entries and cycles
before any entry is freed. Directory and volume-label replacements fail.

A sector callback may fail after changing bytes. Any failed stage operation
that attempted a write poisons its handle. The caller must discard that private
candidate. Adding per-operation rollback would require its own captured disk
observations; the existing guarded publisher already provides the appropriate
outer publication transaction.

The hosted runtime also gains `r+`, `w+` and `a+`, with both binary spellings.
These modes give the test store real seekable read/write access. Linux uses
`O_RDWR`; Windows requests both read and write access and retains its existing
append-at-write behavior. The exact native mode parser and real Cupid-built
runtime callers verify file preservation and error behavior separately.

Guest-name projection captures the actual Unicode 15 and 16 transforms instead
of relying on the host's current library version. The request selects its
profile explicitly. Uppercase expansion, filtering and truncation occur in the
same order as the existing writer, including rejection only for non-ASCII
characters retained in the final name. Count queries and complete validation
keep allocation and partial-output handling out of callers. The native disk
publisher still needs to capture that profile with the stage request.

Private contracts and byte parity pass on both hosts. Producer-plan carriage,
source capture, the guarded publisher, installed seeds, normal Make ownership
and final OS acceptance remain open. No production disk recipe changes here.
