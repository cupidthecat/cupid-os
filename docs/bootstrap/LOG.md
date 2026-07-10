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

### First capture failure and GUI input fix

The first capture of runner commit `e1cfed3` did not reach its second build. The clean build (9.316 seconds), host tests, and CupidC GUI smoke passed, but the CupidASM GUI smoke timed out with no panic. The serial log showed that the terminal sometimes received `/demoshello.asm`, `/demo/hello.asm`, or other reordered fragments instead of `as /demos/hello.asm`.

The failure was reproduced with an unattended three-attempt loop: two attempts failed and one passed. Three one-variable probes narrowed the cause:

- Doubling inter-key pauses did not help (one of three passed), so command pacing alone was rejected.
- Removing the USB keyboard made Ctrl+Alt+T fail in all three attempts, proving that the smoke VM had no usable alternate PS/2 injection path.
- Holding each QEMU HMP key report for 300 ms passed three of three attempts. The unmodified harness with that fix then passed the original three-attempt CupidASM loop three of three times.

The root cause was a mismatch between QEMU's default key-report hold and Cupid OS USB HID polling: a long delay between keys did not prevent the guest from missing a short individual press. `gui_terminal_smoke.py` now emits `sendkey <key> 300` for the terminal hotkey, command characters, and Return, with a regression test that pins the HMP command and pause.

| Command | Result | Notes |
| --- | --- | --- |
| `python -m unittest tests.test_gui_terminal_smoke` | PASS | The USB-visible 300 ms HMP hold contract is pinned. |
| `make test` | PASS | 10 host tests passed after adding the harness regression. |
| Three-attempt original CupidASM GUI loop | PASS | 3/3 passed at 23.37–23.58 seconds after the fix; the pre-fix loop passed only 1/3. |

### Frozen Windows evidence

The first checked Windows capture, later superseded by the complete manifest below, recorded committed revision `092ada58f4180d207a83dd577b898950bacedddd` on Windows 10 AMD64. It fingerprinted Make 4.4.1, Python 3.14.3, Clang and LLD 22.1.0, hashed LLVM objcopy/nm utilities, NASM 2.16.01, QEMU 10.2.50, and the available FFmpeg 8.0.1 JPEG converter.

| Evidence | Result |
| --- | --- |
| Isolated build 1 | PASS in 8.615 seconds |
| Host tests | PASS in 0.593 seconds (10 tests) |
| CupidC `/bin/ls.cc` GUI smoke | PASS in 18.947 seconds |
| CupidASM `as /demos/hello.asm` GUI smoke | PASS in 23.460 seconds |
| Isolated build 2 | PASS in 8.673 seconds |
| Reproducibility | PASS: all 343 artifacts matched; aggregate SHA-256 `b10222d82c963cac5330e0cf2bd7d9152d32c9f3c658adfd198d664b50291a4e` |
| Final ELF `.text` | 1,292,372 bytes |
| Final kernel ELF / raw binary | 6,123,276 / 5,919,539 bytes |
| Fresh baseline disk image | 209,715,200 bytes |

This completes the reproducible baseline on the current Windows host toolchain. Linux GCC/binutils evidence remains a separate host capture; Windows and Linux oracle outputs are not required to match each other.

## 2026-07-09: active build graph and source feature audit

### Audit interface and decisions

- Added `tools/build_graph_audit.py` behind `make bootstrap-audit` and `make check-bootstrap-audit`. Its external seam is a deterministic JSON inventory plus a generated Markdown summary; tests invoke the CLI against synthetic Make graphs rather than its parser internals.
- Chose GNU Make's expanded database as the graph authority. Extension-wide counts and early variable snapshots were rejected because they cannot distinguish host-compiled source, wrapped runtime source, generated source, or late `KERNEL_OBJS` appends.
- Preserved Make prerequisite order in every transform. Alphabetizing inputs was rejected because link order is an ABI/layout contract even though sorting would look deterministic.
- Added the separate `user:all` supported build root. Root `all` alone would incorrectly classify the three user C programs and `user/cupid.h` as inactive.
- Include closure covers declared `-I` paths, forced headers, quoted/angle C includes, and assembly `%include`. Generated C translation units remain reachable even when absent from a clean checkout; their generator/build feature is recorded without borrowing content from a dirty build tree.
- Feature IDs are comment/string-masked lexical evidence with file/line examples, supplemented by build-derived output and relocation requirements. They are deliberately labeled as discovery evidence rather than a substitute for a compiler AST or semantic tests.
- Independent review rejected the first generated snapshot because it read include edges from ignored generated C files when those files happened to exist. Generated translation units are now opaque generator outputs, and a two-state regression proves byte-identical audit JSON before and after materialization. The same review exposed lexical false positives: bit-field recognition now reports only the four declarations in `i_video.h`, function-pointer return syntax is not called a compound literal, and commented includes do not create graph edges.
- Kept graph extraction, evidence collection, schema rendering, and the atomic CLI check behind one standalone audit module for this first contract. The public seam is deliberately narrow; the collectors can move into separate modules when another consumer needs an independently evolving interface.
- Added `Supported build root` and `Source cohort` to the project glossary. The audit's cohort order transfers tool ownership without calling every checked file active.
- Added a checked contract that every final-link object is individually present in the reproducibility manifest. `make test` now verifies the checked audit and fails on source, Makefile, generator, summary, or contract drift. Source/control hashes canonicalize LF according to `.gitattributes`, so a pre-existing CRLF checkout and a clean checkout produce the same audit evidence.

### Resolved build graph and ownership

The two supported roots contain 642 active language inputs and 437 reachable transforms:

| Input/transform cohort | Count | Current ownership |
| --- | ---: | --- |
| C translation units | 241 | 238 root outputs through Clang/GCC; three user outputs through hard-coded GCC |
| C headers | 249 | 246 root preprocessor inputs, two embedded CupidC headers, one user ABI header |
| Cupid C source | 126 | 104 top-level programs plus 22 browser fragments; host objcopy embeds, CupidC compiles at runtime |
| Assembly source | 26 | Four NASM outputs plus 22 embedded CupidASM inputs |
| Root transforms | 430 | 238 C, four NASM, two linker, 181 object-copy-owned, and six Python-owned/composite transforms |
| User transforms | 7 | Three GCC objects, three GNU-ld executables, one host-shell-owned POSIX directory command |

The root pass-1 link has 419 ordered objects; the final link adds `ksyms_data.o` for 420. The 419 divide into 240 kernel/driver inputs and 179 wrapped payload objects. Python generates three installer C tables before pass 1 and the symbol C table between links.

### C and Cupid C requirements

The source audit established the following C-mode requirements:

- i386 ILP32/cdecl with signed plain char, 8/16/32/64-bit integer semantics, float/double, 16-byte stack/SIMD alignment, aggregate layout/calls, callbacks, variadics, and real volatile MMIO/IRQ/SMP behavior.
- Structs, unions, enums, typedefs, anonymous aggregate members, four verified bit-fields, `_Static_assert`, multidimensional/inferred arrays, function pointers, designated/partial initialization with zero-fill, static/tentative storage, weak/undefined linkage, and complete control/expression conversions. No compound literal is verified in the active corpus.
- Full preprocessing: 2,032 quoted and 146 angle includes, forced Doom compatibility input, object/function/multiline/variadic macros, rescanning, stringify, paste, GNU comma elision, nested arithmetic conditionals, predefined/command-line macros, `__FILE__`/`__LINE__`, and pack/once pragmas.
- GNU platform requirements: 199 basic/extended inline-assembly sites across 33 C files and 72 attribute sites across 31 files. Required attributes include packed/aligned, section, weak, noreturn, noinline, used/unused, and naked.
- The 83-file Doom/port cohort needs a deliberate compatibility mode for old/no-prototype declarations, five implicit calls, legacy callback/object-pointer conversions, shim headers, and relaxed diagnostics without weakening strict C mode.
- Cupid mode's 126 files exercise sized types, `float4`/`double2`, two class declarations, new/del, reg/noreg, `#exe`, native `asm {}` blocks, predefined kernel bindings, and browser-scale globals/include fragments. Existing wide-type fixtures do not prove the full 64-bit semantics required by C mode.

Source-driven CupidC order is therefore ELF32 interoperability first; then ILP32/cdecl types and calls; full preprocessing; storage/linkage/initialization/qualifiers; GNU attributes and extended asm; Doom compatibility; and finally production-scale plus extension-specific Cupid mode behavior.

### Cupid ASM and CupidDis requirements

All 26 checked assembly files are active and total 2,239 lines:

| Role | Files | Required output |
| --- | ---: | --- |
| Boot and SMP trampoline | 2 | Exact flat binaries |
| ISR and context switch | 2 | ELF32 `ET_REL` objects |
| Runtime demo/include corpus | 22 | Embedded source assembled by CupidASM |

The corpus has 1,196 instruction statements, 91 mnemonics plus two active prefixes, 153 colon labels, 33 exports, five externals, 99 memory operands, and 18 directive spellings. It requires `BITS`, `ORG`, section/global/extern, data and reserve forms, `times`, `equ`, `%define`, `%include`, `$`/`$$`, label arithmetic, binary-suffix literals, mixed 16/32-bit encoding, far transfers, segment/control registers, sized operands, segment overrides, and full ModRM/SIB indexing.

Executed oracle observations:

- `boot/boot.bin` is 2,560 bytes with SHA-256 `57884f86c907d8669f16a667e83238b5f840f2b67e7b82eeeedea09ec5244445`.
- `kernel/smp_trampoline.bin` is 4,096 bytes with SHA-256 `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90`.
- `isr.o` is ELF32 `ET_REL`; `.text` is 377 bytes aligned to 16 and has five `R_386_PC32` relocations. `context_switch.o` is ELF32 `ET_REL`; `.text` is 44 bytes aligned to 16 and has no relocations.

CupidASM is closest on the demo mnemonic set, but structural gaps dominate: no host interface, raw output, `ET_REL`, real sections/symbols/relocations, mode switching, complete expressions, far/segment/control forms, or emitted SIB indexing. Global/extern are parsed then discarded; include resolution is CWD-relative. CupidDis cannot yet validate the migration because it lacks 16-bit mode, semantic prefix handling, broad system/x87/SSE coverage, and `ET_REL` section/symbol/relocation inspection.

### ABI and object evidence

Direct `llvm-readobj` inspection of all 238 root C objects found exactly 27,375 `R_386_32` and 11,658 `R_386_PC32` relocations, with no other i386 relocation type. Current sections include `.text`, `.text.start`, `.rodata*`, `.data`, `.bss`, and `.ksyms`, with alignment through 64 bytes and local/global/weak/undefined symbols. Eleven objects rely on 64-bit division helpers.

`link.ld` requires `ENTRY`, `SECTIONS`, absolute location-counter updates, input-section wildcards, `ALIGN`, symbol definitions, `COMMON`, and `ASSERT`. It is referenced by link flags but is not a declared Make prerequisite. The three Python installer generators similarly omit `tools/hostbuild.py` from their prerequisite lists; 106 active headers are absent from handwritten object dependencies. Forced rebuilds mask much of that dependency incompleteness in root `all`.

The audit also corrected README drift: `run-net-debug` did not exist, while `run-net`/`run-net-e1000` do; active counts are 104 top-level Cupid C programs, 22 browser fragments, and 22 assembly demos rather than the older 88/21 figures.

### Unreachable and duplicate findings

The machine inventory accounts for 35 checked source-like files outside the supported roots: 11 C files, three Cupid C files, 20 headers, and one CupidScript file. No assembly source is unreachable.

- `bin/old_cc2.cc` and `bin/old_cc2_single.cc` are explicitly filtered legacy compiler fixtures.
- Seven `bin/*.c` files are diverged historical snapshots of live kernel/tool implementations. The machine records each active counterpart rather than mislabeling these files as byte-identical duplicates.
- `scheduler.c`/`.h`, `notepad.c`, `terminal_ansi.c`, and `demos/paint.cc` have recorded active successors. `cupidc_runtime.c`/`.h` remain unlinked without an asserted successor.
- The separate `bin/build.cup` CupidScript is also not embedded or referenced and is included directly in the source-like JSON universe.
- Three generated browser headers and six project headers are unreferenced. Fourteen additional dormant headers are from the vendored Doom compatibility surface.
- Seven unreachable one-line Doom stub headers have exact content matches. Header path identity can still be semantically required, so byte equality is not removal evidence.
- `browser_css_gen` is not reachable from root `all`, its outputs are unused, and its declared Blink `.in` prerequisites are absent.

No source was deleted. `not_reached` proves absence from the two supported roots, not safety to remove from every intended future workflow.

### Reproducibility manifest defect and correction

The audit found that `BOOTSTRAP_ARTIFACTS := $(KERNEL_OBJS)` and the `$(KERNEL_OBJS): FORCE` rule were expanded before later TLS/Doom appends. The historical manifest therefore contained 343 paths and omitted 84 linked objects: the checked CA bundle object, four Doom port objects, and all 79 vendored Doom objects. Downstream ELF/kernel/image hashes were still captured, but the claim of complete per-object hashing was false.

Both whole-link declarations now occur after the final append. The manifest contains 427 unique paths and the checked contract proves coverage of all 420 final-link objects. The earlier Windows JSON was retained only as historical evidence until the corrected clean-worktree capture recorded below.

