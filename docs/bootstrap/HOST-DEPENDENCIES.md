# Host dependency inventory

The deterministic active-source audit records three supported build roots: root `all`, `user:all`, and `toolchain:all`. `audits/active-build.json` owns the current 672-source/480-transform graph and its 431-path manifest covers all 424 final-link objects. `baselines/windows-amd64.json` records the passing `6731dd6` CupidObj-cutover Windows oracle until the committed CupidLD cutover is recaptured; a complete Linux GCC/binutils OS capture remains pending.

| Dependency | Current role | Current requirement | Fixed-point disposition |
| --- | --- | --- | --- |
| GCC with i386/multilib support | Compiles the root and user C graphs on Linux and builds the native hosted core, ELF32, x86, CupidDis, CupidASM, CupidObj, and CupidLD contracts/CLIs | Required on Linux and for the separate user/contract builds | Remove from code-producing normal and user paths after Cupid seeds exist; retain only as an optional oracle/bootstrap escape hatch |
| Clang with i386 target support | Compiles the root and user C graphs on Windows and builds the native hosted core, ELF32, x86, CupidDis, CupidASM, CupidObj, and CupidLD contracts/CLIs | Required on Windows | Remove from code-producing normal and contract paths after Cupid seeds exist; retain only as an optional oracle/bootstrap escape hatch |
| NASM | Produces `boot/boot.bin`, `kernel/cpu/isr.o`, `kernel/core/context_switch.o`, and `kernel/smp_trampoline.bin`; it is now also an optional parity oracle for hosted CupidASM tests | Required for four production transforms on both hosts | Cut over those transforms to the tested host-runnable CupidASM candidate, then retain NASM only as an optional oracle/bootstrap escape hatch |
| Host linker backend (`ld`, `ld.lld`, `lld-link`, or platform equivalent) | No direct i386 OS/user link recipe remains; CupidLD owns those five outputs. The host C compiler still invokes a native linker backend to bootstrap the Cupid contract and CLI executables, and standalone ELF linkers remain optional comparison oracles | Required transitively wherever hosted Cupid tools are rebuilt, including root `all` and `toolchain:all`; not an owner of an OS/user ELF transform | Remove from the normal bootstrap after checked Cupid-built seeds/self-hosting exist; retain standalone ELF linkers only as optional oracles/escape hatches |
| GNU `objcopy` / `llvm-objcopy` | No role in the normal build; tracked legacy/oracle helpers may still invoke it manually, and the checked `6731dd6` evidence fingerprints the then-installed oracle | Not required for root `all`, `user:all`, `toolchain:all`, or new `bootstrap-baseline` captures | Retain only as an optional comparison/maintenance utility; CupidObj owns the production transformations |
| GNU `nm` / `llvm-nm` | Supplies sorted symbols to the generated kernel-symbol blob between link passes | Required, selected by host; hosted CupidDis now supplies a checked deterministic `--nm` candidate but Make has not cut over | Replace with CupidDis typed/`--nm` symbol inspection after kernel-symbol parity and boot/backtrace proof |
| Hosted C runtime/libc | Backs the hosted adapter's allocation/whole-file/diagnostic seams plus the CupidDis, CupidASM, CupidObj, and CupidLD CLI drivers and sinks | Required by the temporary native contracts/CLIs and future hosted Cupid executables | Retain as a platform runtime seam; it must not own parsing, code generation, object, assembly, link, or inspection semantics |
| GNU Make | Declares the root, user, and toolchain-contract build graphs and invokes tools | Required; the graph uses portable ordinary/stamp targets rather than GNU Make 4.3 grouped-target syntax | May remain as host orchestration; it must invoke Cupid code-producing tools on the normal path |
| Python 3 | Generates embedded-source/symbol tables; creates, stages, and cleans images; builds fixtures; transforms JPEG data | Required | May remain for orchestration and image packaging; code/object/link behavior must move behind Cupid tools |
| Git | Enumerates the tracked audit universe and creates detached baseline worktrees | Required for development/audit workflows, not image production | Retain as source-control orchestration, never as a code-producing dependency |
| `link.ld` and its documented GNU-script subset | Defines kernel memory and section layout; CupidLD parses the exercised `ENTRY`, `SECTIONS`, location-counter, wildcard, alignment, symbol, `COMMON`, and `ASSERT` forms | Required input to both kernel link passes; host-linker interpretation is oracle-only | Keep the script as the source-owned layout contract and deepen CupidLD when the active script needs more semantics |
| `jpegtran`, `djpeg`/`cjpeg`, or FFmpeg | Optional JPEG normalization selected by `tools/hostbuild.py`; availability changes embedded bytes | At least one converter is preferred; raw-copy fallback exists | Keep as optional asset preprocessing or replace with a deterministic checked policy; fingerprint the selected path |
| `mkisofs`, `genisoimage`, or `xorrisofs` | Builds the test ISO for explicit ISO targets | Required only when regenerating the ISO fixture | Retain as test-fixture tooling or replace with a deterministic Python implementation |
| Bash, curl, OpenSSL, xxd, and Unix text tools | Manual CA-bundle refresh and legacy/oracle helper scripts | Not required by root `all`; required for those maintenance paths | Keep only documented maintenance dependencies; Python/Cupid paths own normal-build behavior |
| Python `pexpect` and `scapy` | Drives and validates explicit network integration tests | Required only for network test targets | Retain as test dependencies |
| QEMU `qemu-system-i386` | Boots emulator smoke and integration tests | Required for automated emulator verification, not image production | Retain as a test dependency; real-hardware tests remain complementary |
| Host shell/platform utilities | Launch Make, Python, and tests | Required operational environment, but no reachable transform is owned by an ad-hoc shell recipe | Keep only non-code-producing orchestration requirements |

