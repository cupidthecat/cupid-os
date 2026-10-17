# ADR 0184: Transfer Doom to CupidC

## Status

Accepted on 2026-07-29.

## Context

ADR 0183 promoted a checked CupidC seed that emits all 83 Doom and platform
translation units. The normal Make graph still sent those files to GCC or
Clang. Compiler capability alone had not removed the production dependency.

The cohort needs two fixed compiler profiles. `DOOM_COMPAT_I386` covers
`dglibc`, the libc stubs, and the platform shim. `DOOM_TREE_I386` covers the
sound adapter and 79 vendored Doom files. The second profile also forces
`kernel/doom/dglibc_compat.h`.

The existing strict kernel wrapper freezes a reviewed per-source header
closure. Doom has a different input shape: 83 sources share a large recursive
header search space, and the Make pattern rule discovers 79 of them. A
production transfer needed to detect source removal, header additions, header
removal, byte changes, links, and changes made during a compile.

## Decision

Rename all 83 owned sources from `.c` to `.cc`. The 79 files under
`kernel/doom/src/` keep their exact bytes. Four Cupid platform sources receive
comment-only wording updates; their C tokens and behavior do not change.
`kernel/doom/src/PROVENANCE.md` continues to describe the vendored origin.

Route the three compatibility roots through the checked wrapper's
`doom-compat` profile. Route `kernel/doom/i_sound_cupidos.cc` and the 79
vendored roots through its `doom-tree` profile. Keep strict C and ordinary GNU
mode unchanged.

Give each profile an exact source allowlist. The wrapper accepts three
compatibility sources and 80 tree sources. A source from the other profile, a
missing source, or an unlisted source fails before seed execution.
The wrapper recursively scans visible `.c` and `.cc` files beneath
`kernel/doom`, then compares that census with the union of both allowlists
before and after each compile. This also rejects a return to the `.c` suffix.

Freeze the complete recursive `.h` and `.inc` space visible through the 20
profile include roots. The current union contains 289 files. The wrapper
rejects a linked source, header, include directory, nested directory, or NTFS
junction. It captures the selected source and full header space in one private
tree, runs the verified seed there, validates the i386 `ET_REL` result,
rechecks the live membership and bytes, and publishes the object only after
all checks pass.

Add `build/bootstrap/doom-cupidc-inputs.json` as an always-checked Make
prerequisite. Its schema records the exact three-source and 80-source
memberships, both profile header memberships, and hashes for the 289 header
and include files. The writer leaves the existing file and timestamp alone
when its content is unchanged. This keeps ordinary incremental builds while
making a removed wildcard member or a new header invalidate the graph.

Keep the checked seed, compiler profiles, source behavior, Doom ABI, and link
order unchanged. The production build now consumes the same object boundary
that passed the compiler frontier.

Permit signed addends on validated `R_386_32` relocations. They select a
static subobject without changing the named symbol. Keep the existing
`R_386_PC32` rule: every direct-call addend must be -4.

## Evidence

The source tree contains 83 Doom `.cc` files and no Doom `.c` file.
`TempleOS/` has no changed path. The compatibility sources and checked-seed
objects are:

| Source | Source bytes | Source SHA-256 | Object bytes | Object SHA-256 |
| --- | ---: | --- | ---: | --- |
| `kernel/doom/dglibc.cc` | 22,632 | `00229885ddcd06c12e476cc47cc24a914053d49db9c690c8c8fea7c880b6aa9c` | 27,992 | `54ce387c7eae45d9f4ae379afdaa11092d2dd021d4e9ca7696be5da2ff5d3dcd` |
| `kernel/doom/doom_libc_stubs.cc` | 8,099 | `808580d6c35388304fa4a07b7c5e0e91ad4687e1a189c3959482f51e17a0ecf8` | 14,352 | `8f667113c54fa0b0d27ce83d134242065ba5b9258324a809e11e72229752ff3b` |
| `kernel/doom/doomgeneric_cupidos.cc` | 13,521 | `8511fd4035db73fde8147a39a92ff65f50e8097ab6f27d4ca517b9883ff15a3e` | 10,232 | `5274b91dfa7bac56cd83ff0f8096eb5a06fef5e61f91ebb3b80efacc8ad2a9cb` |

