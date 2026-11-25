# Build CupidC as a standalone guest program

- Status: Accepted for the opt-in build
- Date: 2026-11-17

Link the same nine compiler objects with one Cupid OS syscall adapter.
The executable reads source and headers from the mounted ISO at `/src`.
It gives the compiler the original logical paths, so translating
`/toolchain/ctool.cc` still finds its neighboring header.

The adapter reads two LF-separated paths from `/home/cupidc.job`. The
first names a source in the ISO. The second names a relocatable object
under `/home`. After compilation succeeds, the adapter writes a pending
file and renames it over the destination. It refuses to overwrite an
existing pending file. A failed rename removes the pending file.

The standalone ELF links at `0x01E00000`. Launching it in Cupid OS needs a
leased arena there, a scheduler-owned two-MiB stack and cleanup for worker
allocations after CPU detachment. The opt-in Linux contract links the same
objects and supplies a Cupid OS syscall table backed by a private directory.
It compares the rebuilt object and checks failed request and publication
paths.