An ordinary working-tree hash comparison was rejected as recapture evidence. This checkout predates the LF attributes and still has CRLF source bytes: exactly 171 byte-wrapped text objects differed from the clean-LF historical capture, followed by three downstream kernel artifacts and the persistent disk image. Compiled C objects and the boot/trampoline binaries matched. The isolated baseline runner remains the authority because it checks out canonical LF text and starts with a fresh image.

### Test and inspection results

| Command/check | Result | Evidence |
| --- | --- | --- |
| `python -m unittest tests.test_build_graph_audit` | PASS | Eleven CLI-level tests cover graph/tool ownership, include closure, feature evidence, unreachable/duplicate/successor classification, manifest coverage, generated-source two-state determinism, supplemental builds, ABI/link script, drift/errors, and LF/CRLF equivalence. |
| `make check-bootstrap-audit` | PASS | Checked JSON/Markdown match generated bytes; 427 declared artifacts cover all 420 linked objects. |
| `make test` | PASS | 21 host tests passed and the audit drift/contract gate passed. |
| `make -j4 all WAD_SRCS=` | PASS | Complete host image build succeeded after forcing every final kernel object. |
| `llvm-readobj --relocations <238 root C objects>` | PASS | Only `R_386_32` (27,375) and `R_386_PC32` (11,658) were present. |
| ELF/raw assembly inspection | PASS | Boot/trampoline sizes/hashes and ISR/context-switch ELF32 properties matched the recorded oracle constraints. |

No source cohort changed tool ownership in this step. The audit clears the source-order fog and makes the next host-core/object/instruction seams concrete; the corrected Windows baseline recapture is recorded below as a separate evidence commit.

## 2026-07-09: corrected Windows oracle baseline

The Windows baseline was recaptured from committed audit revision `7a8cf7ab6d8d3372352ab61c86c31efad0493e89`. A first invocation used an erroneously short command-wrapper timeout, closed the runner's output stream, and wrote failure evidence with zero builds. That partial file was rejected and replaced by a complete rerun with enough time for both isolated builds and guest smokes.

The complete capture passed:

| Evidence | Result |
| --- | --- |
| Reproducibility | PASS: all 427 artifacts matched between two isolated builds; aggregate SHA-256 `d1e176f5d8543105cc6febff368d128563a7de2539fd2da54dd03056234d5bf4` |
| Manifest contract | PASS: the audit independently covers all 420 final-link objects |
| Clean builds | PASS in 8.231 and 9.487 seconds |
| Host tests | PASS: 21 tests in 4.590 seconds |
| CupidC guest smoke | PASS: `/bin/ls.cc` in 19.067 seconds |
| CupidASM guest smoke | PASS: `as /demos/hello.asm` in 23.823 seconds |
| Output sizes | Kernel ELF 6,123,276 bytes; raw kernel 5,919,539 bytes; `.text` 1,292,372 bytes; disk image 209,715,200 bytes |

The capture fingerprints Windows 10 AMD64, Make 4.4.1, Python 3.14.3, Clang/LLD 22.1.0, NASM 2.16.01, LLVM objcopy/nm, QEMU 10.2.50, and FFmpeg 8.0.1. Linux GCC/binutils evidence remains a separate pending capture; cross-host oracle bytes are not required to match.

## 2026-07-09: shared Cupid Toolchain core seam

### Interface decision

- Added a freestanding `toolchain/ctool.c` core with no libc or kernel headers. One opaque toolchain job owns a bounded chunked arena, canonical logical paths, NUL-sentinel whole-file sources, and insertion-ordered structured diagnostics; checked growable buffers are explicit bounded handles and invocation output is scoped to the job. All public sizes, offsets, limits, and serialized integer helpers are checked 32-bit values for the i386/ELF32 domain.
- Chose three composable platform capabilities: allocator, whole-file size/read/write, and length-aware text sink. The hosted adapter implements them with the C runtime below an explicit checkout root; the kernel adapter implements them with `kmalloc`/`kfree`, VFS, and `print`. Arena alignment belongs to the core, so the kernel heap needs to promise only pointer alignment.
- Added `ctool_invoke` as a deep lifecycle helper: normalize paths, load the primary input, create scoped output, run a typed callback, suppress file writes after a callback failure or error diagnostic, render diagnostics, and release all state. This is not a generic tool dispatch enum; later `cupidc_compile`, `cupidasm_assemble`, and `cupiddis_inspect` interfaces remain separately typed modules.
- Rejected a giant platform vtable containing x86, ELF, JIT, shell, or kernel-symbol behavior because it would move the existing coupling instead of hiding it. Also rejected bare allocator/file callbacks without job ownership because every frontend would repeat limits, cleanup, path, output, and diagnostic policy.
- Kernel bindings, executable-memory policy, fixed JIT addresses, shell state, and stack guards remain in kernel drivers. Existing CupidC, CupidASM, and CupidDis entry points and behavior are unchanged; this step creates their migration seam but does not claim their frontends are host-runnable.
- Recorded the stable decision in ADR 0006 and added `Toolchain job` and `Platform adapter` to the glossary. No user clarification was needed and `TempleOS/` remained read-only reference material outside all counts.

### TDD and implementation evidence

- The first public-contract test failed because no `toolchain` build root existed. The green implementation added a strict C11 hosted build and seven black-box contract cases covering zeroed/aligned arena allocation and rewind, checked little-endian buffer emission/patching, canonical relative/absolute path resolution and root-escape rejection, stable diagnostics, real binary file round-trip and missing-input translation, configured-limit failures, and invocation output suppression.
- Strict compilation then exposed a `const`-incorrect arena rewind token; the token now carries a mutable opaque block identity without exposing arena internals. Later boundary review found and fixed zero-length null-pointer arithmetic, small output-limit initialization, unchecked manually constructed paths, unsafe string-length condition ordering, and an internal failed path resolution that incorrectly invalidated an outer arena mark. Nested marks now remain composable while their block/offset exists.
- A second red audit test showed that all `$(CC)` recipes were labeled as ELF32 object compilation. Classification now uses object suffix or standalone `-c`, preserves objects whose `-c` is hidden in `$(CFLAGS)`, recognizes direct compiler links as hosted executables, and recognizes objects feeding those links as native host objects. This avoids assigning the i386 `ET_REL` requirement to the three hosted contract objects.
- Independent Standards review found missing real-adapter not-found coverage, repeated audit-root arguments, and a primitive host root pointer/length pair. The final implementation adds hosted and kernel `CTOOL_ERR_NOT_FOUND` checks, centralizes `BOOTSTRAP_AUDIT_BUILDS`, and uses `ctool_string_t` for the host root. A follow-up found that recycled arena block addresses could make stale marks appear valid; per-block generations plus a deterministic recycling allocator regression now prove stale-mark rejection and zeroed reused storage. Final Standards and Spec re-reviews reported no remaining findings.
- Added a DEBUG kernel self-test after VFS initialization and embedded `/bin` installation. It enters only through `ctool_invoke`, loads `/bin/ls.cc`, verifies the source sentinel, 16-byte zeroed arena allocation and canonical path equivalence, commits a marker to `/tmp`, reads it back, removes it, and then verifies a missing `/bin` input maps to `CTOOL_ERR_NOT_FOUND` without creating output. It panics on any mismatch. The boot log recorded `Cupid toolchain core self-test passed` before entering the desktop.
- Output is gated at the invocation boundary, not transactionally replaced by the current file adapters: a callback/error diagnostic cannot touch the destination, but a platform write failure can still leave a partial file. Whole-file source caching/interning, atomic replacement, and migration of the three tool frontends remain later work.

### Build graph and migration state

The hosted contract is the third supported root beside root `all` and `user:all`. The checked graph now contains 649 active language inputs, 248 feature IDs, 444 transforms, and 35 accounted unreachable source-like files. Its 429 declared artifacts cover all 422 final-link objects. Of the C outputs, 243 are i386 ELF32 objects and three are native host objects; the host compiler also links the temporary native contract executable.

The previous Windows oracle JSON remains valid historical evidence for revision `7a8cf7a` at 427 artifacts/420 link objects. Because this step adds two linked kernel objects, the replacement capture was made from committed implementation `e72f608` and is recorded below rather than being inferred from the working tree.

### Verification

| Command/check | Result | Evidence |
| --- | --- | --- |
| `python -m unittest tests.test_toolchain_core` | PASS | Seven public hosted contract tests passed under strict C11 flags. |
| WSL Linux GCC hosted contract | PASS | The same seven public tests built and ran natively under Linux GCC; MinGW GCC also compiled the strict hosted target. |
| Linux ASan/UBSan contract run | PASS | All contract modes, including real file I/O and success/error invocation paths, completed without sanitizer findings. |
| `python -m unittest tests.test_build_graph_audit` | PASS | Twelve CLI tests passed, including native-object/direct-link classification. |
| `make test` / `make check-bootstrap-audit` | PASS | All 29 host tests passed; regenerated JSON/Markdown and the 429/422 coverage contract matched exactly. |
| Strict i386 object build | PASS | The shared core and real kernel adapter compiled with the repository's freestanding warning-as-error flags. |
| `make -j4 all WAD_SRCS=` | PASS | The complete 422-object kernel, raw binary, and disk image built. |
| CupidC GUI smoke (`ls`) | PASS | Kernel adapter self-test passed during boot; CupidC reached `JIT execution complete` without panic. |
| CupidASM GUI smoke (`as /demos/hello.asm`) | PASS | Kernel adapter self-test passed during boot; CupidASM reached `JIT execution complete` without panic. |

The complete Linux OS oracle baseline remains pending evidence, not an inferred success.

## 2026-07-09: shared-core Windows oracle recapture

The Windows oracle was recaptured from committed shared-core revision `e72f608f08260f50f8f43eb8970cda4d66c1ae62`. The first launch mistakenly used a five-second wrapper timeout and was terminated during the first build; it left no process or worktree behind, and its disposable 4 KB partial evidence was rejected. A complete rerun with a four-minute command window replaced it.

| Evidence | Result |
| --- | --- |
| Reproducibility | PASS: all 429 artifacts matched between two isolated builds; aggregate SHA-256 `124220c8d0e14621ee654e5a252c655fe41d38491cd6151edc5d2282a3b715c3` |
| Manifest contract | PASS: the checked audit covers all 422 final-link objects, including `toolchain/ctool.o` and `kernel/lang/ctool_kernel.o` |
| Clean builds | PASS in 8.809 and 9.589 seconds |
| Host tests | PASS: 29 tests in 5.460 seconds |
| CupidC guest smoke | PASS: `/bin/ls.cc` in 18.810 seconds, after the kernel core self-test |
| CupidASM guest smoke | PASS: `as /demos/hello.asm` in 23.376 seconds, after the kernel core self-test |
| Output sizes | Kernel ELF 6,146,648 bytes; raw kernel 5,939,861 bytes; `.text` 1,309,972 bytes; disk image 209,715,200 bytes |

The capture fingerprints the same Windows 10 AMD64 oracle family (Make, Python, Clang/LLD, NASM, LLVM objcopy/nm, QEMU, and optional JPEG tooling) in the machine-readable manifest. Linux GCC/binutils remains a separate complete-OS capture; the hosted core contract already passed Linux GCC and sanitizer checks but is not a substitute for that oracle.

## 2026-07-09: deterministic shared ELF32 relocatable objects

### Object seam and format decisions

- Added the freestanding `toolchain/elf32.c` module with exactly two public operations: a one-shot semantic `ctool_elf32_write` description and a bounded typed `ctool_elf32_read` view. It depends only on the shared job/arena/source/diagnostic/buffer contracts and is compiled unchanged into the hosted object contract and kernel.
- Compared a stateful append-only builder, raw normalized ELF arrays, and a common-caller one-shot description. The chosen seam keeps frontend parser state out of the object module and hides file offsets, serialized indices, metadata tables, string offsets, layout, and padding while retaining ordered semantic sections, symbols, and relocations.
- The strict canonical writer emits little-endian i386 `ET_REL`: caller `PROGBITS`/`NOBITS` sections, one `SHT_REL` per target, `.symtab`, `.strtab`, `.shstrtab`, then section headers. It stable-partitions local symbols before global/weak symbols, preserves relative input order, remaps relocations, interns strings on first occurrence, and emits deterministic zero padding.
- Relocations carry explicit signed addends at the interface. The module writes or reads the four-byte implicit target field required by `SHT_REL`; `R_386_32` and `R_386_PC32` follow the i386 `S + A` and `S + A - P` rules. Local/global/weak, notype/object/function/section/file/common, and undefined/defined/absolute/common storage are represented without exposing `SHN_*` policy to producers.
- The reader uses explicit little-endian decoding rather than packed hostile-input casts. It rejects malformed known semantics and unsupported object-wide domains, but preserves structurally valid unfamiliar section and relocation codes so CupidDis can inspect more than CupidLD or the canonical writer can yet consume. Unknown relocation addends remain explicitly undecoded.
- Existing CupidC/CupidASM fixed-address `ET_EXEC` output is unchanged. The shared object implementation is now Cupid-owned, but no compiler, assembler, disassembler, linker, object-copy, or source cohort ownership transfers in this step.

### TDD, failures, and fixes