The dglibc object has a new hash because the source identity changed from
`.c` to `.cc`; the generated code and 27,992-byte object size remain fixed.
The libc-stub and platform objects retain their earlier bytes.

The 68,850-byte profile-input manifest has SHA-256
`259d7994ba929d6740528eba117bf9586c713a35e9d3edd0b4fae8b82219d87c`.
It records 289 hashed header inputs, 289 headers for each profile, three
compatibility sources, and 80 tree sources. A second unchanged invocation
keeps its timestamp. Tests change and restore one header's bytes, add and
remove a header, rename an approved source, and verify that the manifest or
normal Make build changes or fails as required.

The 21 production-wrapper tests pass in 26.273 seconds. They cover the exact
source sets and compiler vectors, audited profile agreement, cross-profile
rejection, private snapshots, prepublication drift, header membership,
source removal and addition during a compile, stable manifest timestamps,
legacy `.c` rejection, incomplete include space, symlinks, junctions, object
preservation, and dry-run exclusion of host tools.

The unchanged `g_game.cc` source is 53,065 bytes with SHA-256
`a1a90ae61150e534c3e072b57759a174e2bd01676bd738af192b8668d3f1bd8c`.
Its checked-seed object is 51,492 bytes with SHA-256
`c9da48e696eb521441e8bee0a2b69bfdd691db57b7fbbda42450d208e78d9034`.
The `.data` relocations at offsets `0x40` and `0x44` name `mousearray` and
`joyarray`; both carry addend 4 for the existing `&array[1]` initializers.
The validator's negative checks still reject a PC-relative addend other than
-4.

The promoted seed compiles each renamed compatibility source twice under the
exact audited profile. That focused test passes in 21.531 seconds. The
complete compatibility frontier also compares objects from host-built and
Cupid-built current compiler drivers. It passes in 39.475 seconds. The
80-source tree frontier passes in 24.865 seconds.

The full frontend module passes all 93 tests in 12.688 seconds. The focused
production, freestanding, and USB set passes all 28 tests in 25.959 seconds.
The complete build-graph module passes all 62 tests in 598.261 seconds. It
includes the audit mutation and checked-manifest failures. The standalone
header sweep reports `header-sweep: ok 156 2`; its two known boundaries remain
`kernel/core/scheduler.h:16:37` with `0x0b000007` and
`kernel/cpu/simd_intrin.h:28:1` with `0x0b000003`.

The complete object module passes all 107 tests in 1,131.760 seconds. It rebuilds
the hosted tools, repeats the five-tool fixed point, compiles all 80 Doom-tree
roots, compares the three compatibility objects across host-built and
Cupid-built current compiler drivers, and checks their renamed object locks.

The regenerated graph contains 716 active sources, 252 feature IDs, 505
transforms, and 25 accounted unreachable files. Its records are:

| Record | Bytes | SHA-256 |
| --- | ---: | --- |
| `docs/bootstrap/ACTIVE-SOURCE-AUDIT.md` | 12,129 | `56ea4711ed89066c47d97ea1751b0a68c5d29b1ae405347ef5fef038bc5f82a8` |
| `docs/bootstrap/audits/active-build.json` | 2,503,595 | `ed3d7b043b14b27bcd4f9c3918f4d763351844e9440961a9ef0242c3068ed40c` |
| `toolchain/tests/cupidc_pp_active_cases.inc` | 39,124 | `5468af617b18fbc3d62207053cab938899a99df86048a5959f0b92083776eb3d` |

