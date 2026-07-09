# Host dependency inventory

This source inspection records every external code-producing invocation found in the tracked build definitions at baseline commit `0333208`. `baselines/windows-amd64.json` now captures resolved Windows commands, versions, executable hashes, checks, outputs, and artifact hashes; an equivalent Linux capture remains pending. The active-source audit must still prove whether sources without active Make rules are reachable through another supported path.

| Dependency | Current role | Current requirement | Fixed-point disposition |
| --- | --- | --- | --- |
| GCC with i386/multilib support | Compiles all freestanding kernel, driver, library, Doom, and Cupid Toolchain C sources on Linux | Required on Linux | Remove from the normal build; retain only as an optional oracle/bootstrap escape hatch |
| Clang with i386 target support | Compiles the same C cohorts on Windows | Required on Windows | Remove from the normal build; retain only as an optional oracle/bootstrap escape hatch |
| NASM | Produces `boot/boot.bin`, `kernel/cpu/isr.o`, `kernel/core/context_switch.o`, and `kernel/smp_trampoline.bin` | Required on both hosts | Replace with host-runnable CupidASM |
| GNU `ld` / LLVM `ld.lld` | Performs the two-pass kernel link according to `link.ld` | Required, selected by host | Replace with CupidLD while preserving the used linker-script behavior |
| GNU `objcopy` / `llvm-objcopy` | Wraps source/assets as ELF32 objects and converts the linked kernel ELF to a raw binary; Python's JPEG path also invokes it | Required, selected by host | Replace with CupidObj or equivalent shared object-library operations |
| GNU `nm` / `llvm-nm` | Supplies sorted symbols to the generated kernel-symbol blob between link passes | Required, selected by host | Replace with CupidDis/CupidObj symbol inspection |
| GNU Make | Declares the build graph and invokes tools | Required | May remain as host orchestration; it must invoke Cupid code-producing tools on the normal path |
| Python 3 | Generates embedded-source tables and CSS tables; creates, stages, and cleans images; builds fixtures; transforms symbol and JPEG data | Required | May remain for orchestration and image packaging; code/object/link behavior must move behind Cupid tools |
| `link.ld` and host GNU-linker semantics | Defines the kernel memory and section layout | Required input/behavioral dependency | Keep the script or a documented compatible form; implement only its used semantics in CupidLD |
| QEMU `qemu-system-i386` | Boots emulator smoke and integration tests | Required for automated emulator verification, not for producing the image | Retain as a test dependency; real-hardware tests remain complementary |
| Host shell/platform utilities | Launch Make, Python, and tests | Required operational environment | Keep only non-code-producing orchestration requirements and make them work natively on Windows and Linux |

## Tracked invocation sites

Counts below are static occurrences in the baseline `Makefile`, not dynamic process counts. A recipe can run many times for different source files.

| Tool hand-off | Tracked sites | Required external behavior |
| --- | --- | --- |
| `$(CC)` — 161 recipes | Explicit C-to-object rules from the first kernel rule through CupidC, CupidASM, CupidDis, generated tables, generated kernel symbols, and the `kernel/doom/src/%.o` pattern | Freestanding i386 code generation; cdecl-compatible calls and data layout; repository inline assembly; SSE/SSE2; strict and relaxed diagnostic flag sets; `-O2`, `-Os`, and unoptimized output |
| `$(ASM)` — 4 recipes | `boot/boot.asm` to flat binary; `kernel/cpu/isr.asm` and `kernel/core/context_switch.asm` to ELF32; `kernel/smp/smp_trampoline.S` to flat binary | NASM parsing, expressions/directives, 16/32-bit encoding, exact flat layout, ELF32 symbols/relocations, and ABI-sensitive instruction selection |
| `$(LD)` — 2 recipes | `kernel/kernel.elf.pass1` and final `kernel/kernel.elf` | ELF32 i386 symbol resolution, relocation application, section placement/alignment, weak/strong resolution, and the `link.ld` GNU-script subset |
| `$(OBJCOPY)` — 13 recipe references | SMP trampoline wrapping/renaming; binary wrapping for `.cc`, headers, CTXT, images, fonts, demo ASM, and God data; JPEG helper hand-offs; final ELF-to-raw kernel conversion | GNU binary-input symbol naming, symbol redefinition, section renaming/flags, ELF32 object construction, and raw range extraction |
| `$(NM)` — 1 recipe hand-off | Pass-1 kernel ELF to `tools/hostbuild.py mksyms` | Stable numeric symbol output understood by `_symbols_from_nm`; function/address data used by panic backtraces |

The actual subprocess seams behind the Python hand-offs are `tools/hostbuild.py::_symbols_from_nm`, `_objcopy_binary`, and `embed_jpeg`. Two tracked legacy/oracle shell scripts also encode host-tool behavior outside the normal Make path: `tools/mksyms.sh` invokes GNU `nm`, and `tools/embed_jpeg_baseline.sh` invokes GNU `objcopy`. `tests/test_hostbuild.py` mocks an `llvm-objcopy` command to test the JPEG helper rather than requiring the real binary.

The tracked `link.ld` is itself a compatibility contract. Host compiler flags in `CFLAGS`, `SIMD_CFLAGS`, `CFLAGS_DOOM`, and `CFLAGS_DOOM_TREE`, plus the linker and binary-utility command lines, are external behavior dependencies even where the executable name is configurable.

## Not host compilation

- `bin/*.cc` and `bin/browser/*.cc` are wrapped into objects as bytes and installed in the OS filesystem. They are compiled by the in-OS CupidC when invoked.
- `demos/*.asm` files are likewise embedded as source and assembled by the in-OS CupidASM when invoked.
- Repository headers and freestanding compatibility code replace the host libc/header environment for kernel compilation (`-nostdlib -nostdinc -ffreestanding`). The current compiler executables still come from the external host toolchain.
- Optional WAD discovery and test fixtures affect packaged/runtime content, not compiler ownership.

## Removal gate

A code-producing host dependency leaves the normal build only after the Cupid replacement has positive and negative tests, matches required object/ABI/layout behavior, builds its assigned active-source cohort, and passes the relevant OS boot or runtime smoke. The legacy host path remains available as an oracle until fixed-point bootstrap and behavior gates are reliable.
