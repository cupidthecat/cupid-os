# Run the kernel build sequence in guest CupidBuild

The source-media plan supplied seven requests after compilation. The two
link requests each carried the complete pass-one object list. Their order,
the linker script, and the symbol table are parts of one kernel build.

Add a `kernel` operation to the source-media worker. Its request contains
the ordered pass-one object paths, one per line. CupidBuild validates the
list before assembling the bootloader. It rejects repeated paths, the
generated symbol object, and lists that leave no slot in the second link
for that object.

CupidBuild assembles the bootloader, links pass one, generates and compiles
the symbols, links the kernel, flattens it, and writes the disk prefix. The
workers parse in-memory requests using the same readers they use for files.
They publish their products through the existing VFS adapters. A failing
step stops the sequence and reports which step failed.

The editable HomeFS plan and standalone workers keep their existing job
formats. The host still selects compile profiles, reads object order from
Make, and prepares the source media.