- The first writer contract failed with the missing implementation. Successive red cases drove basic deterministic layout, then the complete symbol/relocation model. An output-limit case exposed both a missing rollback diagnostic and a dangling diagnostic when the arena was rewound; failure diagnostics are now emitted after transactional state restoration.
- Reader round-trip began with an undefined entry point. Malformed-input tests then exposed acceptance of invalid known `STT_SECTION` placement; known symbol-kind semantics are now validated. Failed reads rewind temporary arena state, zero the result, and retain their diagnostic.
- A review-time red case placed two four-byte relocations two bytes apart. The original duplicate-offset check accepted the partially overlapping fields; both writer models and reader objects now reject every known i386 overlap without overflow-prone endpoint arithmetic. Further hostile mutations added common-function, string-table metadata, and invalid-argument rollback coverage.
- Independent review found that the writer exposed link-order/info/group and arbitrary flag bits even though its semantic sections cannot supply the metadata those flags require. V1 now accepts only write/allocate/execute/merge/strings/TLS/exclude, validates merge/string entry semantics, and has positive and negative public contracts.
- Independent review also found that resolving every referenced string offset by rescanning to NUL could amplify a bounded object into quadratic kernel work. The reader now heap-sorts temporary offset/view pairs and resolves each string-table region in one forward pass; 512 descending suffix references into a 32 KiB name exercise the bounded path.
- Kernel integration exposed that the toolchain `test` target invoked the ELF contract without declaring it as a prerequisite. The graph now builds both contracts before running either. The checked active-source audit correctly rejected the resulting graph drift and was regenerated only after review.
- Positive contracts cover deterministic layout, ordered sections, alignment, weak/common symbols, both relocation kinds, explicit addends, stable local partitioning, round-trip parsing, and transactional failure. Negative contracts cover invalid writer models, truncated/corrupt headers and tables, invalid strings, symbols, relocations, alignments, ranges, and unsupported domains.

### Interoperability and build state

- Two independently generated Cupid objects were byte-identical. GNU `readelf` accepted their sections, symbols, weak/common storage, and both relocations; LLVM `ld.lld -m elf_i386 -r` consumed one and the Cupid reader parsed the linked output.
- The reader accepted a Clang i386 `-fcommon` fixture and the real NASM `kernel/cpu/isr.asm` object. The ISR oracle retained a 377-byte, 16-byte-aligned `.text`, five `R_386_PC32` entries, the required symbols, and `-4` call addends. Oracle binaries are optional test dependencies and are skipped when unavailable; their eight-line C fixture is intentionally test-only and therefore recorded as `not_reached` by the three supported `all` build roots.
- A DEBUG kernel self-test writes and reads an actual relocatable object through the public seam, including one undefined symbol and one PC-relative relocation, and logs `Cupid ELF32 object self-test passed` after the existing toolchain-core self-test and before the desktop starts.
- The checked graph now contains 652 active language inputs, 248 feature IDs, 448 transforms, and 36 accounted unreachable source-like files. Its 430 declared artifacts cover all 423 final-link objects; the host compiler still owns 244 i386 objects, five native contract objects, and two native contract executables.

Alternating the Linux sanitizer build and the dedicated Windows oracle test exposed a cross-host test-isolation failure: Windows Make tried to link Linux objects left in `toolchain/build`. The hosted Make root now permits an overridden build directory, and the ELF Python suite uses its own automatically cleaned temporary directory rather than deleting or reusing the shared one. The unchanged suite passes independently of a WSL-produced default build directory and no longer races another suite's clean.

### Verification

| Command/check | Result | Evidence |
| --- | --- | --- |
| `make test` | PASS | All 38 repository host tests passed, including nine ELF32 module/oracle tests and the generated-audit check. |
| Strict Windows Clang and MinGW GCC contracts | PASS | Both compilers built the shared core/object contracts with warning-as-error C11 flags; every contract mode passed. |
| WSL Linux GCC contract | PASS | The same public writer, reader, malformed-input, and core modes built and ran natively. |
| WSL Linux ASan/UBSan contract | PASS | Every public contract mode completed with address/undefined sanitizers and no finding. |
| GNU/LLVM/NASM/Clang object oracles | PASS | Cupid output was deterministic, readable, and linkable; external compiler/assembler/linker objects were accepted with the expected semantic manifests. |
| `make check-bootstrap-audit` | PASS | The checked 652-source/448-transform graph and 430/423 artifact-coverage contract matched generated evidence. |
| Strict freestanding i386 objects | PASS | `toolchain/elf32.o` and `kernel/lang/ctool_kernel.o` rebuilt with the repository's warning-as-error freestanding flags. |
| `make -j4 all WAD_SRCS=` | PASS | The complete 423-object kernel, raw binary, and disk image built. |
| CupidC GUI smoke (`/bin/ls.cc`) | PASS | Boot logged the shared core and ELF32 self-tests; CupidC completed execution without panic. |
| CupidASM GUI smoke (`as /demos/hello.asm`) | PASS after unchanged retry | The first attempt hit the known terminal-input flake and reported `undefined variable` before assembly; the unchanged retry completed, with the ELF32 self-test passing on both boots. |

## 2026-07-09: shared-ELF32 Windows oracle recapture

The Windows oracle was recaptured from committed shared-object revision `1b5901c1b363182e07ff9f0c9e109f429982deba`. The first capture built its initial isolated tree and passed all 38 host tests, but its CupidC GUI command lost the `n` keystroke and reached the compiler as `/bi/ls.cc`, alongside a stray `expected expression` input. The resulting preprocess failure was the known terminal-input flake, not an ELF failure. The runner exited, left no QEMU process or worktree, and its failed disposable evidence was replaced by an unchanged retry.

| Evidence | Result |
| --- | --- |
| Reproducibility | PASS: all 430 artifacts matched between two isolated builds; aggregate SHA-256 `5a25340e2c6b803cd0bfd7dac8b8a866c97fd008ea0529e3f19d8478a6d3279f` |
| Manifest contract | PASS: the checked audit covers all 423 final-link objects, including `toolchain/ctool.o`, `toolchain/elf32.o`, and `kernel/lang/ctool_kernel.o` |
| Clean builds | PASS in 9.915 and 9.987 seconds |
| Host tests | PASS: 38 tests in 9.429 seconds |
| CupidC guest smoke | PASS: `/bin/ls.cc` in 19.219 seconds, after the shared core and ELF32 kernel self-tests |
| CupidASM guest smoke | PASS: `as /demos/hello.asm` in 23.800 seconds, after the same self-tests |
| Output sizes | Kernel ELF 6,181,012 bytes; raw kernel 5,973,175 bytes; `.text` 1,340,356 bytes; disk image 209,715,200 bytes |

The machine-readable evidence fingerprints the Windows 10 AMD64 oracle and its Make, Python, Clang/LLD, NASM, LLVM objcopy/nm, QEMU, and optional JPEG tooling. Linux GCC/binutils remains a separate complete-OS capture; the shared core/object contracts passing Linux GCC and sanitizers do not substitute for it.

## 2026-07-09: shared 16/32-bit x86 instruction model

### Interface and catalogue decisions

- Added the freestanding `toolchain/x86.h` and `toolchain/x86.c` module above the shared job core. Its public seam is typed semantic instructions, operands, registers, memory addresses, far pointers, constants or opaque references, exact encoded bytes, decoded results, and serialized field spans; it does not expose the private form rows. The same source compiles under strict hosted C11 and the freestanding i386 kernel flags.
- One private flat catalogue is the only opcode/form authority for both encoding and decoding. Its 546 rows comprise 544 encodable forms and two decode-only invalid recognizers, covering 226 canonical mnemonics and 64 canonical registers; ordered catalogue rows plus the complete canonical/alias mnemonic and register name tables produce fingerprint `3159218E`. Production validation rejects malformed rows, undeclared decode-key collisions, out-of-range roles and flags, invalid recognizers that claim a mnemonic, and any canonical mnemonic without a form.
- `AUTO` chooses the shortest successful encoding with stable catalogue-order tie-breaking. A caller may request the opaque form returned by decode to replay the same catalogue spelling, but only with the same model fingerprint and matching semantics. Output is a caller-owned 15-byte value and is fully zeroed on every failure.
- Raw decode distinguishes `KNOWN`, `UNKNOWN`, `TRUNCATED`, and `INVALID`: unknown input consumes one byte for inspector progress, truncation consumes none while retaining available bytes, and exact matched-form illegality or an explicit invalid catalogue row is invalid. A mandatory-prefix, group digit, or overlapping memory/register shape mismatch is only `NO_MATCH`, so valid unimplemented PSRAW, LLDT, CVTPD2PS, and FNOP bytes remain `UNKNOWN`. Repeated prefixes in one legacy prefix group and a 15-byte prefix-only run are deliberately invalid in v1. Longest successful decode resolves `FINIT` versus `FWAIT`; explicit fixed-width aliases have a declared canonical policy rather than silently depending on row order.
- Encoded field spans retain operand, offset, serialized width, reference, signed addend, absolute/PC-relative intent, and PC bias. Raw bytes never invent relocation ownership. Parsing, expressions, labels, symbols, ELF policy, formatting, executable layout, and JIT behavior remain outside this seam; later CupidASM and CupidDis integrations will bridge the existing shared ELF32 model.
- Recorded the stable boundary and rejected alternatives in ADR 0007. A semantic recipe VM was deferred because the measured v1 corpus does not justify another interpreter and exceptional-handler policy; family callbacks were rejected because they would recreate independent opcode authorities. No project-specific glossary term was added for the general x86 instruction/form vocabulary, and no user clarification was needed. `TempleOS/` remained read-only reference material and outside every count.

### Source-driven audit and failed approaches

- Audited all 26 active assembly inputs: 2,239 lines, 1,196 instruction statements, 91 mnemonic families, two semantic prefixes, 27 register names, 107 memory operands, 162 normalized signatures, and 182 mode-specific cases. This corrects the earlier hand-written capability prose that said 99 memory operands; the generated audit already carried the source facts. A checked Python normalizer now reconstructs those counts and exact manifest keys from the live source on every test run. A separate scan found 208 inline-assembly occurrences across 36 active C/Cupid C files.
- Exact oracle work covered all 169 instructions in `kernel/cpu/isr.asm` and all 20 in `kernel/core/context_switch.asm`, then the mixed-mode boot sector and SMP trampoline. The first catalogue missed active `OUT DX,AX` and accumulator `A1`/`A3` moffs forms, emitted an unnecessary absolute-address SIB, and could not exactly consume those NASM bytes. Adding the real forms and canonical no-base/no-index ModRM rule fixed the source-driven gaps without simplifying any assembly.
- A name-only registry initially made unsupported mnemonics appear complete. Model validation now requires a real form for every canonical mnemonic, and the catalogue was extended across ordinary integer, descriptor/system, far transfer, port, string, x87, SSE, and SSE2 families used by the active source or current CupidC emitter. MMX registers and operand classes are typed in the public/private vocabulary so future MMX forms do not require an interface change; MMX encodings are not claimed as active v1 coverage.
- Encoder/decoder review exposed and fixed conflation of an unrelated row's `NO_MATCH` with a recognized illegal `INVALID`, `LOCK` acceptance on non-lockable or register-only forms, reserved control/segment encodings, `PAUSE` prefix handling, operand-size aliases, signed relative/displacement decoding, commuted 16-bit address pairs, narrow unresolved-reference selection, displacement truncation, and `FINIT` shadowing. The catalogue fingerprint was strengthened to include names and aliases after the initial form-only hash could not detect all semantic changes.
- The initial tests proved name lookup but not usable source coverage. The final active-surface contract consumes two traceable source manifests: one NASM 2.16.01 representative for every one of the 182 mode-specific active-ASM signatures, plus 129 manually audited, cohort-linked source-spelling/form rows representing all 208 active C/Cupid C inline-assembly sites and all 96 source instruction spellings while retaining source aliases and distinct widths or directions. It also retains 153 focused and current-CupidC integer/SSE vectors. Every manifest vector retains exact bytes and fields and re-encodes byte-identically through its requested form. A separate supported-host regression retains GNU's `66 F3` input bytes on decode and proves semantic re-encoding uses canonical `F3 66` order, as the public contract requires. Every truncation boundary and representative unknown, illegal-prefix, illegal-group, reserved-register, memory-only, range, relocation, and transactional failure path is checked. A generated every-form/operand-combination corpus remains a later strengthening step.
- Independent Standards and Spec review found that the first “active” corpus had missed inline-x87 semantics, `PSRLW xmm,imm8` incorrectly admitted its reserved memory spelling, and SSE move store-direction rows incorrectly rejected valid register-register bytes. Expanding the checked inline corpus to all 129 source-spelling/form rows also caught AT&T `fsub %st,%st(1)`: GNU emits `DC E1`, whose Intel-ordered model semantic is `FSUBR ST(1),ST(0)`, not `FSUB`. A deeper decoder review then caught valid unimplemented instructions being called invalid merely because they shared an opcode with a modelled row; prefix, group-digit, and overlapping-shape mismatches now remain `NO_MATCH`, while reserved knowledge stays in explicit rows of the same catalogue. The added x87 forms, corrected reverse-subtract semantic, conservative unknown/invalid boundary, register-only PSRLW constraint with negative encode/decode cases, and register-or-memory SSE store rows are now in the same catalogue and corpus. The review also exposed that a 139-vector family sample could not prove the documented 182 mode cases; the checked source normalizer and manifest replace that unsupported inference and derive the ASM cohort from the generated build graph.

### Kernel integration and migration state

