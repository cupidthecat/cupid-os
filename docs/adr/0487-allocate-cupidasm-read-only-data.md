# Allocate CupidASM read-only data

- Status: Accepted
- Date: 2026-11-17

CupidASM emits `.rodata` as allocated read-only `PROGBITS` with
four-byte default alignment. Its bytes and relocations still go through
the shared ELF32 writer. CupidLD can now resolve references to those
bytes and place them in the read-only load segment.

An assembly probe used a symbolic `push` to pass a message to the
guest syscall table. The object carried the correct `R_386_32` relocation.
The missing allocation flag kept the target section out of the image,
and CupidLD rejected the link. The NASM oracle emitted the expected
section flags and alignment.

The assembler contract checks the section flags, alignment and exact
relocation target. A custom metadata section remains unallocated.
