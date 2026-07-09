# Host dependency inventory

The deterministic active-source audit records both supported build roots: root `all` and `user:all`. `audits/active-build.json` owns current transform/source counts and `baselines/windows-amd64.json` records the passing Windows oracle tools and all 427 artifacts. The manifest covers all 420 final-link objects; a Linux GCC/binutils capture remains pending.

| Dependency | Current role | Current requirement | Fixed-point disposition |
| --- | --- | --- | --- |
| GCC with i386/multilib support | Compiles the root C graph on Linux; `user/Makefile` hard-codes GCC on every host | Required on Linux and for the separate user build on Windows | Remove from the normal and user builds; retain only as an optional oracle/bootstrap escape hatch |
| Clang with i386 target support | Compiles the root C graph on Windows | Required on Windows | Remove from the normal build; retain only as an optional oracle/bootstrap escape hatch |
| NASM | Produces `boot/boot.bin`, `kernel/cpu/isr.o`, `kernel/core/context_switch.o`, and `kernel/smp_trampoline.bin` | Required on both hosts | Replace with host-runnable CupidASM |
| GNU `ld` / LLVM `ld.lld` | Performs the two-pass kernel link according to `link.ld`; `user/Makefile` hard-codes GNU `ld` for three executables | Required, selected by host for the kernel and fixed to GNU `ld` for user programs | Replace with CupidLD while preserving the used linker-script behavior |
| GNU `objcopy` / `llvm-objcopy` | Wraps source/assets as ELF32 objects and converts the linked kernel ELF to a raw binary; Python's JPEG path also invokes it | Required, selected by host | Replace with CupidObj or equivalent shared object-library operations |
| GNU `nm` / `llvm-nm` | Supplies sorted symbols to the generated kernel-symbol blob between link passes | Required, selected by host | Replace with CupidDis/CupidObj symbol inspection |
| GNU Make | Declares both build graphs and invokes tools | Required | May remain as host orchestration; it must invoke Cupid code-producing tools on the normal path |
| Python 3 | Generates embedded-source/symbol tables; creates, stages, and cleans images; builds fixtures; transforms JPEG data | Required | May remain for orchestration and image packaging; code/object/link behavior must move behind Cupid tools |
| Git | Enumerates the tracked audit universe and creates detached baseline worktrees | Required for development/audit workflows, not image production | Retain as source-control orchestration, never as a code-producing dependency |
| `link.ld` and host GNU-linker semantics | Defines kernel memory and section layout | Required input and behavioral dependency | Keep the script or a documented compatible form; implement only its used semantics in CupidLD |
| `jpegtran`, `djpeg`/`cjpeg`, or FFmpeg | Optional JPEG normalization selected by `tools/hostbuild.py`; availability changes embedded bytes | At least one converter is preferred; raw-copy fallback exists | Keep as optional asset preprocessing or replace with a deterministic checked policy; fingerprint the selected path |
| `mkisofs`, `genisoimage`, or `xorrisofs` | Builds the test ISO for explicit ISO targets | Required only when regenerating the ISO fixture | Retain as test-fixture tooling or replace with a deterministic Python implementation |
| Bash, curl, OpenSSL, xxd, and Unix text tools | Manual CA-bundle refresh and legacy/oracle helper scripts | Not required by root `all`; required for those maintenance paths | Keep only documented maintenance dependencies; Python/Cupid paths own normal-build behavior |
| Python `pexpect` and `scapy` | Drives and validates explicit network integration tests | Required only for network test targets | Retain as test dependencies |
| QEMU `qemu-system-i386` | Boots emulator smoke and integration tests | Required for automated emulator verification, not image production | Retain as a test dependency; real-hardware tests remain complementary |
| Host shell/platform utilities | Launch Make, Python, and tests; `user/Makefile` assumes POSIX `mkdir -p` and `rm -rf` | Required operational environment; the user build is not natively Windows-safe | Keep only non-code-producing orchestration requirements and make them native on Windows and Linux |

## Resolved output ownership

Counts are output transforms in the checked audit, not textual recipe occurrences. Composite Python transforms list the code-producing utility they invoke as a second owner.

| Tool hand-off | Reachable outputs | Required external behavior |
| --- | ---: | --- |
| Host C compiler | 241 | 238 root objects plus three user objects; freestanding i386 generation, cdecl/ILP32 layout, inline assembly, SSE/SSE2, strict/optimized/Doom flag cohorts |
| NASM | 4 | Two flat binaries and two ELF32 `ET_REL` objects; active directives/expressions, exact 16/32-bit layout, instruction/addressing parity, symbols, and relocations |
| Host linker | 5 | Two kernel links plus three fixed-address user executables; `R_386_32`/`R_386_PC32`, weak/strong symbols, layout/alignment, and the used `link.ld` subset |
| Host object-copy utility | 181 owned transforms | 179 binary-to-ELF wrappers, SMP object transformation, and final ELF-to-raw conversion; the JPEG transform invokes objcopy twice internally |
| Host symbol reader | 1 composite transform | Stable numeric symbols used by `_symbols_from_nm` and panic backtraces |
| Python | 6 root transforms | Four generated C outputs, JPEG conversion/wrapping, and disk-image construction; symbol generation also uses Python |
| Host shell | 1 user transform | Creates `user/build` with the POSIX-only `mkdir -p` recipe |

`tools/hostbuild.py::_symbols_from_nm`, `_objcopy_binary`, and `embed_jpeg` are the subprocess seams behind the composite transforms. `tools/mksyms.sh` and `tools/embed_jpeg_baseline.sh` are tracked legacy/oracle duplicates outside the normal Make path.

The tracked `link.ld` is itself a compatibility contract. It uses `ENTRY`, `SECTIONS`, location-counter assignment, input-section wildcards, `ALIGN`, symbol definitions, `COMMON`, and `ASSERT`. It is referenced in linker flags but is not currently a declared prerequisite of either kernel ELF target.

## Not host compilation

- The 104 active `bin/*.cc` roots and 22 `bin/browser/*.cc` fragments are wrapped as bytes and installed in the OS filesystem. CupidC compiles them on demand inside Cupid OS.
- The 22 `demos/*.asm` files are likewise embedded as source and assembled by CupidASM on demand.
- Repository headers and compatibility code replace the host libc/header environment for root compilation (`-nostdlib -nostdinc -ffreestanding`). The compiler executables remain external.
- Optional WAD discovery and test fixtures affect packaged/runtime content, not compiler ownership.

## Removal gate

A code-producing host dependency leaves the normal build only after the Cupid replacement has positive and negative tests, matches required object/ABI/layout behavior, builds its assigned active-source cohort, and passes the relevant OS boot or runtime smoke. The legacy host path remains available as an oracle until fixed-point bootstrap and behavior gates are reliable.