## Resolved output ownership

Counts are output transforms in the checked audit, not textual recipe occurrences. Composite Python transforms list the code-producing utility they invoke as a second owner.

| Tool hand-off | Reachable outputs | Required external behavior |
| --- | ---: | --- |
| Host C compiler | 280 | 245 i386 root/user objects, 22 native hosted core/ELF32/x86/CupidDis/CupidASM/CupidObj/CupidLD/kernel-bridge objects, and 13 native contract/CLI executables; these builds remain temporary bootstrap evidence even though assembly, inspection, object-transformation, and link semantics have transferred |
| NASM | 4 | Two flat binaries and two ELF32 `ET_REL` objects. CupidASM now passes the required directive/expression, layout, instruction/addressing, symbol, relocation, and exact-output parity gates; production recipe and boot/runtime transfer remain |
| CupidLD | 5 owned transforms | Two script-driven kernel links plus three fixed-address user executables; owns `R_386_32`/`R_386_PC32`, weak/strong/common/script symbols, absolute COMMON alignment, relocation-aware merge entries, assertions, static ELF32 serialization, explicit unsupported allocated-section diagnostics, and the used `link.ld` subset |
| CupidObj | 181 owned transforms | 179 binary-to-ELF wrappers, one direct final-form read-only SMP wrapper, and final initialized ELF-to-raw conversion; the JPEG path invokes CupidObj once on preprocessed bytes with the original asset identity |
| Host symbol reader | 1 composite transform | Stable numeric symbols used by `_symbols_from_nm` and panic backtraces |
| Python | 8 transforms | Six root transforms plus the user and hosted-toolchain build-directory operations; symbol generation also uses Python |
| Make recursion | 3 transforms | Builds the hosted CupidObj/CupidLD executables from the root and CupidLD from the user build before production transforms consume them |

`tools/hostbuild.py::_symbols_from_nm` remains the symbol-reader subprocess seam. `embed_jpeg` performs optional image preprocessing, then calls CupidObj once with the original source identity; the former temporary-name wrapper plus three-symbol rewrite pass was removed. `tools/mksyms.sh` and `tools/embed_jpeg_baseline.sh` are tracked legacy/oracle duplicates outside the normal Make path.

The hosted contract suites use the host C compiler and its native linker backend to bootstrap and exercise the shared core, ELF32, x86, CupidDis, CupidASM, CupidObj, CupidLD, and the kernel's buffer-only fixed-image-to-`ET_EXEC` bridge. The ELF32, CupidASM, and CupidLD suites may additionally use NASM, GNU `readelf`, and standalone GNU/LLVM ELF linkers as optional comparison oracles. They prove that Cupid-written objects and executables are accepted by external consumers, that the Cupid reader accepts Clang-, NASM-, and linker-produced objects, that every active assembly source reaches the required raw, relocatable, or fixed artifact, and that object/link/executable failures roll back transactionally; absent oracle tools are skipped. Assembly, inspection, object-transformation, and link semantics have transferred, but source-build ownership and the four production NASM transforms have not. These tests do not transfer CupidC ownership.

The tracked `link.ld` is itself a compatibility contract. It uses `ENTRY`, `SECTIONS`, location-counter assignment, input-section wildcards, `ALIGN`, symbol definitions, `COMMON`, and repeated `ASSERT` statements. Both kernel ELF targets declare it as a prerequisite and pass it to CupidLD.

## Not host compilation

- The 104 active `bin/*.cc` roots and 22 `bin/browser/*.cc` fragments are wrapped by CupidObj and installed in the OS filesystem. CupidC compiles them on demand inside Cupid OS.
- The 22 `demos/*.asm` files are likewise embedded by CupidObj and assembled by CupidASM on demand.
- Repository headers and compatibility code replace the host libc/header environment for root compilation (`-nostdlib -nostdinc -ffreestanding`). The compiler executables remain external.
- The hosted contracts intentionally use the host C runtime only through the core adapter and thin CLI drivers. The shared arena, buffer, path, source, diagnostic, limit, object, instruction, assembly, and inspection behavior is freestanding and the same CupidASM source is linked into the kernel.
- Optional WAD discovery and test fixtures affect packaged/runtime content, not compiler ownership.

## Removal gate

A code-producing host dependency leaves the normal build only after the Cupid replacement has positive and negative tests, matches required object/ABI/layout behavior, builds its assigned active-source cohort, and passes the relevant OS boot or runtime smoke. The legacy host path remains available as an oracle until fixed-point bootstrap and behavior gates are reliable.