- The root kernel now links `toolchain/x86.o`. The DEBUG adapter validates the model, encodes an exact 16-bit `MOV AX,[BX+SI+0x7f]`, verifies a symbolic PC-relative `CALL` field, decodes an SSE2 `PXOR`, and logs `Cupid x86 instruction model self-test passed` after the shared core and ELF32 tests.
- The hosted toolchain root builds a third native contract executable and runs eight x86 modes: inventory, model, integer, addressing, relocations, system/SIMD, active surface, and errors. The checked build graph now contains 655 active inputs, 248 feature IDs, 452 transforms, and 36 accounted unreachable source-like files. Its 431 declared artifacts cover all 424 final-link objects; the host C compiler still owns 245 i386 objects, seven native contract objects, and three native contract executables.
- This step transfers shared instruction semantics, not a source or artifact cohort. The existing CupidC, CupidASM, and CupidDis frontends do not yet call the module; NASM still produces the two flat binaries and two ELF32 objects, and GCC/Clang, the host linker, objcopy, and nm retain their documented production roles. The committed 431/424 graph is captured by the `c1df30c` Windows oracle recorded below.

### Verification

| Command/check | Result | Evidence |
| --- | --- | --- |
| `make -C toolchain test` and focused Python x86 suites | PASS | All shared core/object modes and eight x86 modes passed under Windows Clang; 10 Python contract/source-manifest tests passed. |
| WSL Linux GCC hosted contracts | PASS | The identical shared sources compiled with strict warning-as-error GCC and every contract mode passed. |
| WSL Linux ASan/UBSan hosted contracts | PASS | Every mode completed under address/undefined sanitizers with leak detection and no finding. |
| `make -B toolchain/x86.o kernel/lang/ctool_kernel.o` | PASS | Both sources compiled with the repository's strict freestanding i386 warning-as-error flags. |
| `make check-bootstrap-audit` / `make test` | PASS | Generated JSON/Markdown matched exactly; all 48 repository host tests passed and the 431/424 coverage contract remained complete. |
| `make -j4 all WAD_SRCS=` | PASS | The complete 424-object kernel, two-pass ELF, raw binary, and disk image built. |
| CupidC GUI smoke (`/bin/ls.cc`) | PASS | Boot logged all three shared-module self-tests and CupidC reached `JIT execution complete` without panic. |
| CupidASM GUI smoke (`as /demos/hello.asm`) | PASS | Boot logged all three shared-module self-tests and CupidASM reached `JIT execution complete` without panic. |

The complete Linux GCC/binutils OS oracle baseline remains pending; hosted GCC and sanitizer contracts do not substitute for it.

## 2026-07-09: shared-x86 Windows oracle recapture

After committing the shared x86 model as `c1df30c`, the baseline runner rebuilt that exact revision twice in isolated worktrees and refreshed `baselines/windows-amd64.json`.

| Check | Result |
| --- | --- |
| Reproducibility | PASS: all 431 artifacts matched between two isolated builds; aggregate SHA-256 `11febd09886aba270b9e3780774f1d0ab5e825146bf496e18e7cf9fb23ff596f` |
| Manifest contract | PASS: the checked audit covers all 424 final-link objects, including `toolchain/ctool.o`, `toolchain/elf32.o`, `toolchain/x86.o`, and `kernel/lang/ctool_kernel.o` |
| Clean builds | PASS in 8.754 and 7.792 seconds |
| Host tests | PASS: 48 tests in 12.096 seconds |
| CupidC guest smoke | PASS: `/bin/ls.cc` in 18.628 seconds, after all three shared-module kernel self-tests |
| CupidASM guest smoke | PASS: `as /demos/hello.asm` in 23.229 seconds, after the same self-tests |
| Output sizes | Kernel ELF 6,249,088 bytes; raw kernel 6,039,445 bytes; `.text` 1,369,924 bytes; disk image 209,715,200 bytes |

The machine-readable evidence fingerprints the Windows 10 AMD64 oracle and its Make, Python, Clang/LLD, NASM, LLVM objcopy/nm, QEMU, and optional JPEG tooling. Linux GCC/binutils remains a separate complete-OS capture; the hosted GCC and sanitizer contracts do not substitute for it.
## 2026-07-09: shared and hosted CupidDis object inspection