The root image graph has 442 transforms. CupidC owns 242, CupidASM owns four,
CupidDis owns one, CupidLD owns two, and CupidObj owns 182. Host C owns none.
Across the root, user, and hosted Toolchain builds, CupidC owns 245 transforms
and the host compiler owns 52 hosted Toolchain transforms. Python participates
in 261 transforms.

The preprocessor inventory covers 685 source files and 2,390 include
occurrences: 2,158 quoted and 232 angle forms. It drives 379 checked profile
runs plus four generated kernel translations. The profile counts remain 155
kernel, three Doom compatibility, 80 Doom tree, three user, 105 Cupid
programs, twelve hosted Toolchain, one hosted kernel bridge, 19 strict hosted
i386 Linux, and one GNU hosted i386 Linux runtime.

The first current frontend run exposed four stale feature-count locks. The
checked graph already recorded 21,076 returns, 3,892 `for` statements, 34,961
`if` statements, and 4,445 `else` clauses, while the tests named older counts.
The `if` mismatch stopped that test before its stale `else` assertion ran.
Updating those four exact locks brings the test back into agreement with the
generated inventory; no compiler behavior changed.

The final `make -j8 all` run exits successfully and produces:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 8,575,688 | `968ffb652b6065bbef005b45b2ac90343367cbc9ab3ac34fd081a92f1f592cfc` |
| `kernel/cpu/ksyms_data.cc` | 364,195 | `fe08622b91f471492d8320fb82be1aae2e1b5a98666ae78604b6917cef0c14a2` |
| `kernel/cpu/ksyms_data.o` | 110,272 | `1022442e1d69544092f4ad0d8ad4ca8c85fd824eb83dd6677528d3796bb98115` |
| `kernel/kernel.elf` | 8,682,184 | `4da26719fe5a88bd7ddef5a434ba99c8d1c62af30c7d9d82182b41c94cce9591` |
| `kernel/kernel.bin` | 8,482,788 | `28e8630956a79803dd4ee29e67fdbc049fbb848b188daa16f1a0b328b9525ed4` |
| clean `cupidos.img` | 209,715,200 | `2d0363b58a8319cb858c14e040c53334702dd72dc9856849764e4e0192a3ae29` |

CupidDis places `_loaded_end` at `0x00916FE4`, `_bss_start` at
`0x00917000`, and `_kernel_end` at `0x00D3BD5C`. The image bytes at LBA 5
match the raw kernel. Its 16,568 sectors leave 3,907 sectors before FAT16 at
LBA 20480. The raw file keeps 2,000,412 bytes below its cap, and allocated
kernel memory keeps 1,852,068 bytes below `0x00F00000`.

CupidDis supplies 4,560 unique text-symbol addresses to the generated source.
The logical blob is 109,857 bytes; its word-packed initializer adds three
zero bytes after that recorded length.

The private four-CPU runtime evidence is:

| NIC and gate | Log bytes | Log SHA-256 | Result |
| --- | ---: | --- | --- |
| e1000 complete frontier | 83,300 | `971a7b40621e35920304c8cbfd3140f668b0dff16f259b47444ed66b9caa63f7` | 51,044 changed pixels; 7,477,683 AC97 frames and 75,901 PC-speaker frames; SMP, network, command, and USB replug contracts pass |
| RTL8139 complete frontier | 77,125 | `a6659d20d6b11a4828c2e684b9b0a36d699fac6ee6d2585a61212f9534ac96cd` | 73,682 changed pixels; 7,435,844 AC97 frames and 79,127 PC-speaker frames; SMP, network, command, and USB replug contracts pass |
| e1000 Doom recovery | 139,396 | `b1e5a3bdb3d5fdae4d21605e01327571ff99798b5f4794b82ac7d6734fc69504` | no-WAD guidance, missing-IWAD error, return to shell, then CupidC-built `ls` |
| RTL8139 Doom recovery | 128,902 | `2256539f8708844ba9142a1b7ef23ab2ada166b44c4fa8154e855564122ed623` | no-WAD guidance, missing-IWAD error, return to shell, then CupidC-built `ls` |

