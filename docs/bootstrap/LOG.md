# Bootstrapping log

Entries are chronological. Test results name the command and environment; a source-inspection finding is not recorded as a passing test.

## 2026-07-09: control plane and initial baseline

### Scope and decisions

- Began work on `bootstrap/cupid-self-hosting` from `0333208` (`Fix GUI terminal CupidC stack overflow`).
- Published the implementation map as [GitHub issue #13](https://github.com/cupidthecat/cupid-os/issues/13), with nine linked initial tickets and native blocking relationships.
- Confirmed Windows and Linux as canonical bootstrap hosts. Make and Python may remain as orchestration and image-building tools; external C compilers, assemblers, linkers, and binary utilities may not remain in the normal build.
- Confirmed two CupidC language modes: freestanding C11 and Cupid mode. The implementation direction is a shared typed AST followed by a shared linear IR.
- Confirmed the assembly compatibility target as the NASM behavior actually used by active Cupid OS sources, not unrestricted NASM compatibility.
- Confirmed deterministic ELF32 `ET_REL` as the compiler/assembler interchange, with CupidLD implementing the GNU linker-script subset used by `link.ld`.
- Confirmed checked-in Windows and Linux bootstrap seeds. Stage 2 and stage 3 tool outputs must be byte-identical before a seed update is accepted.
- Confirmed CupidDis as an objdump-class 16-bit/32-bit x86 inspector through SSE2, with DWARF v4 source information. x86-64 and AVX are out of scope.
- Confirmed that a trusted Cupid-built cohort may be at most 20% worse than the oracle in the agreed size or runtime measure; extra bootstrap stages are exempt from the runtime comparison.
- Confirmed that verified unreachable duplicate implementations may be removed only after evidence shows that no supported build or runtime path uses them.
- Confirmed that `TempleOS/` is reference material only. It must not be modified, built into Cupid OS, or counted in completion metrics.
- No unanswered design question blocks the initial baseline and active-build audit. Source cohort order, optimizer work, seed release mechanics, and CI frequency remain intentionally open until measurements exist.

### Questions and user answers

The user-approved implementation plan preceding this entry resolved the following questions. Answers are paraphrased as durable project decisions rather than presented as verbatim quotations.

| Question | User-approved answer |
| --- | --- |
| Which development hosts must the canonical bootstrap support? | Native Windows and Linux. |
| Must all host-side programs disappear? | No. Make and Python may remain for orchestration and image construction; they may not own compilation, assembly, object manipulation, linking, or disassembly in the normal build. |
| What C compatibility target should drive CupidC? | Freestanding C11 plus a distinct Cupid mode, with existing OS and vendored source driving additions. |
| Should C and Cupid mode be separate compilers? | No. They share a typed AST and linear IR while preserving distinct language boundaries. |
| How broad must Cupid ASM compatibility be? | The cleanly expressible NASM subset required by active Cupid OS source, including its real directives, expressions, sections, relocations, and output forms. |
| What object and linker contracts should the toolchain use? | Deterministic ELF32 `ET_REL` and the subset of GNU linker-script semantics exercised by Cupid OS. |
| What establishes bootstrap trust? | Checked Windows and Linux seeds with recorded provenance; stage 2 and stage 3 tool outputs must be byte-identical. |
| What is CupidDis expected to become? | An objdump-class 16/32-bit x86 inspector through SSE2 with ELF/object and DWARF v4 awareness. |
| What output-quality regression is acceptable? | At most 20% on an agreed trusted cohort's size or runtime measure; extra bootstrap stages are exempt from runtime comparison. |
| May duplicate or apparently stale implementations be removed? | Only after supported build/runtime reachability is checked and evidence proves them unreachable. |
| How is TempleOS treated? | Read-only reference material, excluded from Cupid OS builds and completion metrics. |
| Where is the long-running work tracked and developed? | GitHub issues in `cupidthecat/cupid-os`, with external PRs outside triage, on the long-lived `bootstrap/cupid-self-hosting` branch. |

### Current build pipeline

Source inspection of `Makefile`, `tools/hostbuild.py`, `link.ld`, and the native tool sources found this pipeline:

1. Make selects Clang, `ld.lld`, `llvm-objcopy`, and `llvm-nm` on Windows, or GCC, GNU `ld`, `objcopy`, and `nm` on non-Windows hosts. NASM is selected on both.
2. NASM produces the flat boot loader and SMP trampoline and ELF32 objects for interrupt and context-switch assembly.
3. The host C compiler produces freestanding i386 objects for the kernel, drivers, native Cupid tools, libraries, Doom port code, and vendored Doom sources. The Doom cohorts use separate relaxed flag sets.
4. `objcopy -I binary -O elf32-i386` wraps Cupid C programs, Cupid ASM demos, documentation, fonts, and other assets as linkable objects. The `.cc` and demo `.asm` files in this step are embedded source, not host-compiled programs.
5. The host linker performs a two-pass ELF kernel link with `link.ld`. Between passes, Python invokes host `nm` to generate `kernel/cpu/ksyms_data.c`, which is compiled by the host C compiler.
6. Host `objcopy` converts the final kernel ELF to a raw kernel binary.
7. Python creates and stages the disk image from the NASM boot binary, raw kernel, filesystem content, fixtures, and optional WADs.
8. QEMU is used for emulator verification; it is not a code-producing toolchain component.

Inside the booted OS, CupidC can JIT `.cc` files or emit fixed-address ELF32 executables, CupidASM can JIT `.asm` files or emit fixed-address ELF32 executables, and CupidDis can decode code and inspect a subset of ELF32 data. These tools currently depend on kernel/VFS services and have no checked-in host executable or bootstrap seed.

### Initial capability and dependency findings

- CupidC and CupidASM emit ELF32 `ET_EXEC` files with program headers, not relocatable `ET_REL` objects with sections, symbols, and relocations.
- CupidC generates x86 directly while parsing; it has no typed AST or linear IR seam yet.
- CupidC has substantial Cupid-language and C-like support, but active-source C11 coverage and ABI correctness have not yet been measured systematically.
- CupidASM has a substantial Intel-syntax encoder, labels, expressions, data/reserve directives, sections, and includes, but it has not been proven against the four host-assembled OS sources.
- CupidDis can inspect ELF32 executables and function symbols and decode a useful 32-bit subset, but it is not yet a host object inspector and lacks the required complete ISA, relocation, and DWARF coverage.
- There is no CupidLD or CupidObj implementation. The current linker, symbol extraction, raw conversion, and binary wrapping remain host-owned.
- Exact active-source feature inventories and generated-output parity are assigned to the build-graph and feature audit; this entry does not claim completeness.
- Tracked build scripts contain 161 host C-compiler recipe invocations, four NASM invocations, two host-linker invocations, 13 `objcopy` references in recipes, and one `nm` hand-off. Exact locations and semantics are recorded in `HOST-DEPENDENCIES.md`.

### Test results

Executed on native Windows PowerShell:

| Command | Result | Notes |
| --- | --- | --- |
| `python -m unittest tests.test_hostbuild` | PASS | 4 host-build helper tests passed. |
| `make -j4 all` | PASS | Built the complete legacy host image using the configured Windows LLVM/NASM path. |
| `python tools/gui_terminal_smoke.py --command "/bin/ls.cc"` | PASS | Explicit CupidC GUI-terminal compile/run smoke passed. |
| `python tools/gui_terminal_smoke.py --command "as /demos/hello.asm"` | PASS | CupidASM GUI-terminal smoke passed. |
| `python tools/gui_terminal_smoke.py` | FLAKY/PASS | The first default `ls` invocation reported a CupidC undefined-variable error; an unchanged rerun passed. Preserve this as a harness/runtime reliability finding rather than hiding it. |

The baseline build produced many untracked objects and images. They were deliberately left untouched and are not part of this documentation commit.

### Migration progress

No source cohort changed toolchain ownership in this step. GCC/Clang, NASM, host linker, `objcopy`, and `nm` remain required by the current build. This step established the durable records and dependency graph needed to migrate them without weakening the OS.

## 2026-07-09: reproducible baseline runner

### Decision and failed approaches

- Chose one deep host module, `tools/bootstrap_baseline.py`, behind `make bootstrap-baseline`. Make names the workflow; the module owns isolated process execution, evidence normalization, artifact hashing, comparison, and failure reporting.
- Rejected hashing the developer's ordinary `cupidos.img`: image construction intentionally preserves an existing FAT filesystem, including guest writes. Each baseline run instead starts from a clean detached worktree and therefore creates a new image.
- Rejected globbing every object in the checkout because stale or currently unreachable objects can exist. Make now expands the exact `KERNEL_OBJS` link cohort and appends the non-link format boundaries that the migration must preserve.
- Disabled optional host WAD discovery for baseline builds. Otherwise `/usr/share/games/doom/freedoom*.wad` makes the image depend on unrelated host content.
- Kept Windows LLVM and Linux GNU evidence separate. The two external toolchains are not required to match each other; two isolated builds using one recorded tool set must match.
- Chose final ELF `.text` bytes and kernel/image byte sizes as stable quality evidence. Build and GUI-smoke wall-clock time is recorded but explicitly non-gating until a guest-monotonic trusted performance cohort exists.
- Added canonical LF checkout attributes because `.cc`, `.h`, `.asm`, CTXT, and other text are embedded byte-for-byte into the image; host line-ending policy must not silently change artifacts.

### Implementation

- Added `make test` as the deterministic host unit-test entry point.
- Added `make bootstrap-baseline`, which probes and fingerprints required host tools plus the optional JPEG converters that can affect embedded bytes, builds a committed revision twice in disposable worktrees, runs host/CupidC/CupidASM checks, hashes 343 currently expanded artifacts, and emits `cupid.bootstrap-baseline.v1` JSON.
- Added per-file mismatch localization, missing-artifact errors, command failure/skip evidence, direct ELF32 section-size inspection, and atomic evidence writes.
- Added `.gitignore` coverage for 436 current build/cache/log artifacts without hiding `.agents/`, `TempleOS/`, or `skills-lock.json`.
- Extended clean targets for runtime logs, Python caches, the USB image, and root baseline output. Removed the dead `kernel/smp/smp_trampoline.bin` cleanup path; the actual output is `kernel/smp_trampoline.bin`.
- Added `docs/bootstrap/BASELINE.md` as the reproduction and interpretation guide.

### Test results before frozen capture

| Command | Result | Notes |
| --- | --- | --- |
| `python -m unittest tests.test_bootstrap_baseline` | PASS | 5 artifact, pre-runtime capture, comparison, ELF, and command-failure tests passed after red/green cycles. |
| `make test` | PASS | 9 total host tests passed through the new public Make entry point. |
| `python -m py_compile tools/bootstrap_baseline.py tests/test_bootstrap_baseline.py` | PASS | Baseline runner and tests compile under Python 3.14. |
| `make -s print-bootstrap-artifacts` | PASS | Expanded 343 distinct artifact paths, including generated `ksyms_data.o`; current build output contained every path. |
| Host tool preflight | PASS | Make, Python, Clang, NASM, LLD, LLVM objcopy/nm, and QEMU resolved and were SHA-256 readable; optional JPEG probing recorded FFmpeg as available and the IJG tools as absent. |

The first checked two-run Windows evidence is recorded in the following evidence commit so its `source.revision` names the committed runner it executed.