Wayfinder ticket [#19](https://github.com/cupidthecat/cupid-os/issues/19) moved CupidDis off its kernel-only opcode/packed-ELF implementation and behind the shared Toolchain seams. `toolchain/cupiddis.c` is freestanding and composes the typed ELF32 reader with the single x86 catalogue; `toolchain/cupiddis_main.c` is the hosted driver, while `kernel/lang/dis.c` is now only a VFS/job/text adapter. The existing `dis`, `exec -d`, and CupidC JIT call sites remain. The unused `dis_decode_one` surface and its second opcode authority were removed after repository-wide caller verification.

### Capability and interface decisions

- Design-It-Twice compared a one-call text-only interface, a fully normalized instruction document, and a CLI-first record sink. ADR 0008 records the selected two-operation seam: inspection returns an arena-owned typed report, then rendering streams selected text. The report retains shared file metadata for in-process consumers but does not allocate one record per decoded instruction. This keeps CLI text out of the architecture and bounds in-kernel memory.
- Raw input accepts only an explicit 16-bit or 32-bit mode and an explicit base address. One raw request has one mode; a mixed-mode flat image must currently be split into homogeneous ranges. ELF input accepts static little-endian i386 `ET_REL` and `ET_EXEC`. Relocatable code regions are executable `PROGBITS` sections; executable code regions are executable `PT_LOAD` entries so current sectionless CupidC/CupidASM files remain inspectable. Dynamic symbol tables and their relocation domains return an explicit unsupported diagnostic rather than being mislabeled malformed.
- The shared ELF writer stays deterministic `ET_REL`-only. Its reader now exposes file type, entry point, flags, checked program headers and borrowed program contents. This intentionally supersedes ADR 0002's read-side exclusion of program headers/executables without moving final executable layout out of CupidLD.
- Cupid text orders headers/program headers, sections, symbols, relocations, and code by serialized file order. The ELF reader keeps relocations target-grouped for constant-time section slices, while an arena-owned report index restores relocation-section/entry order for presentation. Matching relocations overlay decoded fields with symbol/addend text; retained `ET_EXEC` REL records recover their original addend from the applied field, symbol, and place for both `R_386_32` and `R_386_PC32`. Unknown and invalid bytes render as data and advance; a truncated tail renders once. Raw x86 decode never claims relocation ownership.
- The separate `--nm` view uses address plus serialized symbol index as a deterministic total order and emits GNU-compatible symbol letters. Its heap sort is bounded by `O(n log n)` so the future kernel-symbol path does not acquire quadratic behavior. The normal Makefile still calls GNU/LLVM `nm`; parity and boot/backtrace cutover remain a later coherent step.
- Hosted and kernel drivers deliberately raise the generic 4 MiB source ceiling to 64 MiB and the arena ceiling to 128 MiB. These are allocation ceilings, not eager reservations, and preserve inspection of the current roughly 6 MiB kernel ELF. No user clarification was needed: the approved ticket and existing shell/JIT behavior fixed the migration intent. `TempleOS/` remained read-only reference material and outside every metric.

### Test-first findings and failed approaches

- Red contracts first fixed raw 16/32 output, 16-bit short/near IP wrap with 32-bit override behavior, `ET_REL` header/section/symbol/relocation views, sectionless `ET_EXEC`, relocation overlays, address-sorted `nm` output, malformed requests/reports, unknown/truncated progress, stripped-symbol output, exact diagnostics, and failing sinks. CLI contracts fix default/all and explicit views, mandatory raw mode/base, distinct usage (2) versus processing (1) failures, and late buffered-stdout failure detection.
- The first malformed-ELF CLI run printed zeroed diagnostic text. CupidDis had rewound the outer job-arena mark after the ELF reader had transactionally rewound its own allocations and then copied a diagnostic. Removing that second rewind preserved nested-module diagnostic ownership while the failed report still zeros.
- A first symbol-order implementation used stable insertion sort. It passed the small fixture but was rejected for the real kernel-symbol scale and replaced with a deterministic heap sort using serialized index as the tie-breaker.
- The initial hosted limits inherited the 4 MiB generic source cap and could not load the current kernel ELF. Both real drivers now use the explicit larger inspection policy instead of weakening the inspected artifact.
- The synthetic executable fixture initially made defined-symbol bounds look stricter than real linker output permits. The current kernel tags zero-sized `_bss_start` as a `NOTYPE` symbol in the preceding `.ksyms` section after alignment. The read side now preserves such zero-sized `ET_EXEC` linker-boundary symbols while continuing to range-check functions, objects, relocations, and every borrowed content view; a focused executable contract locks the case.
- Host-`nm` parity exposed that undefined weak rows must omit their zero address; otherwise the existing symbol generator treats the weak import as a definition. Active relocatable objects also exposed unnamed `STT_SECTION` symbols and memory displacement relocations. CupidDis now renders the referenced section name while preserving memory brackets, and only overlays four-byte fields whose absolute/PC-relative intent matches the relocation kind.
- Independent bounds review found that 16-bit relative targets were formatted with unbounded 32-bit addition, short stdout remained buffered past a successful return, and the report ownership comment incorrectly called source-backed ELF names arena-owned. Target formatting now follows decoded operand width, the CLI flushes and checks stdout before success, and the public lifetime contract distinguishes arena arrays from every borrowed name/payload view.
- Further hostile/variant ELF probes removed non-format alignment policy from byte-decoded program/section tables, accepted standard entry sizes on zero-count tables, accepted section tables with `SHN_UNDEF` section-name storage while leaving names empty, and modeled defined `STT_TLS` values in static executables as TLS-block offsets rather than section VMAs. Metadata-only high-address views remain valid, while symbol/disassembly views diagnose any address sum that the 32-bit report cannot represent. Dynamic symbol tables remain a deliberate unsupported domain rather than being accepted into a single-table model.
- The first renderer rescanned every symbol at every instruction, linearly searched a section's relocation slice for every decoded field, and allocated temporary `nm` sort storage across caller callbacks. Reports now precompute only the indexes required by selected views: exact `nm` symbol order, serialized relocation order, target/offset relocation sites, and exact-count function-label order. Region rendering carries or binary-searches its label cursor and binary-searches relocation sites, render callbacks may safely allocate diagnostics in the same job arena, and metadata-only views do not pay for unused indexes. Real-kernel disassembly dropped from roughly 5.4 seconds in the review repro to roughly 1.6 seconds on the same host.
- The old kernel implementation cast hostile bytes to packed ELF structures, selected only the first load segment, and maintained a much smaller x86 table. It was not retained as a compatibility parser: read-side ELF validation, region selection, decode traversal, operand formatting, and relocation correlation now have one shared authority.

### Migration state

- `make -C toolchain all` now builds native core, ELF32, x86, and CupidDis contracts plus `toolchain/build/cupiddis[.exe]`. ELF input defaults to all implemented views; `--headers`, `--sections`, `--symbols`, `--relocations`, `--disassemble`, `--all`, and exclusive `--nm`/GNU-compatible `-n` are available. Raw use is `--raw --mode 16|32 --base ADDRESS FILE`.
- The root kernel links `toolchain/cupiddis.o`. Its adapter maps VFS/job status, existing JIT labels, diagnostics, and callback output without owning inspection semantics. A DEBUG boot self-test enters through the preserved `dis_disassemble` adapter, then writes a sectionless ELF fixture through VFS, calls the `dis_elf` command backend, verifies status/output, and unlinks it. Together they cover translated labels, raw/JIT decoding, file loading, VFS status mapping, executable rendering, and cleanup.
- Inspection semantics and frontend behavior are now Cupid-owned and host-runnable, but every new C source is still compiled by GCC/Clang. CupidDis self-build, DWARF v4, the remainder of the bounded x86 domain, and replacement of production `nm` remain open. NASM, the host linker, and objcopy roles are unchanged.

### Verification

| Command/check | Result | Evidence |
| --- | --- | --- |
| `make -C toolchain clean test` | PASS | Strict Windows Clang built the native `cupiddis` CLI and ran 25 core/ELF32/x86/CupidDis contract modes, including positive and negative executable/CLI prerequisites. |
| `make test` / checked audit | PASS | All 63 Windows host tests passed (one `/dev/full` case skipped there and passed in the 12-test WSL CupidDis suite); generated JSON/Markdown matched a 659-input, 248-feature, 458-transform graph with 432 declared artifacts covering all 425 linked objects. |
| WSL Linux GCC hosted build | PASS | The same shared sources, native CLI, and all 25 modes compiled with strict warning-as-error GCC and passed. |
| WSL Linux ASan/UBSan build | PASS | All 25 modes completed under address/undefined sanitizers with leak detection and no finding. |
| Strict freestanding objects and `make -j4 all WAD_SRCS=` | PASS | `toolchain/cupiddis.o`, the kernel adapter/self-test, all 425 final-link objects, both ELF links, raw kernel, and disk image built from a clean root. |
| Real artifact inspection | PASS | Hosted CupidDis decoded a separately extracted 512-byte stage-1 prefix of the 2,560-byte mixed-mode `boot/boot.bin`, inspected/disassembled the real NASM `isr.o` with `R_386_PC32` overlays, and read the roughly 6 MiB final kernel `ET_EXEC` header/program/section/symbol model. Whole-file mixed-mode raw inspection remains unsupported. |
| Kernel-symbol oracle parity | PASS | `tools.hostbuild._symbols_from_nm` returned the same ordered 3,756 text/weak symbol pairs for `llvm-nm -n` and `cupiddis -n` on the clean pass-1 kernel ELF. Production Make still selects the host oracle. |
| CupidC and CupidASM GUI smoke | PASS | `/bin/ls.cc` and `as /demos/hello.asm` each completed without panic; boot serial output included `CupidDis kernel adapter self-test passed` after the shared ELF32 and x86 tests. |

The post-commit Windows baseline recapture below records reproducibility, exact artifact hashes, sizes, and smoke durations. The complete Linux GCC/binutils OS oracle baseline remains pending; hosted GCC/sanitizer evidence is not a substitute.

## 2026-07-09: hosted-CupidDis Windows oracle recapture

After committing the shared inspector as `4efd5ed`, the baseline runner rebuilt that exact revision twice in isolated worktrees and refreshed `baselines/windows-amd64.json`.

| Check | Result |
| --- | --- |
| Reproducibility | PASS: all 432 artifacts matched between two isolated builds; aggregate SHA-256 `d9590ca71017c18842b910a174f8844bb1385cfd93cf883e5d09af5d34d4a133` |
| Manifest contract | PASS: the checked audit covers all 425 final-link objects, including `toolchain/ctool.o`, `toolchain/elf32.o`, `toolchain/x86.o`, `toolchain/cupiddis.o`, and `kernel/lang/ctool_kernel.o` |
| Clean builds | PASS in 9.017 and 7.690 seconds |
| Host tests | PASS: 63 tests in 19.731 seconds, with the Windows-only `/dev/full` case skipped and covered by the WSL suite |
| CupidC guest smoke | PASS: `/bin/ls.cc` in 18.620 seconds, after the CupidDis kernel adapter self-test |
| CupidASM guest smoke | PASS: `as /demos/hello.asm` in 23.352 seconds, after the same self-test |
| Output sizes | Kernel ELF 6,275,816 bytes; raw kernel 6,062,681 bytes; `.text` 1,394,020 bytes; disk image 209,715,200 bytes |

The machine-readable evidence fingerprints the Windows 10 AMD64 oracle and its Make, Python, Clang/LLD, NASM, LLVM objcopy/nm, QEMU, and optional JPEG tooling. Linux GCC/binutils remains a separate complete-OS capture; the hosted GCC and sanitizer contracts do not substitute for it.


## 2026-07-10: shared hosted CupidASM and real-source parity

Wayfinder ticket [#20](https://github.com/cupidthecat/cupid-os/issues/20) moved assembly semantics into a freestanding hosted frontend before changing production ownership. ADR 0009 records one deep `ctool_asm_assemble` operation with raw, deterministic ELF32 relocatable-object, and fixed code/data image profiles. The module owns lexing, `%define` and include expansion, expressions, symbols, section layout, branch relaxation, shared-x86 lowering, fixups, and artifact construction. It calls the shared x86 encoder and semantic ELF32 writer directly; it does not publish parser/fixup state or claim linker-script, multi-object, unrestricted `%macro`, or final-executable ownership.

### Capability and interface decisions

- `toolchain/cupidasm.h` accepts immutable request definitions, logical include roots, entry candidates, explicit artifact policy, and a caller-owned empty output buffer. Success returns borrowed bytes plus job-arena-owned fixed-region metadata; failure empties output and zeros the result while retaining structured diagnostics. Fixed regions publish ELF32 `SHF_*` flags, initialized byte slices, memory sizes including BSS, and code-before-data order.
- Symbols are case-sensitive by default for the NASM-compatible host path. A request flag enables ASCII-insensitive identity for the future kernel adapter, preserving the legacy CupidASM policy for source labels, entry names, and runtime definitions. Contracts prove mixed-case binding/entry resolution and reject `Foo`/`foo` as a duplicate only in that compatibility profile.
- `%include` tries the including source's logical parent followed by ordered request roots through the shared file-store seam. Missing files, cycles, excessive nesting, invalid requests, lexical/syntax failures, duplicate/undefined names, invalid expressions, layout/region limits, encoding, relocation, entry, and output failures have stable `CT6000001`-`CT6000017` diagnostics.
- `toolchain/cupidasm_main.c` provides `cupidasm -f bin|elf32 INPUT -o OUTPUT`, including relative and repository-contained absolute paths from the process working directory, distinct usage exit 2 versus processing exit 1, opening output only after successful assembly, and rendered structured diagnostics. The CLI is a host adapter only; parsing, layout, encoding, and object policy remain freestanding. A late host write/close failure may leave a partial destination and is not described as transactional publication.
- The source-driven surface includes mixed `BITS 16/32`, `ORG`, sections, global/extern, colon and no-colon data labels, dot-local labels, `equ`, `$`/`$$`, checked arithmetic/shift/complement expressions, binary-suffix literals, `db/dw/dd/dq`, `times`, `%define`, `%include`, all seven active reserve spellings, strings/escapes, prefixes, segment overrides, far operands, and the active integer/system/x87/SSE2 instruction forms. Branches begin short and widen deterministically when required.

### Real-source evidence

- Unmodified `boot/boot.asm` produces exactly 2,560 bytes with SHA-256 `57884f86c907d8669f16a667e83238b5f840f2b67e7b82eeeedea09ec5244445`; unmodified `kernel/smp/smp_trampoline.S` produces exactly 4,096 bytes with SHA-256 `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90`. Both are byte-identical to NASM 2.16.01.
- Unmodified `kernel/cpu/isr.asm` produces a 377-byte `.text` with SHA-256 `09434aeb25549b239065814f2fbbfbbe80c079cb56fe522ab1f79b18d3f93417`, 38 symbols, and five `R_386_PC32` relocations matching the NASM oracle's relevant semantics. Unmodified `kernel/core/context_switch.asm` produces a 44-byte `.text` with SHA-256 `6c926a52a59c328e65d3ca1dea1d4eb9355ea8b08478f27b5ea521d93ba980cb`, two globals, and no relocations.
- A checked table names all 22 unmodified `demos/*.asm` inputs and the exact required constant/absolute definition names and kinds, using deterministic synthetic addresses. Every source assembles twice to an identical fixed image with valid entry and bounded code/data regions. The next adapter step exercises the real 631-name kernel catalogue; the kernel command still uses the legacy parser until then.
- The build-graph lexer previously counted quoted bracket text and bracketed control directives as memory operands. Focused regressions correct 107 to 99 true memory operands while retaining seven `BITS` and two `ORG` occurrences. The generated source audit now classifies the new shared frontend, CLI, and contracts as CupidASM rather than generic host C.

### Test-first findings and rejected failures

- The first fixed-image run treated a resolved request constant as undefined because constant evaluation fell through into source-definition handling. Request constants now remain resolved expression atoms.
- A symbolic immediate such as `push message` initially chose a constant-width layout before object/fixed lowering knew it needed a reference field. Non-raw layout now retains symbolic references so final encoding cannot change size.
- The first whole-demo pass failed `fxsave [fp_buf]` because memory-only x87/SSE/system forms lacked source-width inference. Adding mnemonic-specific inference exposed a regression in `mov [BOOT_DRIVE], dl`: the mnemonic default incorrectly won over the companion 8-bit register. Direct raw and boot-image contracts now require the companion width first, with mnemonic defaults only when no operand supplies a width.
- Fixed requests initially read `initial_origin` through the inactive raw-profile union member, accidentally aliasing the code base. Non-raw profiles now start with origin zero; fixed label and `EQU` evaluation uses the label's actual section load address, invalidates pre-layout cached values, and is pinned by a data-label address fixture.
- The first hosted logical path split made relative paths use the source's directory as native root while retaining the directory in the logical name. Relative CLI inputs now use the working directory root; absolute inputs split once. Includes still resolve through logical source-parent/root policy rather than ambient CWD inside the assembler.
- The initial active-source audit classified quoted `"[... ]"` strings and `[BITS ...]`/`[ORG ...]` controls as memory syntax. The tokenizer now removes quoted content and known bracketed directives before counting operands. A regression fixture locks the corrected feature evidence.
- No production source was shortened or rewritten. The four NASM inputs and all 22 demos remain unchanged; capability was added to the toolchain instead.
- Independent Standards and Spec review found an absolute-path CLI include-root loss, fixed-image branch-place ambiguity, unchecked linearized arithmetic, included-file post-parse diagnostics pointing at the primary source, sparse negative coverage, and a private 64-token line ceiling. Absolute repository paths now retain the working logical root; fixed layout assigns section addresses before relaxation; linearized addition/multiplication/shifts report overflow; statements/symbols carry their source path into layout/emission; one error matrix covers invalid requests, undefined/non-relocatable/overflowing expressions, narrow relocations, invalid `ORG`, missing entry, output rollback, include cycles/depth, and included encoding errors; token storage is bounded only by the job arena. A 70-value source line proves the artificial ceiling is gone.

### Verification

| Command/check | Result | Evidence |
| --- | --- | --- |
| `make -C toolchain test` | PASS | Strict Windows Clang built eight native contracts/CLIs and passed 35 core/ELF32/x86/CupidDis/CupidASM modes, including the negative matrix, long-line contract, and all 22 demos. |
| Relevant Python suites | PASS | 22 CLI, active-source, demo-corpus, and build-graph-audit tests passed; absolute-path includes, raw bytes, and ELF section/symbol/relocation semantics matched their frozen/oracle evidence. |
| WSL Linux strict GCC hosted build | PASS | The identical shared sources and all 35 modes compiled warning-as-error and passed. |
| WSL Linux ASan/UBSan hosted build | PASS | All 35 modes, including two full demo-corpus assemblies per source, completed under address/undefined sanitizers with no finding. |

The production Makefile still invokes NASM for two raw binaries and two ELF32 objects. The kernel still links the legacy CupidASM lexer/parser/emitter, every new C source is still compiled by GCC/Clang, and no CupidASM checked seed or staged self-build exists. Kernel fixed-image adapter migration, strict i386/full-OS/guest proof, the four production recipe cutovers, and self-hosting remain separate green steps.

## 2026-07-10: shared CupidASM kernel JIT/AOT adapter

Wayfinder ticket [#20](https://github.com/cupidthecat/cupid-os/issues/20) completed the semantic migration started by the hosted frontend. The in-OS `as` and `cupidasm` commands now call the same freestanding `ctool_asm_assemble` operation as the native CLI. The legacy `kernel/lang/as_lex.c` and `kernel/lang/as_parse.c` implementations were deleted rather than retained as a second parser, expression engine, layout policy, or encoder authority.

### Kernel boundary and compatibility decisions

- `kernel/lang/as.c` is now a policy adapter. It owns the checked 631-entry case-insensitive runtime-definition catalogue, JIT-versus-AOT binding choices, VFS loading, the historical 0x01A00000 code and 0x01B00000 data placement, entry selection, executable-memory copying, diagnostics, and execution. The shared module owns source semantics and fixed-image construction. The catalogue preserves the legacy 8,192-entry ceiling, and the DEBUG self-test rejects accidental additions, removals, or duplicate spellings by requiring exactly 631 definitions.
- Kernel `%include` resolution first tries the including source's parent, then the shell CWD when it is not `/`, then the VFS root. These latter two paths are ordered request roots rather than hidden filesystem behavior. `/demos/include_feature.asm` is the boot self-test input so the production VFS adapter and included helper are exercised together.
- Fixed artifacts are validated before use: one required executable code region, at most one non-executable data region, bounded initialized and memory sizes, contiguous artifact bytes, an entry inside initialized code, and the established 1 MiB per-region limits. JIT zeroes each complete memory region before copying initialized bytes, so BSS remains represented without being stored in the artifact.
- `kernel/lang/as_elf.c` is now a pure checked buffer operation behind `as_elf32_exec_write`. It accepts a validated fixed artifact and serializes the existing sectionless i386 `ET_EXEC` compatibility format without performing VFS writes or retaining assembly state. Hosted contracts cover code-only, code/data/BSS, and malformed-artifact/output-rollback behavior. Final executable layout remains CupidLD ownership.
- Shell JIT state now records and snapshots explicit code and data regions. The compatibility wrapper `shell_jit_program_start` still supplies CupidC's historical regions, while CupidASM supplies its own two regions. Nested or suspended programs therefore restore the memory actually owned by the active frontend instead of assuming every JIT uses CupidC placement.
- The in-OS guide `cupidos-txt/10ASM.CTXT` was reverified against source on 2026-07-10. It now describes the shared module and thin adapters, active syntax, real limits, include order, runtime catalogue, JIT snapshots, and AOT wrapper rather than the deleted legacy parser.

### Test-first failure and runtime fix

- The first AOT guest smoke assembled the hello demo successfully, then the loader rejected its historical 1,048,598-byte mapped span. `exec` still imposed an unrelated 256 KiB whole-executable ceiling even though CupidASM deliberately places independent 1 MiB code and data regions next to one another. The loader ceiling is now 2 MiB: exactly the bounded span already permitted by those adjacent fixed regions, not an unbounded acceptance rule. Header/program-table validation, segment bounds, identity-mapped-address validation, and the per-region assembler limits remain in force.
- After the bounded loader fix, `cupidasm` wrote the hello executable, `exec` loaded it as a process and reported its PID, and the guest completed without panic. This is the first executed proof of the shared CupidASM AOT path rather than only a writer-format contract.
- Moving the wrapper behind a buffer-only operation exposed malformed-artifact cases that the old file-writing function could not test transactionally. The negative contract now proves an empty output remains empty for invalid region counts, flags, ordering, entry placement, sizes, and overlapping or inconsistent byte views.

### Migration state

- Hosted RAW, hosted ELF32 `ET_REL`, kernel fixed-image JIT, and kernel fixed-image-to-`ET_EXEC` AOT now share one parser, expression engine, symbol/layout model, and x86 encoder. The real kernel catalogue and VFS include behavior are checked at boot; the hello demo passed both JIT and AOT/`exec` guest paths.
- The compatibility AOT bridge still places every executable in the same global 0x01A00000/0x01B00000 slots. It proves single-image assembly/loading but does not provide concurrent per-process placement or final image-lifetime ownership; those remain explicit CupidLD/loader work rather than hidden assembler policy.
- The checked graph contains 664 active language inputs, 248 feature IDs, 467 transforms, and 36 accounted unreachable source-like files. Its 431 declared artifacts cover all 424 final-link objects. The supported roots contribute 434 root, seven user, and 26 hosted-toolchain transforms; the host C compiler still owns 270 outputs.
- No production assembly artifact changes owner in this adapter step. NASM still produces the boot image, ISR object, context-switch object, and SMP trampoline until issue #22 changes the four recipes and runs their boot, interrupt, scheduler, and SMP proofs. GCC/Clang still compiles the shared CupidASM source, kernel adapter, and temporary executable bridge; no checked seed or staged self-build exists.

### Verification

| Command/check | Result | Evidence |
| --- | --- | --- |
| `make -C toolchain test` | PASS | Strict Windows Clang built the hosted tools plus the kernel ELF bridge contract and passed all 38 core/ELF32/x86/CupidDis/CupidASM modes. The three new modes cover code-only, code/data/BSS, and malformed rollback. |
| WSL Linux strict GCC and ASan/UBSan hosted gates | PASS | Strict warning-as-error GCC passed all 38 modes. A separate build ran the same modes with address, leak, and undefined-behavior sanitizers enabled on every compile and link command; it reported no finding. |
| Strict Windows i386 object gate | PASS | `toolchain/cupidasm.c`, `kernel/lang/as.c`, `kernel/lang/as_elf.c`, the region-aware shell changes, loader change, and kernel self-test compiled under the root freestanding warning-as-error policy. |
| `make -j4 all WAD_SRCS=` | PASS | The complete root image linked all 424 final objects, including the shared CupidASM implementation and thin kernel bridge, and produced the kernel and disk image. |
| `make test` | PASS | All 76 repository tests passed in 40.386 seconds; one platform-specific case was skipped on Windows. |
| Checked build-graph audit | PASS | The generated JSON/Markdown match 664 active inputs, 248 feature IDs, 467 transforms, 36 unreachable files, and the 431/424 artifact coverage contract. |
| DEBUG kernel self-test | PASS | Boot assembled the real `/demos/include_feature.asm` plus its included helper through VFS and logged `CupidASM kernel adapter self-test passed (631 definitions)`. |
| CupidASM JIT GUI smoke | PASS | `as /demos/hello.asm` assembled and executed through shared fixed-image output, reached `JIT execution complete`, and produced no panic. |
| CupidASM AOT/loader GUI smoke | PASS after bounded loader fix | `cupidasm` assembled the hello demo to ELF32, `exec` loaded the result and reported a PID, and serial output contained no panic. The initial unchanged assembly had exposed the obsolete 256 KiB loader ceiling described above. |
| Four-CPU boot comparison | PRE-EXISTING FAILURE | With the repository's exact `-smp cpus=4` flags, both this tree and an isolated `d72a528` control build stopped after `acpi: MADT: 4 CPUs, 1 IOAPIC(s)` and never reached the CPU-online or desktop markers. The adapter therefore did not introduce the hang, but issue #22 must not claim the production SMP-trampoline cutover until a four-CPU boot succeeds. |

## 2026-07-10: shared-CupidASM Windows oracle recapture

After committing the kernel adapter as `50c5b04`, the baseline runner rebuilt that exact revision twice in isolated worktrees and refreshed `baselines/windows-amd64.json`.

| Check | Result |
| --- | --- |
| Reproducibility | PASS: all 431 artifacts matched between two isolated builds; aggregate SHA-256 `47f778f4e9b9a1954786aee1099f3aaf0a2ee1d357e82e1242fcc38a126d459d` |
| Manifest contract | PASS: the checked audit covers all 424 final-link objects, including `toolchain/cupidasm.o`, `kernel/lang/as.o`, and the new `kernel/lang/as_elf.o` bridge |
| Clean builds | PASS in 8.279 and 7.718 seconds |
| Host tests | PASS: 76 tests in 42.159 seconds, with one Windows-only skip |
| CupidC guest smoke | PASS: `/bin/ls.cc` in 18.625 seconds, after the shared toolchain kernel self-tests |
| CupidASM guest smoke | PASS: `as /demos/hello.asm` in 23.355 seconds, after the 631-definition adapter self-test |
| Output sizes | Kernel ELF 6,276,332 bytes; raw kernel 6,064,029 bytes; `.text` 1,390,468 bytes; disk image 209,715,200 bytes |

The machine-readable evidence fingerprints the Windows 10 AMD64 oracle and its Make, Python, Clang/LLD, NASM, LLVM objcopy/nm, QEMU, and optional JPEG tooling. Linux GCC/binutils remains a separate complete-OS capture; the passing hosted GCC and sanitizer contracts do not substitute for it.

## 2026-07-10: CupidObj production ownership cutover

Wayfinder ticket [#21](https://github.com/cupidthecat/cupid-os/issues/21) first transferred every normal-build object transformation from GNU/LLVM `objcopy` to a hosted CupidObj implementation. ADR 0010 records a single transactional final-artifact operation: it either wraps bytes directly into their requested final relocatable-object form or extracts the initialized flat range from a linked executable. A public generic object-edit graph was rejected because every production rename and section change is already known at wrapping time; direct final-form construction removes an intermediate parse/rewrite pass and keeps object policy behind one deep seam.

### Capability and migration decisions

- `ctool_obj_transform` follows the shared job/arena/caller-buffer contract. `WRAP_BINARY` emits one deterministic i386 `ET_REL` `PROGBITS` section with exact requested name, flags, alignment, global start/end symbols, and absolute size symbol, including an empty-payload case. `EXTRACT_FLAT` orders initialized `PT_LOAD` bytes by physical address, zero-fills gaps, excludes BSS, and provides a checked allocated-`PROGBITS` fallback for static executables without load segments. Failures restore an empty output and zero the result.
- The hosted `cupidobj wrap|flat` adapter accepts relative or absolute inputs, maps both native arguments into one canonical logical-path universe, and enters through `ctool_invoke`, so the source is job-owned, source/output limits apply, diagnostics carry canonical paths, and publication uses the job commit gate. It derives GNU binary symbol identity from the caller's input spelling, supports an explicit original identity or final symbol stem, and owns section/read-only policy without exposing ELF editing. Usage failures exit 2 and processing failures exit 1.
- The normal root graph now builds the hosted tool, uses it for 178 direct source/asset wrappers plus one JPEG-preprocessed wrapper, emits the SMP trampoline directly as read-only `.rodata` with `smp_trampoline_{start,end,size}`, and flattens the final kernel ELF. The JPEG helper preprocesses bytes into a temporary JPEG, then invokes CupidObj once with the original source identity; the former wrapper plus three-symbol rewrite pass was removed.
- GNU/LLVM `objcopy` is no longer selected or required by root `all`, `user:all`, or `toolchain:all`. It remains only in tracked legacy/oracle helpers outside the normal graph. The host C compiler still bootstraps CupidObj, so this is semantic and production-transform ownership rather than a checked-seed/self-hosting claim.
- The generated audit now records 668 active sources, 248 feature IDs, and 473 transforms. CupidObj owns 181 production transforms; the host C compiler owns 275, the host linker five, NASM four, and the host symbol reader one. All 431 declared artifacts still cover the 424 final-link objects.

### Test-first findings and behavior proof

- The first public contract failed to link because `ctool_obj_transform` did not exist. The first extraction tracer then exposed missing gap/BSS behavior. A final negative pass caught invalid-operation and reserved-section diagnostics with the wrong classifications; stable `CT8000001`-`CT800000B` cases now cover invalid requests/input/names, no-load, overlap, overflow, limits, unsupported forms, and rollback.
- Five C contract modes cover basic/model wrapping, determinism, empty payloads, load-segment flattening, section fallback, and failures. Five hosted CLI tests cover relative/absolute paths, default and explicit identities, final stems/sections/flags, deterministic output, sectionless executable gaps, external `readelf` acceptance, status classes, diagnostics, and preservation of an existing destination on pre-publication failures.
- The JPEG migration began with a failing test that observed two objcopy invocations. It now proves exactly one CupidObj call over the converted temporary bytes with `--identity` set to the original source path.
- Rebuilding the complete 424-object image after every wrapper and the final extraction changed no trusted artifact bytes: `kernel/kernel.elf` remained SHA-256 `88b8006dcc869618483e1b59f695e92f67877f14ce0580d20f9e28f8ac511c10`, `kernel/kernel.bin` remained `565c23edbc88ad4ea9a89704e84f542d384cd4f8bb00dd3439631c0c53a2397a`, and `cupidos.img` remained `027146215be35a7ef85576f549ec30d5bd105acfcecb96f3a35666aed877b8c9`. A second independent CupidObj flatten matched byte-for-byte.

### Verification

| Command/check | Result | Evidence |
| --- | --- | --- |
| Strict Windows Clang CupidObj contracts | PASS | All five core modes and the hosted CLI compiled under the repository warning-as-error policy and passed. |
| WSL strict GCC and ASan/UBSan CupidObj contracts | PASS | The same five modes passed under strict GCC and under address/leak/undefined sanitizers with no finding. |
| `python -m unittest tests.test_toolchain_cupidobj tests.test_hostbuild` | PASS | Nine CLI/helper tests passed, including external-format, error, and JPEG single-wrap cases. |
| `python -m unittest tests.test_build_graph_audit` | PASS | All 14 ownership/classification tests passed, including native CupidObj wrapper/flat ownership. |
| Repository host tests through this commit scope | PASS | All 82 tests in the CupidObj commit passed in 49.030 seconds; one platform-specific case was skipped on Windows. Subsequent CupidLD work was outside this coherent commit gate. |
| `make -j4 all WAD_SRCS=` | PASS | The complete root image rebuilt with all 181 production object transforms owned by CupidObj. |
| CupidC GUI smoke (`/bin/ls.cc`) | PASS | The rebuilt image reached `JIT execution complete` without panic in 18.2 seconds. |

CupidLD, the three user links, the two kernel links, production NASM cutover, and CupidDis symbol-reader cutover remain separate ownership steps. The post-commit reproducibility recapture remains pending.

## 2026-07-10: CupidObj Windows oracle recapture

After committing the production object-transform cutover as `6731dd6`, the baseline runner rebuilt that exact revision twice in isolated worktrees and refreshed `baselines/windows-amd64.json`.

| Check | Result |
| --- | --- |
| Reproducibility | PASS: all 431 artifacts matched between two isolated builds; aggregate SHA-256 `a1f4a1b10fd326318ad0c2b861a050ca95dc225d6aea95caa6e7864d8d6d5fdf` |
| Manifest contract | PASS: the checked audit covers all 424 final-link objects while assigning all 181 normal object transformations to CupidObj |
| Clean builds | PASS in 14.387 and 12.975 seconds |
| Host tests | PASS: 82 tests in 52.467 seconds, with one Windows-only skip |
| CupidC guest smoke | PASS: `/bin/ls.cc` in 18.882 seconds |
| CupidASM guest smoke | PASS: `as /demos/hello.asm` in 23.246 seconds |
| Output sizes | Kernel ELF 6,276,332 bytes; raw kernel 6,064,029 bytes; `.text` 1,390,468 bytes; disk image 209,715,200 bytes |

The evidence still fingerprints the installed LLVM `objcopy` as historical oracle context because the baseline preflight catalogue has not yet made that probe optional; none of the three captured normal build roots invoked it. The complete Linux GCC/binutils OS oracle remains pending, and hosted GCC/sanitizer contracts do not substitute for it.

## 2026-07-10: CupidLD production ownership and executable-image lifecycle

Wayfinder ticket [#21](https://github.com/cupidthecat/cupid-os/issues/21) implements the linker ownership transfer begun by the CupidObj step. CupidLD now owns both `link.ld` kernel passes and the three fixed-address user-program links. No standalone host ELF-linker recipe produces an OS/user executable; the host C compiler still invokes its native linker backend while bootstrapping the hosted Cupid tools, and standalone ELF linkers remain optional semantic oracles. The same step made the loader and process subsystem own the lifetime of fixed-address images instead of treating their pages as independently releasable allocations.

### Linker interface and layout decisions

- `ctool_ld_link` is a one-shot transactional operation over an ordered list of static little-endian i386 `ET_REL` objects. Its two explicit profiles are a fixed-text layout for small hosted programs and the tracked `link.ld` subset for the kernel. Parsing, resolution, merge interning, relocation, layout, and ELF serialization remain private job state; success publishes one complete caller-owned buffer and every failure rewinds per-call scratch.
- Structured diagnostics no longer borrow rewindable source-arena storage. Each job owns a bounded diagnostic record plus path/message slots until job close. Thirty-two repeated low-arena failures preserve the exact arena mark and all rendered diagnostics, after which the same job links a valid object successfully.
- The hosted CLI implements `-m elf_i386`, `-T`, `--text-address`, `--entry`, and `-o`. Usage errors are distinct from processing failures. Root Make preserves the exact `KERNEL_OBJS` order for each link, declares `link.ld` as a prerequisite, and uses one ordinary shared host-tool build target so parallel Make cannot race over shared hosted objects. The user Makefile selects Clang or GCC cross-platform, compiles to `ET_REL`, and invokes CupidLD at `0x00D00000`.
- The exercised script language includes `ENTRY`, `SECTIONS`, location-counter assignment, exact and wildcard input selectors, `ALIGN`, output and boundary symbols, `COMMON`, comments, and repeated `ASSERT`. Failed assertions preserve their supplied diagnostic text. `link.ld` rejects a file-backed image past `0x008FF600` and a complete memory image past the fixed stack at `0x00B00000`.
- Strong, common, weak, undefined, local, absolute, and script symbols are resolved with i386 cdecl/ELF semantics. CupidLD applies `R_386_32` and `R_386_PC32`, emits page-congruent RX/R/RW load segments plus a non-executable `PT_GNU_STACK`, and retains inspectable sections, symbols, and strings. CupidDis was extended to report `GNU_STACK` and truthful input `M`/`S` merge/string flags; CupidLD treats those flags as link-time input semantics and clears them on final allocated output sections whose combined model has no single entry size.
- Compatible unrelocated `SHF_MERGE` atoms are interned in exact first-occurrence order; any atom overlapped by a relocation remains distinct. Reproducing LLD 22's internal hash iteration was rejected as an undocumented implementation accident; semantic comparison instead requires identical layout, atom multisets, symbol values, and relocation results after accounting for the intentional permutation. Suffix merging remains unimplemented because no active input requires it.

### Merge-relocation failure found by the oracle

The first exhaustive comparison masked the six intentional merge-payload permutations and their derived relocation words, but still left one semantic failure. `kernel/doom/src/g_game.o` defines the local object `.Lswitch.table.G_DoCompleted` in `.rodata.cst16` and relocates a table access with addend `-4`. CupidLD incorrectly mapped the already adjusted input offset `S-4`; because interning had reordered adjacent 16-byte atoms, the instruction addressed unrelated data rather than `mapped(S)-4`.

A minimized contract arranges merge atoms as `C,B,A`, defines an object symbol on `B`, and relocates it with `-4`. It failed in 0.6 seconds before the fix. CupidLD now maps combined `S+A` only for `STT_SECTION`, where the addend selects an input-section byte; named object/function symbols map `S` first and then apply `A`. Corpus classification confirms the rule: 7,536 merge relocations resolve through `STT_SECTION`, 61 through `STT_OBJECT`, and none through `STT_NOTYPE`, function, or TLS definitions. The new contract, the prior section-symbol contract, and all 39,093 real relocations pass after the fix.

Independent review found three more correctness gaps. Identical bytes carrying different relocations were being interned and losing distinct target values; relocated atoms now remain separate while unrelocated atoms still intern. COMMON storage aligned only a section-relative size; it now aligns the absolute `output.address + output.size` and advertises a larger `sh_addralign` only when the output base truthfully satisfies it. Allocated inputs with unimplemented types or INIT_ARRAY, FINI_ARRAY, TLS, or EXCLUDE semantics were silently flattened; they now return the explicit unsupported-input diagnostic. Focused red/green merge-addend, unaligned-script COMMON, and negative-section contracts pin all three decisions.

### External executable arena and process ownership

- Ordinary hosted-C/CupidLD images use the permanently PMM-reserved `[0x00D00000, 0x00F00000)` arena. `0x00600000` was rejected after the measured kernel showed that it overlaps initialized image state. CupidC `[0x01000000, 0x01900000)` and CupidASM `[0x01A00000, 0x01C00000)` remain separate permanent legacy arenas.
- The ELF loader accepts only static ELF32 i386 `ET_EXEC`, current ident/header versions, known program-header types/flags, checked file ranges, power-of-two/congruent alignment, non-overlapping `PT_LOAD` ranges wholly inside one accepted arena, and an entry in file-backed bytes of a `PF_X` load. It reads and validates a page-zeroed heap staging image completely before touching fixed memory or claiming a lease.
- One generation-tagged external lease prevents two fixed-address processes from overwriting each other. A successful `process_create_with_arg_image_ex` consumes the descriptor into the PCB before PID publication. Failure before the claim has no lease side effect; process-creation failure discards an unconsumed generation; permanent CupidC/CupidASM descriptors are range-checked and are never returned to the PMM.
- Exit, remote kill, and stack-canary termination only mark a PCB terminated. Cleanup runs under the BKL after scheduler detachment proves `on_cpu == 0xFF`; then the reaper frees the stack and releases exactly the consumed external generation. `context_switch.asm` loads target ESP and FPU state before releasing the scheduler's sole BKL acquisition on the target stack, then restores the target interrupt policy without a second unlock. Only detached READY PCBs can migrate, PID 1 is BSP-only, and every AP captures a PID-less CPU-local scheduler context so a terminated AP task can detach without stealing the BSP idle stack. Nested switches become CPU-local pending requests for the outer unlock; remote kill publishes that bit before IPI delivery and the real handler consumes it after EOI without re-arming. NASM and CupidASM produce the same 73-byte `.text`, SHA-256 `25b78f4c2cbf3dfadc6dc87a9731a097bfd9df0675534d8449c24d890114fbfa`, with one `R_386_PC32` handoff relocation at `0x21`.

### Interrupt, AP, and storage failures found by guest recapture

The first post-handoff AOT smoke recursively page-faulted because common IRQ entry replaced the per-CPU `%gs` selector before interrupt-side BKL use. Both common stubs now reserve the saved-GS slot, keep the live CPU selector across C, increment/decrement `interrupt_depth`, finish the handler and IRQ EOI, consume pending rescheduling at common exit, and discard the source CPU selector slot after any migration. Loading the saved selector would corrupt `this_cpu()` when a frame resumes on another CPU. CupidASM and NASM match at 417 text bytes, SHA-256 `bcf582569c26029d5143ec42f6de24388596c412bca2b4a672608800fe2606e3`, 41 symbols, and eleven `R_386_PC32` relocations. The full suite exposed and corrected two stale fixtures that still expected five ISR relocations.

The first four-vCPU smoke then raised `#UD` in compiler-generated `movsd` inside `timer_get_ticks`. CR0/CR4 SSE enablement is CPU-local, but only the BSP had run `fpu_init`. A red contract now requires `ap_main_c` to call `fpu_init_cpu` before LAPIC ID lookup, per-CPU setup, or logging. That hardware-only helper is compiled with `target("no-sse,no-sse2")`, performs CR0/CR4, `fninit`, and `ldmxcsr` setup without logging, and is shared by BSP initialization. Disassembly confirms the AP's first C call is the non-SSE helper.

A newly formatted 200 MB SMP image still reported a HOMEFS flush failure, disproving the initial disk-full explanation. Every AP's LAPIC timer uses vector `0x20`, so the generic timer handler invoked the block-cache callback on all CPUs while the BSP imported HOMEFS. Interspersed AP flushes clobbered shared cache metadata and the ATA PIO command ports; the failing valid LBA reported ready/no-DRQ status `0x50`. Cached reads, writes, flushes, and sync now hold the BKL, serializing cache state and cached PIO transactions across CPUs while disabling same-CPU interrupt re-entry. Three source contracts pin the guarded paths, and a fresh four-vCPU image completes both HOMEFS writes and CupidC JIT with no ATA, cache, panic, or exception marker.

The timer design remains explicit debt: the nominal five-second callback still executes on every LAPIC tick in hard-IRQ context, causing BKL contention, and raw exported block-device/ATA calls bypass the cache guard. A correct follow-up needs frequency-aware deferred work outside interrupt context.

### Harness and persistence findings

- AOT persistence initially appeared flaky because hard process termination could race QEMU disk writeback. The harness now sends monitor `quit`, waits for the disk-flushing exit, and uses terminate/kill only as fallbacks. Each final AOT file is executed after a separate reboot.
- Failed invocations were retained: a repeated-exec pattern expected the command text instead of a process record; Windows stripped embedded quote patterns; a greedy DOTALL expression merged exits; one `ccc` command omitted `-o` and correctly used `/bin/ls`; a single-value `--stage` flag was given two paths; and waiting for lease release before issuing the second `exec` deadlocked against the intentionally lazy reaper. Line-bounded exit patterns and separate staging corrected those harness errors.
- Repeated leading keys and long AOT commands can be dropped by USB HID polling. The smoke CLI now exposes positive `--key-pause` and `--smp` options; final AOT and repeated-exec gates use a 0.60-second key gap and graceful shutdown.

### Full-corpus and artifact evidence

- The final pass-one corpus has 423 objects, entry `0x00100000`, loaded end `0x006C1F34`, and memory end `0x00AE2910`. Pass two has 424 objects, loaded end `0x006D8361`, and memory end `0x00AF9910`. Section addresses, sizes, alignments, load permissions, all six program headers, and the 3,789-entry `t/T/w/W` symbol projection match LLD 22.1 exactly. Final allocated sections deliberately clear input-only `M`/`S` metadata instead of copying LLD's combined `.rodata` flags with a zero entry size.
- Both passes apply 39,131 input relocations: 27,606 `R_386_32` and 11,525 `R_386_PC32`. The exhaustive pre-review oracle classified its complete relocated-word corpus; the final refresh repeats deterministic full-corpus links, exact LLD allocated layout and projected-symbol comparison, focused merge/addend/COMMON/error contracts, and byte-identical NASM/CupidASM ISR and context-switch oracles.
- Pass-one ELF/raw SHA-256 values are `7f19088f2762c4141833442b2c7941dd25b1b63a7beaa08205ba7ed2e85e48f1` and `744cc0842299c8e8c5f25f936e81314cf7466ca98edd0fc1780f3e8ac5390f51`. Final ELF/raw values are `bc1844e3e306c8662b8ca166025707e44b916101de8daf1a5a846091e192b312` and `8c82aef834a43e61796d4ca6f5f24537626d7a69d4c5fc36cc69ea89d4de99eb`; the production outputs match the second independent CupidLD/CupidObj run exactly. CupidLD, LLD, and production generate the same 558,613-byte ksyms C source, SHA-256 `38b57a10fdffb1fc915febe6099b1cea5b23da6c5ec859254a19741bccc35a90`; the serialized blob is 91,165 bytes.
- `hello`, `ls`, and `cat` each rebuild twice to deterministic 12,656-byte ELF files. Their SHA-256 values are `2d783d4ac3400820253497fbbd5d784647b32b14baae8daefd1a939a0874c3eb`, `aa942961bc2a9e565c5983910ca5a28184209df177aafacfaf8b1da0b9d8c7a9`, and `a2c01d0ae096c9f82c23e9a5cdfc971c7c69196af9b66debcb538fe4a7328096`. CupidDis accepts all three and reports RX/R/RW loads, allocated read-only `.rodata`, BSS, and RW `GNU_STACK`.
- The checked graph contains 672 active inputs, 248 feature IDs, 480 transforms, 39 accounted unreachable source-like files, and exact coverage of 431 declared artifacts/424 linked objects. Ownership is CupidLD 5, CupidObj 181, host C compiler 280, NASM 4, host symbol reader 1, Python 8, and Make recursion 3; host ELF-link and objcopy transform ownership is zero. The tracked-state count is three higher than the pre-commit capture because `kernel_exec_contract.c`, `kernel_process_contract.c`, and their support header are test-only fixtures outside the three supported production Make roots; all three are explicitly classified rather than silently omitted.

### Independent review

Standards and specification reviews ran independently against fixed point `48af6d2`. Standards review found relocation-blind merge interning, section-relative COMMON alignment, silent unsupported allocated-section semantics, and an unrelated `sync-demos` path change; all four were corrected and re-reviewed green. Specification review found shared PID1/AP idle ownership, lack of a terminated-AP detach target, and unsafe generic-interrupt migration/GS restoration; the detached-candidate gate, BSP-only PID1, CPU-local idle contexts, interrupt-depth safe point, and GS-slot discard resolve them. Final review accepted the implementation after requiring this evidence/doc recapture. The later AP FPU and SMP cache/ATA failures were found by that recapture and received their own red contracts and fresh guest proof before commit.

### Verification

| Command/check | Result | Evidence |
| --- | --- | --- |
| `make -C toolchain test` | PASS | The complete strict Windows Clang suite passed, including all six CupidLD modes, negative diagnostics, and all 22 CupidASM demos. |
| WSL GCC and sanitizer toolchain gates | PASS | A clean strict GCC build passed the complete suite. A second clean build visibly compiled and linked every hosted contract/CLI with ASan/UBSan, ran with leak detection and halt-on-UB, and reported no finding. An earlier retry that lost sanitizer flags at the Windows/WSL quoting boundary is recorded as an invocation failure, not sanitizer evidence. |
| `python -m unittest discover -s tests -p "test_*.py"` | PASS | 142 tests ran in 61.785 seconds: 141 passed and the Windows-only `/dev/full` case was skipped; that path is covered by the Linux hosted-core run. |
| Kernel loader/process/SMP contracts | PASS | 48 positive, negative, and production-source cases pass under Windows Clang and WSL GCC. Coverage includes malformed loader rollback, all arenas/leases, failure ownership, local/remote quiescence, PID1/AP idle ownership, BKL handoff/nesting/IPI ordering, generic interrupt depth and GS discard, AP-local FPU setup, cache/ATA guarding, stack-canary cleanup, and permanent-page non-release. |
| `make -j4 WAD_SRCS= all` | PASS | The complete 424-object image built with both production links owned by CupidLD and final flattening owned by CupidObj. |
| Full linker oracle | PASS | Two deterministic Cupid links per pass match production and LLD's six program headers, allocated-section layout, and all 3,789 projected text/weak symbols. CupidLD/LLD ksyms sources are byte-identical; 39,131 input relocations and all review-driven merge/COMMON/unsupported cases are covered. |
| Repeated external guest execution | PASS | A fresh image loaded `/home/hello` twice. The log shows two exits, lease generation 1 released before generation 2 was claimed, no busy error, and no panic/corruption marker. |
| CupidC/CupidASM JIT | PASS | Four-vCPU `/bin/ls.cc` and UP `as /demos/hello.asm` each reached `JIT execution complete`. The SMP log shows four CPUs online, two complete HOMEFS writes, scheduler start, and no failure marker. |
| CupidC/CupidASM AOT | PASS | `ccc /bin/ls.cc -o /home/cctest` persisted a 1,111-byte ELF after a complete 397-cluster/1,622,442-byte HOMEFS write; the next boot loaded it at `0x01000000` and observed process exit. `cupidasm` persisted a 166-byte `/home/asmtest` after a complete 397-cluster/1,622,631-byte write; the next boot loaded it at `0x01A00000` and observed exit. All accepted guest logs are free of ATA/cache, panic, exception, corruption, and incomplete-write markers. |
| Checked active-source audit | PASS | Generated JSON/Markdown match the current supported graph and declare `link.ld` for both kernel links. |

### First post-cutover baseline attempt

The detached recapture at `857b6c7` completed its clean 142-test host suite (141 passed, one platform skip), then failed the first GUI smoke because the 0.35-second default HID gap delivered `/bin/l.cc` instead of `/bin/ls.cc`. This matched the already observed long-command input loss and did not indicate a compiler or kernel failure. A red baseline-contract assertion now requires both GUI checks to pass `--key-pause 0.60`; the focused six-test baseline module passes. The failed manifest remains non-authoritative, and recapture must restart from the committed timing fix so both isolated builds and both GUI smokes describe one revision.

Recapture from the committed fix `92b3684` passed. Two clean isolated builds produced the same 431-artifact digest, `5f141dadf52069b8fda711533cc8d694191479b5e5a0681744fa24bb5ab79e5d`, with no mismatch. The first isolated build passed the 142-test host suite (141 passed, one platform skip) plus CupidC and CupidASM GUI smokes; the second independently rebuilt and rehashed the complete manifest. The checked JSON is 252,942 bytes with SHA-256 `03bfe0543e639ede5a410cc8fa9bbb93191dd6bc3311b14bf06384b02c7dbef2`. Canonical-LF baseline quality values are 1,396,388 kernel text bytes, 6,252,832 kernel ELF bytes, 6,078,269 flattened kernel bytes, and a 209,715,200-byte image. Timings remain recorded non-gating observations.

Normal-build OS/user link and object-transform ownership is now Cupid-native, but the fixed point is not reached. GCC/Clang and their native linker backends still build every hosted Cupid executable, the compiler owns 280 outputs, NASM still owns four production assembly transforms, and GNU/LLVM `nm` still owns one kernel-symbol extraction transform. CupidC host extraction/self-build, CupidASM Make cutover, CupidDis symbol-reader cutover, checked seeds, staged bootstrap equivalence, and the Linux full-OS baseline remain open.

## 2026-07-10: CupidASM production ownership and NASM retirement

Wayfinder ticket [Migrate the OS assembly build off NASM](https://github.com/cupidthecat/cupid-os/issues/22) transfers the four active assembly transforms without changing any assembly source. Root Make now builds the hosted CupidASM executable first, uses it for the boot image, ISR object, context-switch object, and SMP trampoline, and serializes the three hosted-tool recursive builds around their shared object directory. The normal graph has no `ASM`/NASM fallback. `make nasm-assembly-oracle` retains the installed external assembler only as an optional comparison path.

The supported build and baseline interfaces now distinguish production ownership from oracle evidence. The active-graph auditor recognizes CupidASM as a code-producing assembler, classifies both raw and ELF32 operations, and assigns the four source inputs to the CupidASM runtime owner. Baseline preflight no longer fails when NASM is absent; it records NASM separately under `optional_oracle_tools`. The checked graph contains 672 active inputs, 248 feature IDs, 481 transforms, and 39 accounted unreachable source-like files. Tool ownership is CupidASM 4, CupidLD 5, CupidObj 181, host C compiler 280, host symbol reader 1, Python 8, and Make recursion 4; normal NASM, host ELF-linker, and host object-copy ownership are zero.

### Red/green ownership contracts

- The baseline preflight contract first failed because `assembler` was still required, then passed after moving NASM to optional oracle discovery.
- A synthetic build-graph contract first reported a `$(CUPIDASM)` recipe as `host_shell`, then passed after the auditor gained typed CupidASM ownership and raw/ELF32 operation classification.
- The repository-level graph contract first found all four expected outputs owned by NASM, then passed after the Make cutover. It now rejects any normal-build NASM transform and pins the four exact output/operation pairs.
- No source feature was weakened or rewritten. The 2,560-byte boot image and 4,096-byte trampoline remain byte-identical to NASM. The ISR and context-switch ELF containers deliberately use Cupid's deterministic table layout, while their code bytes, alignment, symbols, bindings, and all twelve combined `R_386_PC32` relocations match the oracle semantics.

### Verification and remaining host debt

| Command/check | Result | Evidence |
| --- | --- | --- |
| `make nasm-assembly-oracle` | PASS | Both active raw and ELF32 source suites passed against installed NASM. |
| Focused Python suites | PASS | 42 CupidASM CLI/source/ELF32, build-graph, and baseline tests passed. |
| `make -C toolchain test` | PASS | All 49 strict Windows Clang contract modes passed, including all 22 unchanged demos. |
| Kernel loader/process/SMP contracts | PASS | All 48 cases passed, including the produced ISR/context-switch invariants. |
| Clean Windows normal build with `ASM` and `NASM` poisoned | PASS | The full 424-object image completed; four assembly outputs came from the freshly built hosted CupidASM. |
| Four-vCPU CupidC guest smoke | PASS | Four CPUs reached online state, scheduler startup completed, the CupidASM kernel self-test passed with 631 definitions, and `/bin/ls.cc` reached JIT completion without an accepted failure marker. |
| UP CupidASM guest smoke | PASS | `as /demos/hello.asm` reached `JIT execution complete`. |
| Checked active-source audit | PASS | Generated JSON/Markdown report four `cupid_assembler`, zero normal NASM, and complete 431-artifact/424-link-object coverage. |
| `make test` | PASS | All 145 repository tests ran in 76.715 seconds: 144 passed and the Windows-only `/dev/full` case was skipped; the checked audit also matched. |

One full WSL GCC attempt is retained as a failed approach rather than claimed as Linux evidence. With `ASM` poisoned, Linux successfully built the hosted CupidASM and began the root graph, then GCC 13.3 stopped on the pre-existing `fpu_init_cpu` `target("no-sse,no-sse2")` diagnostic under `-Werror`. The source and failure are unchanged from the prior revision and unrelated to assembly ownership. [Capture the Linux oracle bootstrap baseline](https://github.com/cupidthecat/cupid-os/issues/23) remains responsible for resolving that host-C portability blocker and freezing complete Linux evidence.

Independent standards and specification reviews finished green. The first specification pass found that baseline provenance honored a configured `NASM` command while two optional oracle suites still resolved PATH independently. One shared command parser/resolver now supplies both the fingerprint and both executions, including configured arguments and explicit paths; the re-review confirmed the mismatch closed. The standards pass also led to one shared hosted-tool prerequisite list and the same command resolver instead of three drifting copies.

The post-cutover Windows reproducibility capture must run from the committed implementation, because the baseline runner intentionally ignores working-tree changes. GCC/Clang plus their native linker backend still bootstrap hosted Cupid tools, and GNU/LLVM `nm` still owns kernel-symbol extraction. NASM is no longer a normal-build dependency.

## 2026-07-10: CupidASM Windows oracle recapture

The baseline runner rebuilt committed cutover revision `0969300` twice in isolated Windows worktrees and refreshed `baselines/windows-amd64.json`.

| Check | Result |
| --- | --- |
| Reproducibility | PASS: all 431 artifacts matched; aggregate SHA-256 `c5fd401aff3dd914d72d3b8b2fff97d6f89df0dcf5fcb8e7de905b745c0b2f55` |
| Clean builds | PASS in 18.364 and 17.169 seconds |
| Host tests | PASS: 145 tests in 74.943 seconds, 144 pass and one Windows-only skip |
| CupidC guest smoke | PASS: `/bin/ls.cc` in 20.487 seconds |
| CupidASM guest smoke | PASS: `as /demos/hello.asm` in 25.598 seconds |
| Quality | Kernel `.text` 1,396,388 bytes; ELF 6,252,832 bytes; raw kernel 6,078,269 bytes; disk image 209,715,200 bytes |
| Evidence file | 252,773 bytes; SHA-256 `bd896f8219e9127098b21de5c1a2eaa8e3b9e15e4a927ab3b15dd778c4bb15f8` |

The captured required-tool set no longer contains an assembler; NASM 2.16.01 is fingerprinted only under `optional_oracle_tools`. Relative to the preceding Windows evidence, exactly four artifacts changed: the 696-byte context-switch object, the 1,892-byte ISR object, and the pass-one/final linked ELF containers. The boot image, SMP trampoline, flattened kernel, disk image, all sizes at loaded boundaries, and every other manifest artifact remain byte-identical. This is the expected distinction between Cupid's deterministic ELF metadata layout and NASM's container layout, not a runtime payload change. The complete Linux oracle remains pending on its separate roadmap ticket.

## 2026-07-10: Linux host portability and hermetic test gates

The first implementation step for [the Linux oracle baseline](https://github.com/cupidthecat/cupid-os/issues/23) makes the existing freestanding source build under both supported host compiler families without weakening the AP/FPU invariant. GCC 13.3 rejected `fpu_init_cpu` because the per-function `no-sse,no-sse2` target inherited translation-unit `fpmath=sse`; the resulting contradictory option state warned that GCC was falling back to x87, and the repository's `-Werror` policy stopped the build. `target("general-regs-only")` is accepted by GCC 13.3 and Clang 18/22, prohibits compiler use of x87/MMX/SIMD registers, and still permits the two intentional post-enable `fninit` and `ldmxcsr` inline instructions. A production-source code-generation contract now rejects FP/SIMD registers, implicit x87 instructions, and runtime helper calls before the CR4 write, then requires CR4, `fninit`, and `ldmxcsr` in order. This closes the less obvious case where a future floating expression could lower to a soft-float helper call without naming an FP register.

The next clean GCC build reached CupidLD and exposed allocated `.eh_frame` input that Clang's i386-unknown-ELF path had not emitted. The kernel and fixed-address user programs have no CFI unwinder, so all freestanding C recipes now explicitly disable synchronous and asynchronous unwind tables instead of teaching the linker to retain unusable metadata. Both strict kernel and relaxed vendored flags, plus the separate user-program flags, share that policy. A dry-run contract pins it on representative strict, vendored, and user objects.

The isolated Linux test run found two host-environment failures unrelated to OS semantics. A user-installed regular Python package named `tools` displaced the repository's former namespace-package directory; a tracked `tools/__init__.py` and a hostile-package subprocess regression make imports checkout-local. Two build-audit fixtures used shell-dependent `echo` quoting and emitted `[main.o]` under POSIX `sh`; they now use the current Python interpreter and `json.dumps`, matching the production manifest seam on both Windows and Linux.

### Red/green evidence

- The new FPU compiler contract first reproduced GCC's `SSE instruction set disabled, using 387 arithmetics` failure, then passed on WSL GCC 13.3 and Windows Clang 22 with identical safety ordering.
- The first post-FPU clean link failed explicitly on `kernel/core/kernel.o` section 11, `.eh_frame`; the new freestanding recipe contract failed for strict, vendored, and user C before the flags were added. A clean 424-object GCC build then completed both CupidLD passes, CupidObj flattening, and the 200 MB image with `ASM` and `NASM` poisoned.
- The package-isolation test first imported `/home/frank/.local/lib/python3.12/site-packages/tools`; both platform runs now resolve `tools/bootstrap_baseline.py` from the checkout.
- Both audit fixtures first failed on Linux with `did not emit a JSON string list`; the focused two-platform cases now pass.

| Command/check | Result | Evidence |
| --- | --- | --- |
| WSL repository Python suite | PASS | All 149 tests passed in 140.038 seconds. |
| WSL clean root build, host assemblers poisoned | PASS | GCC built the complete 424-object image; CupidASM owned all four production assembly transforms. |
| WSL `make -C user clean all` | PASS | All three freestanding user programs compiled with no unwind metadata and linked through CupidLD. |
| WSL `make -C toolchain clean test` | PASS | Every strict hosted contract mode and all 22 demo assemblies passed under GCC. |
| Windows clean root build, host assemblers poisoned | PASS | Clang accepted the shared portability flags and completed the full image. |
| Focused Windows/Linux FPU, package, recipe, and audit contracts | PASS | All new positive contracts pass on both supported hosts after recording their failing state. |

This is source/build portability evidence, not the oracle capture itself. The next committed step must run both isolated builds, all three supported roots, host tests, and guest smokes from one revision before Linux baseline status changes from pending.