Build the image and partitioned USB fixture first:

```sh
make all test_usb_partitioned.img
```

The frontier command is:

```sh
python tools/gui_terminal_smoke.py --image cupidos.img --nic e1000 \
  --smp 4 --cpu max --verify-smp-runtime --verify-frontier-runtime \
  --private-image --timeout 300 \
  --log tests/doom-production-frontier-e1000.log
```

The recovery command is:

```sh
python tools/gui_terminal_smoke.py --image cupidos.img --nic e1000 \
  --smp 4 --cpu max --verify-smp-runtime --private-image --timeout 300 \
  --setup-command "doom" \
  --setup-success-pattern "doom: no WAD found in /disk/wads/ or /home/doom/\..*?try: doom -iwad /path/to/your\.wad" \
  --setup-command "doom -iwad /disk/missing.wad" \
  --setup-success-pattern "IWAD file '/disk/missing\.wad' not found!.*?\[doom\] returned to shell" \
  --command "ls" \
  --success-pattern "\[cupidc\] JIT compile: /bin/ls\.cc.*?\[cupidc\] JIT execution complete" \
  --log tests/doom-production-runtime-e1000.log
```

Repeat each command with `--nic rtl8139` and the corresponding log name.
All four runs use the final image with SHA-256
`2d0363b58a8319cb858c14e040c53334702dd72dc9856849764e4e0192a3ae29`.
None of their logs contains a panic or corruption marker. The repository
contains no `.wad` file, so this evidence stops at the asset-free runtime
boundary.

## Rejected alternatives

Keeping the `.c` suffix was rejected because checked-in roots move to `.cc`
when CupidC owns their normal objects.

Rewriting or trimming Doom was rejected because the checked seed represents
the existing source and ABI. The transfer changes tool ownership, not game
behavior.

Using the Make wildcard as the source inventory was rejected. Removing one
file would silently shrink the object cohort. The manifest depends on the
exact wrapper allowlists instead.

Listing only current headers as ordinary prerequisites was rejected. A newly
added header under an include root would not be a prerequisite yet. The
always-run content check detects membership changes and keeps its timestamp
when nothing changed.

Checking only `Path.is_symlink()` was rejected because a Windows junction can
redirect an include subtree without reporting itself as a symbolic link. The
final path policy rejects both forms at every traversed component.

Publishing an object before the second membership and byte check was rejected
because a live edit could otherwise enter or leave the compile without
belonging to the recorded transform.

Restricting absolute relocations to addend zero was rejected because C permits
static pointer initializers that name a subobject. The existing `g_game.cc`
source needs that ordinary form.

Launching both frontier gates and both recovery gates together was rejected
after the four guests exhausted a 360-second outer allowance. The two recovery
logs reached their markers, and the frontier harnesses continued after their
parent shells exited, but their exit states were unavailable. None of those
four runs is counted. Repeating the frontier pair and then the recovery pair
preserved all four successful exit codes.

## Consequences

Checked-seed CupidC owns all 238 checked-in normal C roots: the 155 strict
kernel and driver roots plus the 83 Doom and platform roots. The generated
kernel symbol translation brings the normal compiler total to 239, and the
three generated installation tables bring the root graph to 242 CupidC
transforms.

The normal image no longer needs GCC or Clang for an OS object. The hosted
Toolchain contracts and native development commands still use a host C
compiler. Windows still uses WSL to run the checked i386 Linux seed for root
objects. Python remains the checked wrapper and image orchestration layer.

No-WAD guidance, explicit missing-IWAD recovery, shell survival, and the full
frontier pass on both supported NICs. Gameplay, game input, game audio, and
save behavior still require a staged IWAD runtime gate. The repository and
existing images contain no WAD, so the asset-free checks do not close the full
Doom behavior issue.

`TempleOS/` remains untouched reference material.
