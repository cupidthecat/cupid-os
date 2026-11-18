# Plan compiler engine loads before copying

- Status: Accepted
- Date: 2026-11-17

The Linux fixed-point harness previously checked and copied compiler ELF
segments in its platform code. A guest worker needs those rules as well.
Move the image policy beside the freestanding compiler entry. Leave mapping,
arena ownership, and scheduling to each caller.

The planner uses the shared ELF32 reader. It accepts ordered,
non-overlapping load segments inside a caller-supplied interval. The entry
must fall inside file-backed executable bytes. The only ignored program
header is empty, non-executable GNU stack metadata. The installer rejects a
short destination before writing, clears the image span, and copies the
file-backed bytes. The plan borrows its input and metadata until installation.

The engine and its source build inputs also travel together on an opt-in ISO.
Checked CupidObj keeps their repository-relative names. CupidBuild authors
and verifies the ISO in a private directory. The build replaces the
public path only after that check passes. The worker can resolve logical
source names under a single read-only mount.

The existing JIT and AOT paths still own the legacy compiler region. The
guest worker needs an exclusive arena, a scheduler-owned stack, and a way
to release the arena and job memory after a killed worker stops running.
