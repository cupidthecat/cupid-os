# ADR 0425: Scan FAT16 free clusters by sector

## Status

Implemented and qualified on 2026-10-05. Native and Cupid-built contracts pass
on both hosts; the fresh kernel/image passes strict four-CPU boot. The IWAD
observation reaches game tics but does not pass the completed-frame boundary.

## Context

HomeFS replaces its complete `HOMEFS.SYS` container through the durable FAT
transaction in ADR 0211. Doom initialization creates directories through this
path. Six debugger samples of all four CPUs, taken over 180 seconds after the
Doom command, repeatedly find `DG_Init` inside a HomeFS flush and FAT allocation.
The other stacks include timer writeback waiting for the big kernel lock.
Allocation still advances, so these samples do not establish a permanent
deadlock or the complete cause of the missing frame.

The allocator starts at cluster 2 for each request and reads the containing
sector separately for every entry. A controlled 317-cluster replacement with
7,049 occupied entries makes 2,284,936 sector-read calls. Most calls fetch a
sector whose neighboring entries were just examined.

## Decision

Keep the lowest-free-cluster policy, but read each FAT sector once per scan and
inspect its little-endian entries in memory. Stop at the volume's data-cluster
limit, exclude reserved entries 0 and 1, and never allocate free tail padding.
The existing marking helper still writes the end-of-chain value to every FAT
copy. There is no new allocation hint or shared mutable cache.

Reject invalid sector, cluster, FAT-count, root-directory, capacity and address
geometry before sector access. The scan requires the driver's 512-byte sector
buffer and a nonzero power-of-two cluster size. A read error ends the allocation
without marking a later entry. Marking errors retain the existing failure
result and copy-write behavior; this change does not add rollback to that helper.

The HomeFS reservation, directory publication, sync and chain-release ordering
remain the transaction in ADR 0211. The active FAT source stays `.cc` and
compiles through CupidC.

## Evidence

The controlled replacement now makes 9,708 sector-read calls and the same 634
copy writes. Both versions select clusters 7,049 through 7,365 in the same
order and produce identical 131,072-byte FAT tables, SHA-256
`2f2770c52b48ae84010cfa6323294737cd3c78842822f0be79f0eec4e26bd9c1`.
This measures allocator work; it is not a whole-game timing result.

Thirteen methods cover the read bound, lowest holes, reserved entries, sector
boundaries, the final data cluster, tail padding, full volume, failed scan and
mark reads, first- and second-copy write failures, twelve invalid geometries, occupied
bad/end-of-chain entries, and reuse of a freed earlier cluster. The harness
extracts the exact four active allocator/accessor bodies with their real types
and uses controlled sector callbacks. It does not reimplement the allocator.

All thirteen methods pass with native callers on Windows and Linux. They also
pass with checked CupidC/CupidLD callers on each host. The original allocator
fails the read bound and scan-error cases and faults on invalid geometry.

The first Windows native harness exposed the host compiler's incompatible
`size_t` spelling in the kernel header. A private test-only alias permits the
unchanged kernel types to compile; the production header is untouched. The
first checked controller treated a void helper result as a process receipt.
The corrected controller retains real runner results. Its Linux link initially
omitted `-m elf_i386`; a separate retry fixes only that external invocation.
All failed logs, sources and process receipts remain in the private proof.

Normal Make compiles the complete changed FAT source through the installed
Cupid tools on both hosts, with host compiler, assembler and linker commands
forbidden. Both produce the same 65,672-byte object, SHA-256
`3642859a85f0077924b055779d71a13aac746dae52c50f33941061c43fb9f547`.
The current frontier lock records that measured object.

The complete checked-seed frontier also passes on a byte-verified Linux source
control in 1,897.026 seconds. It compiles all 156 sources twice, with no
boundaries. Independent checking rereads every object, validates i386 ELF and
matches both copies to the qualified OS cohort. The objects total 4,421,572
bytes; the 469-input snapshot has SHA-256
`f850bf807a896c1c49e12400f8d55fe2dd6c5c2287f2349482bb520e066b4015`.
Earlier compiler changes had left ten other object locks and the snapshot
expectations stale. Those locks are refreshed only after this complete
comparison. All nineteen post-build assertions in the real frontier test pass
against the retained actual outputs. This assertion replay does not launch
another compiler or count as a separate full unittest invocation.

The 37-method kernel compilation and Doom storage regression passes on both
hosts. Its first run finds an older Make fixture that omits the already active
production seed-release prerequisite. Adding that prerequisite to the fixture
makes the complete rerun pass. The test caller is recorded outside the supported
Make roots in the source-suffix ownership policy; it does not change production
build ownership.

The final three-root canonical audit and its check both pass. It records 770
active sources, 44 sources outside those roots, 255 feature cases and the same
452 transforms: 447 CupidBuild and five Python actions.

The incremental OS control rereads all 1,589 accepted source files and changes
only the FAT source and 156,352-byte manual. Among 429 objects, only FAT, the
manual wrapper and generated kernel symbols change. All sixteen artifact
checks and a strict private four-CPU `max`/e1000 boot with completed `ls` pass.
Independent verification checks the manual in all three kernel outputs and
preserves the complete FAT suffix. The raw kernel is 9,590,484 bytes; final and
pass-one ELFs are 9,818,556 and 9,687,484 bytes. Only those three measured policy
rows change. The 200 MiB image has SHA-256
`3353849bd88f6015a0750498a21e017dc276e9238f306d20c3e63bda58befbaa`.

The unchanged 180-second post-command IWAD probe now reaches graphics setup.
Its final sample reads five game tics and renderer frame count one, with an
active stack in `OPL3_Generate4Ch`. The probe still returns one: it observes no
completed draw call. This is initialization progress, not gameplay acceptance
or a controlled whole-game performance comparison.

The separate full-audio `demo1` replay also fails its existing 1,200-second
completion deadline, returning one after 1,290.333 seconds including startup.
Its log reaches graphics setup and later reports an EHCI async-schedule
quiescence error. Independent rereading confirms unchanged source image,
staged IWAD fixture and kernel. The log does not establish the error's cause
or a completed demo; all original runtime requirements remain open.

## Consequences

Repeated lowest-free scans perform one read per examined sector instead of one
per examined entry. The new image passes strict four-CPU boot. Gameplay,
timedemo, audio, save/load and reboot persistence remain separate open work.
The isolated proposal changes no installed seed or normal build ownership.
`TempleOS/` remains read-only reference material and is excluded from the proof.
