# Run CupidLD as a guest worker

- Status: Accepted for the opt-in build
- Date: 2026-11-17

Link the existing CupidLD core as a worker in the compiler arena. CupidC
and CupidLD share the syscall-backed allocation, VFS and publication
callbacks. The compiler keeps its logical ISO paths. The linker reads
object paths directly from the guest filesystem.

The linker reads a line-oriented job with an output path, text base,
entry symbol and ordered object paths. The text base selects the ordinary
external or compiler arena. CupidLD applies the existing ELF32 layout and
relocation checks. The adapter writes the completed ELF to a pending file
and renames it over the output in /home.

The guest entry supports fixed-text ELF32 links. Script-driven kernel
links and the complete guest build schedule remain separate steps.
