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

The first implementation step for [the Linux oracle baseline](https://github.com/cupidthecat/cupid-os/issues/23) makes the existing freestanding source build under both supported host compiler families without weakening the AP/FPU invariant. GCC 13.3 rejected `fpu_init_cpu` because the per-function `no-sse,no-sse2` target inherited translation-unit `fpmath=sse`; the resulting contradictory option state warned that GCC was falling back to x87, and the repository's `-Werror` policy stopped the build. `target("general-regs-only")` is accepted by GCC 13.3 and Clang 18/22, prohibits compiler use of x87/MMX/SIMD registers, and still permits the two intentional post-enable `fninit` and `ldmxcsr` inline instructions. A production-source code-generation contract now rejects FP/SIMD register use and runtime helper calls throughout the helper, rejects implicit-stack x87 instructions before the CR4 write, then requires CR4, `fninit`, and `ldmxcsr` in order. This closes the less obvious case where a future floating expression could lower to a soft-float helper call without naming an FP register.

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
| Four-vCPU CupidC guest smoke | PASS | All APs completed the CPU-local FPU/SSE enable path and `/bin/ls.cc` reached JIT completion without a panic or exception marker. |
| Focused Windows/Linux FPU, package, recipe, and audit contracts | PASS | All new positive contracts pass on both supported hosts after recording their failing state. |

This is source/build portability evidence, not the oracle capture itself. The next committed step must run both isolated builds, all three supported roots, host tests, and guest smokes from one revision before Linux baseline status changes from pending.

## 2026-07-10: Three-root baseline and cross-host gate

The reproducible recorder now treats the active-source audit's three supported Make roots as one bootstrap evidence cohort. Every isolated run cleans and builds root `all`, `user:all`, and `toolchain:all`; each root publishes its final artifact list, and the recorder stores both per-root manifests and one combined 447-path digest. The original 431-path OS manifest remains intact inside the root record and still covers all 424 final-link objects. User evidence adds three CupidLD executables, while hosted-tool evidence adds all thirteen contract and CLI executables. Because `build.artifacts` now names the combined cohort instead of the former root-only cohort, the baseline schema advances from v1 to `cupid.bootstrap-baseline.v2`; comparison tests explicitly reject v1 evidence. This closes the former gap where tests happened to build hosted tools and never built user programs, but neither supplemental root was named or hashed by the baseline.

Git is now an explicit required orchestration probe and the exact resolved/configured command owns revision resolution and disposable worktrees. The C compiler probe must produce a little-endian ELF32 `ET_REL`/`EM_386` object with a valid header using the configured `CC_TARGET`; a version command, arbitrary nonempty file, or wrong-target object fails preflight before an expensive build. Linux host metadata also records `/etc/os-release` identity, so WSL kernel provenance no longer hides the Ubuntu user space.

The new `bootstrap-host-comparison`/`check-bootstrap-host-comparison` workflow compares checked Windows and Linux evidence only when both describe the same revision. The hard gate requires the canonical Windows Clang/LLVM and Linux GCC/GNU-binutils families, recomputed two-run reproducibility over structurally and cryptographically validated manifests, every supported-root check in both runs, run-one host tests and both guest smokes, an identical platform-neutral logical artifact cohort, all four canonical size metrics, and the fixed 200 MiB disk geometry. Windows hosted `.exe` suffixes make physical-path equality observational. Cross-toolchain SHA-256 equality is likewise observational rather than gating; the comparison records both aggregate digests and absolute/percentage quality deltas. Canonical comparison evidence uses schema `cupid.bootstrap-host-comparison.v1` at `baselines/windows-linux.json`.

### Red/green and verification evidence

- New contracts first failed because Git, the functional compiler probe, distribution metadata, supported-root manifests, and host comparison operation did not exist. Positive/negative cases now prove a real output object is required, differing host bytes are accepted, and mismatched revisions, artifact paths, failed checks, failed reproducibility, or stale comparison JSON are rejected.
- The first full in-place recorder rehearsal exposed a relative-checkout normalization bug: treating `.` as the worktree path rewrote every period in artifact names. A focused test reproduced `artifact.o` becoming `artifact<WORKTREE>o`; `_run_command` and `capture_build` now canonicalize the checkout before output normalization or hashing.
- Independent correctness review found that a literal physical-path comparison could never accept Windows `.exe` and Linux extensionless hosted tools, that recorded `reproducibility.matched` and arbitrary digest strings were trusted, and that the compiler probe accepted five arbitrary bytes. Logical IDs now normalize only the hosted executable suffix, the gate validates per-root/combined structure and SHA-256 aggregates then recomputes every run comparison, and the compiler probe parses the actual target header. Invalid UTF-8 evidence now returns the controlled comparison error instead of a traceback.
- A full Windows rehearsal passed all nine build/artifact checks, hashed 447 paths across all three roots, passed the complete host suite, then passed the CupidC and CupidASM guest smokes. The equivalent Linux build/hash rehearsal passed all three clean GCC roots and produced a 447-path digest.

| Command/check | Result | Evidence |
| --- | --- | --- |
| Bootstrap recorder unit/CLI contracts on Windows and Linux | PASS | All 21 positive, negative, collision, ELF capability, manifest-integrity, supported-root, comparison, and stale-evidence cases passed on each host. |
| WSL complete repository suite | PASS | All 161 tests passed in 131.918 seconds. |
| Windows full in-place baseline rehearsal | PASS | Root build 15.752 seconds, user build 0.227 seconds, hosted-tool build 3.066 seconds, host tests 75.329 seconds, CupidC smoke 21.152 seconds, CupidASM smoke 25.142 seconds; 447 artifacts hashed. |
| Linux three-root build/hash rehearsal | PASS | Root GCC build 25.593 seconds, user build 0.132 seconds, hosted-tool build 4.493 seconds; all 447 declared artifacts hashed. |
| WSL ASan/UBSan hosted toolchain gate | PASS | Every contract mode and all 22 demo assemblies passed with address/leak/undefined checks and halt-on-error enabled. |
| Required tool capability probes | PASS | Windows Clang 22 and WSL GCC 13.3 each produced the real freestanding i386 probe object; Git, Make, Python, `nm`, and QEMU also resolved and fingerprinted. |

This step establishes the recorder and gate but deliberately does not claim checked oracle evidence from a dirty tree. The next step must commit this workflow, capture Windows and Linux twice from that exact commit, generate the cross-host comparison, and freeze all three JSON files.

### First exact-capture finding: hosted PE reproducibility

The first exact capture of workflow revision `1c1ef02` passed on Linux: both clean GCC builds produced the same 447-artifact manifest. Windows passed all three builds, the complete host suite, and both guest smokes, but correctly failed the capture because all thirteen hosted toolchain executables changed hashes between runs while retaining identical sizes. A minimized comparison found a one-byte difference in each executable's PE/COFF `TimeDateStamp`; the 22 freshly compiled COFF objects likewise carried wall-clock timestamps. This was a real bootstrap reproducibility defect, not a permissible cross-host difference, so the failed Windows JSON is not authoritative and both hosts must be recaptured from the corrected revision.

The hosted-tool Makefile now gives every Windows link step LLD's `-Wl,/Brepro` mode. The flag is deliberately link-only: placing it in shared `CFLAGS` makes strict compile-only Clang commands fail on an unused linker argument. Linux GCC links remain unchanged. A red contract first crossed the timestamp boundary and observed different hosted executable hashes; it now relinks the complete thirteen-file cohort byte-identically and clean-rebuilds a representative tool from fresh timestamp-varying objects with the same result. The complete strict hosted contract suite also passes under Windows Clang and Linux GCC. Zeroing input COFF timestamps was investigated but rejected as unnecessary for the recorded final-artifact contract: reproducible LLD output already removes their nondeterministic effect without altering compilation.

### Exact Windows/Linux baseline and comparison evidence

Corrected revision `1e079d1` is the first checked three-root oracle baseline on both supported hosts. Each isolated run declares and hashes 431 root artifacts, three CupidLD user executables, and thirteen hosted Cupid contract/CLI executables. Windows Clang/LLVM reproduced aggregate SHA-256 `fc3e626f85780e4973b57a010528e4e3e59d72c63c54cc3701e61936555bc960`; Linux GCC/binutils reproduced `38bd2192e3d973b8c5b03d04ea69ed4397769913931ef0127c8fe8fee0536c0d`. Every root/build/list check passed in both runs. Run one on each host also passed the 162-test repository suite and the explicit CupidC `/bin/ls.cc` and CupidASM `as /demos/hello.asm` guest smokes.

The first corrected Windows attempt ran concurrently with the Linux QEMU capture. Its clean builds and host suite passed, but resource contention coincided with the terminal receiving `/bin/ls.c` instead of `/bin/ls.cc`; CupidC reported the incomplete command and the harness timed out. That failed JSON was retained only long enough to diagnose the exact input loss, then replaced by a resource-isolated Windows capture. No timing constant or OS/compiler behavior was changed for that environmental retry. The isolated capture passed both guest smokes and both full artifact builds.

The checked `windows-linux.json` gate passes at the same source revision with 447 common logical artifact IDs and no failures. Physical paths differ because only Windows hosted tools use `.exe`; aggregate hashes and compiler-specific kernel sizes also differ and remain non-gating observations. Disk geometry is identical at 209,715,200 bytes. Windows records `.text`/ELF/raw sizes of 1,396,388/6,252,832/6,078,269 bytes; Linux records 1,078,952/5,947,852/5,770,439 bytes. The Windows, Linux, and comparison evidence-file SHA-256 values are `63a9a7ea060f640f88b3c6add7bba242ee49d16a43702c71c8a658ffdc3c91c3`, `ffa89f0132397dbd68ff838225a2a35cb651f4211dc1ca6b5c5533de96a1f454`, and `459da66b60684a44c9e1bccb62c2e7d08f931fa91a7b24f5d87d426a9e78fe1f`.

The checked active-source audit was regenerated after the reproducible-link change. Its source counts, feature inventory, 481 transforms, ownership totals, 431-path root manifest, and 424 final-link objects are unchanged; the machine record now fingerprints the modified hosted Makefile and all thirteen link recipes with their explicit `LDFLAGS`/`HOST_REPRO_LDFLAGS` inputs. Both audit and host-comparison drift checks pass.

Ticket #23 therefore establishes a reproducible Linux GCC/GNU oracle and a checked behavioral/quality comparison with the Windows Clang/LLVM oracle. It does not remove the host compiler: GCC/Clang plus their native linker backends still bootstrap all hosted Cupid tools, and GNU/LLVM `nm` still owns kernel-symbol extraction. Those are the next ownership boundaries, alongside checked seeds and staged CupidC self-hosting.

## 2026-07-10: typed CupidC preprocessing foundation

The first implementation step for [the active preprocessing contract](https://github.com/cupidthecat/cupid-os/issues/24) establishes `toolchain/cupidc_pp.*` as a freestanding module above `ctool_job_t`. Its one public operation borrows a primary source plus ordered include/macro configuration and returns an immutable job-owned token tape. Tokens retain exact post-splice spelling, canonical logical path, physical line/column, and an effective pack-alignment field reserved for the pragma slice. On any failure the result is zeroed, every operation allocation is rewound, and the independently owned structured diagnostic survives.

The lexer implements CR, LF, and CRLF normalization; escaped-newline removal; C line and block comments as token separators; identifiers and preprocessing numbers; prefixed string/character literals; and longest-match C punctuators. A token can cross a CRLF splice while remaining one contiguous spelling with its original start location. The implementation stores one normalized source image and only the normalized offsets of removed newlines. A per-character origin table was rejected as unnecessary arena pressure, while raw-source token slices were rejected because a splice can make one token from noncontiguous input bytes.

This does not claim the active preprocessing contract complete. Include traversal, ordered forced includes and macro actions, directive handling, recursive macro expansion, conditional expressions, built-ins, pack/once pragmas, Cupid `#exe`, kernel integration, and the 642-C-family-input proof remain assigned to the same ticket. The existing kernel preprocessor is unchanged, and no OS source was weakened.

Independent Standards and Spec review rejected the first temporary unsupported boundary because it treated every hash/digraph token as a directive and reported valid configured inputs as invalid requests. The lexer now tracks logical-line start and leading-space state privately: whitespace and comments preserve/reset line-start correctly, phase-two splices do not introduce space, only `#`/`%:` at logical-line start enter the unsupported directive path, and `#`, `##`, `%:`, and `%:%:` elsewhere retain their exact longest-match spellings. Command macros, forced includes, and language predefined-macro uses each return a specific unsupported diagnostic instead of being ignored. The same review moved canonical-path validation into the Toolchain core and added proof that both borrowed contents and borrowed path storage can be mutated without changing a successful result.

### Red/green and verification evidence

- The hosted contract first failed because `cupidc_pp.c` did not exist. It now proves escaped-newline/comment translation phases, exact token kinds and spellings, physical locations, and natural pack state.
- A negative contract feeds an unterminated string after valid prefix tokens, requires the precise lexical diagnostic at `/invalid.c:2:1`, verifies the arena returned to its entry mark with no partial tape, then successfully preprocesses a token crossing a CRLF splice in the same job.
- Four focused Python contracts pass under strict Windows Clang and WSL GCC. The complete strict hosted toolchain suite passes, and all 166 repository tests pass on Windows with only the expected `/dev/full` platform skip. `make check-bootstrap-audit` passes after regenerating the supported graph.
- The graph now contains 675 active inputs, 248 feature IDs, and 484 transforms. The new module/header/contract add two native objects and one hosted executable, bringing host-C ownership to 283 outputs and the current three-root artifact cohort to 448. Checked cross-host evidence remains the prior 447-artifact `1e079d1` oracle until the complete ticket is recaptured from one committed revision.

## 2026-07-10: object macros, conditional groups, and transactional includes

The second issue #24 slice moves ordered preprocessing state behind the typed tape. Command actions execute in request order, forced headers run after them and before the primary source, and source `#define`/`#undef` share the same macro namespace. Object replacements recursively rescan while an explicit disabled-macro stack terminates self and mutual recursion. Redefinitions are accepted only when their token sequence is equivalent; differing definitions fail with the macro name location. Function-like definitions remain an explicit unsupported boundary for the next slice.

Quoted includes search the including file's parent before roots flagged `QUOTED`; angle includes search only roots flagged `ANGLE`, preserving request order. Each successful source is tokenized once and cached as an immutable raw tape, then reprocessed under current macro/conditional state on every include. Cache membership is deliberately not `#pragma once`: the contract proves a guardless header emits twice while reading once, guarded mutual recursion terminates through macros, and a caller-supplied primary can include itself from its seeded cache without a file-store read. Conditional stacks remain file-local; inactive branches perform no include I/O. The 64-nested-include depth limit exceeds the audited 12-edge maximum chain and diagnoses unguarded recursion instead of rejecting active-path duplicates.

Host comparison reversed two initially plausible implementations. Clang and GCC deliberately do not parse operands of a conditional nested under an inactive parent or an `#elif` skipped after a taken branch, so CupidC now updates nesting only in those cases; requiring an identifier or expression there would reject accepted C. Direct `<...>` header names are recognized contextually before comment decomposition, so spaces and text such as `/**/` inside the delimiters belong to the requested filename. Reconstructing an angle name from globally comment-stripped tokens was rejected because it silently changed filenames. The private lexer instead emits one exact raw header-name token only after a logical-line directive marker and `include` name.

Positive contracts cover command define/undef order, identical redefinition, undefine-then-redefine, a forced definition, collision-backed parent/root precedence and form filtering, exact contextual angle names containing spaces or comment spelling, object alias rescanning, `ifdef`/`ifndef`, basic `if`/`elif`/`else`, ignored operands in inactive/skipped conditions, guarded header and primary recursion, included-file locations, and repeatable source caching. Negative contracts cover missing/malformed includes, invalid/differing macro definitions, active malformed and unmatched/unterminated conditionals, and unguarded include depth with exact status/code/path/location, zero results, arena rollback, and same-job diagnostic survival. The new read-only `ctool_job_limits` view keeps request validation under the job's configured policy: oversized primary paths/contents, include paths, and command macro names/replacements fail as limits before operation allocation, retain a stable diagnostic, and allow successful reuse of the same job.

Strict Windows Clang and WSL GCC pass the complete hosted Toolchain suite. A direct WSL GCC build of the preprocessing contract passes all seven modes with address, leak, and undefined-behavior sanitizers, and GCC `-fanalyzer` reports no finding. An earlier Make-based sanitizer attempt lost all flags after `-std=c11` at the Windows/WSL quoting boundary and only built artifacts; it is not counted as sanitizer evidence. All 169 repository tests pass on Windows with the expected `/dev/full` platform skip, and the regenerated active-source audit drift check passes. The checked audit records 675 active inputs, 249 feature IDs, and 484 transforms with unchanged artifact ownership and complete 431/424 root coverage. The kernel still uses its private preprocessor, so this step transfers semantics into the shared module but not production ownership.

### Phase-two macro inventory correction

The generated preprocessing inventory previously applied function-macro and operator regular expressions one physical line at a time. That missed two definitions whose parameter lists are spliced across lines and missed continuation-line `#`/`##` operators, undercounting the exact acceptance corpus even though the broader roadmap named each feature. The scanner now removes escaped newlines into logical directive lines before masking comments and literals, while retaining the first physical line for evidence. Masking after splicing is required: masking first would incorrectly count a `#define` on the line after a spliced `//` comment.

A red fixture combines a spliced parameter list, continuation-line paste/stringify/GNU comma elision, and a deliberately hidden macro after a spliced line comment. All C feature and directive detection now consumes the same post-splice, post-mask logical lines, preventing both phantom directives and ordinary tokens from skipped comment text. A focused physical-line contract proves LF/CRLF equivalence, preserves a bare backslash at end of file, and refuses to treat vertical tab or form feed as a C newline. The green audit reports the directly checked active totals: 184 function-like definitions across 39 files, 13 variadic definitions across six files, three stringify operators, 13 paste operators, and six GNU comma elisions. All 17 build-graph audit tests pass on Windows and WSL; the complete 170-test Windows repository suite passes with the expected `/dev/full` skip; and the regenerated 675-input/249-feature/484-transform records pass their drift check. These counts define the next function/variadic macro contract rather than weakening any source to the earlier undercount.

## 2026-07-10: non-variadic function macros and sequence rescanning

The third issue #24 implementation slice replaces the private one-token object expander with one arena-owned token-sequence worklist. An object replacement is prepended ahead of the caller suffix, so an alias that produces a function name can consume the following argument list. Function definitions retain their ordered parameter spellings, parse up to a bounded 256 parameters, and substitute pre-expanded arguments into a replacement that is rescanned with the unconsumed caller tokens. This covers the audited 22-parameter spliced `X86_FORM` shape without imposing a source rewrite.

Each private work token carries an immutable macro hide set. Object expansion unions the invoking token's set with the macro; function expansion uses the C rescan intersection of the macro-name and closing-parenthesis sets, then adds the invoked macro. Call-stack disabling alone was rejected because it loses the unavailable-macro state when a replacement is joined to caller suffix tokens. The same sequence representation carries argument tokens through nested calls, preserves argument source locations, assigns invocation locations to replacement literals, and propagates the invocation's pack state. Parameters absent from a replacement are deliberately not pre-expanded: both supported host preprocessors accept an unused malformed nested invocation, whereas eagerly expanding every parsed argument would reject valid input.

The positive contract covers zero-parameter and empty replacements, an unused function name, the 22-parameter spliced definition, nested and parenthesized-comma arguments, object aliases becoming calls, ordinary prescan/rescan, empty and repeated arguments, an unused malformed argument, commas introduced during prescan, both retaining and dropping hide-set members at the rescan intersection, equivalent same-spelling redefinition, self/mutual recursion, a literal `...` in an ordinary replacement, exact locations, and result ownership. Redefinition equality compares whitespace-separation positions while ignoring the amount of whitespace at an existing separation, matching strict GCC and Clang. The negative contract pins malformed/duplicate parameters, the reserved `__VA_ARGS__` spelling in non-variadic macro names, parameters, and replacements, strict C11 same-spelling/whitespace redefinition, too few/many arguments, unterminated invocation, and explicit unsupported diagnostics for the pending valid variadic/stringify/paste slice. Every failure leaves a zero result, rewinds the arena, retains its diagnostic, and permits same-job recovery.

Replacement-token parameter indices and a used-parameter bitmap are compiled once when a function macro is defined. Re-searching the full replacement and all parameter spellings for every argument at every invocation was rejected after a bounded 256-parameter/10,000-replacement-token probe exposed quadratic repeated work. Invocation is now linear in the parameter list, replacement list, and substituted output; the new scale mode exercises that declared parameter boundary.

All ten focused modes and the complete hosted Toolchain suite pass under strict Windows Clang and WSL GCC. A separate strict WSL GCC build passed all ten modes with address, leak, and undefined-behavior sanitizers; GCC `-fanalyzer` and Clang's static analyzer reported no finding. The first WSL sanitizer invocation lost the requested flags after `-std=c11` at the Windows/WSL quoting boundary and only built default artifacts without running the modes, so it is recorded as an invocation failure and excluded from sanitizer evidence. The complete Windows repository suite passes all 173 tests with the expected `/dev/full` platform skip, and all 17 build-graph audit tests pass independently under WSL. The regenerated 675-input/249-feature/484-transform audit and its drift check pass. Independent Standards and Spec re-reviews are green after correcting whitespace-sensitive redefinition, reserved `__VA_ARGS__` handling in source/configured object and function macros, literal-ellipsis handling, hide-set intersection coverage, and the bounded-work substitution plan. Variadics, stringification, token paste/GNU comma elision, full conditional expressions, predefined macros, pragmas, Cupid `#exe`, active-corpus proof, and kernel integration remain in issue #24.

## 2026-07-10: variadic macros, stringification, paste, and GNU omission

The fourth issue #24 slice implements the exact remaining macro-operator inventory: 13 standard variadic definitions, three stringification operators, 13 paste operators, and six GNU comma-elision sites. A variadic macro owns its named slots plus one synthetic `__VA_ARGS__` slot. Argument collection preserves all top-level separators after the named slots in one raw variable pack, so braces do not incorrectly shield the 1–10 variable arguments in the active X86 manifests. Each slot records whether it was syntactically omitted and receives a pre-expanded view only when an ordinary replacement occurrence needs one. Standard mode distinguishes a supplied empty pack from the invalid omission after named parameters; GNU mode follows GCC by accepting omission for every variadic shape.

Stringification consumes the raw slot, trims edge whitespace, collapses inter-token whitespace, and escapes quotes/backslashes inside literal tokens into one arena-owned string token. Paste-adjacent parameters also remain raw; empty operands become private placemarkers. Paste resolves definition-owned `##`/`%:%:` left to right, intersects real operand hide sets, preserves the surviving side across a placemarker, concatenates spellings, and re-lexes exactly one complete preprocessing token before caller-suffix rescan. Object macros may paste and retain literal `#`; function macros accept both standard `#`/`##` and `%:`/`%:%:` spellings. Pasted operators are ordinary output tokens and never execute as a second paste pass. The implementation normalizes the first surviving replacement token to the invocation's spacing, preserving nested stringification such as `XSTR(a+A)` versus `XSTR(a A)`.

GNU comma deletion is intentionally narrow: only a definition-owned comma followed by paste and the synthetic variable slot is special, and only a syntactically omitted pack removes it. Explicit empty, comment-empty, and a present macro that later expands empty retain the comma. The raw variable operand must not be pre-expanded before this decision: a review oracle nested the result inside stringification and disproved the first eager implementation. Ordinary omitted substitution follows GCC (`call(fmt,__VA_ARGS__)` retains its comma), while Clang currently chooses a different extension result; the request's `gnu_extensions` flag makes the policy explicit. Named GNU `args...` remains unsupported because the active tree has zero definitions.

The contracts cover raw versus expanded stringify, literal escaping/whitespace, variadic stringify, identifier/number/punctuator/string-prefix paste, both placemarker sides, multi-paste replacements, generated-operator ownership, object/configured paste, digraph directives/operators, hide-set termination, pasted function names consuming caller suffixes, GNU omitted/explicit/macro-empty packs at the end and middle of lists, exact generated/argument locations, mutation-proof ownership, rollback/recovery, and active-source arities. A bounded mode pastes two 16 KiB identifiers, stringifies 32 KiB, and forwards 2,048 variable tokens without a timing assertion. Another mode preprocesses both real X86 `.inc` manifests and proves all 185 + 129 calls through the new variadic engine; those transform inputs are not separate `sources[]` entries in the graph audit, so this direct contract closes the discovered call-shape evidence gap.

Review-driven whitespace oracles exposed four places where deleting a preprocessing token could delete a required separator. Empty ordinary arguments and whole empty object/function replacements now carry their separation to the next caller token; a surviving internal or terminal placemarker carries it to the next material token or replacement tail. GNU comma elision intentionally does not carry the deleted comma's spacing, and a present raw variable operand does not inherit whitespace around `##`. Active `#ifdef __VA_ARGS__` and `#ifndef __VA_ARGS__` now use the same reserved-identifier diagnostic as emitted text. Mixed raw/ordinary parameter use, numeric-suffix paste, phase-two-spliced operators, the active 1+11 variable-argument maximum, and the multiline whitespace-before-call packed-struct shape all received direct positive anchors.

The audit scanner now recognizes digraph directive markers and `%:`/`%:%:` operator spellings as well as literal `#`/`##`. Character lookarounds were rejected after adjacent `###` and `%:%:%:` spellings proved whitespace-sensitive; a linear longest-match operator scan now counts both tokens, and `%:include` participates in include closure. Active production totals remain 184 function definitions, 13 variadic definitions, three stringify operators, 13 paste operators, and six GNU comma elisions because no current production definition uses digraphs. The first active-manifest runner used cwd-relative logical paths and failed from the supported `make -C toolchain` entrypoint. Embedding the manifests was also rejected: concatenated host literals exceeded the strict C minimum and the helper itself would have inflated the audited macro surface. The final contract accepts an explicit host root from each runner, reads the real manifests through the file adapter, and retains deterministic logical paths without copying corpus data.

Clang's analyzer identified three implicit nullable-array paths in private argument/substitution-plan code. The construction invariants already excluded them, but relying on that inference was rejected. Explicit internal-error guards now make the allocation and function-macro plan invariants executable; both Clang's analyzer and GCC `-fanalyzer` report no findings.

Final verification is green: all 15 focused preprocessor tests; the complete strict hosted Toolchain suite under Windows Clang and WSL GCC; all 15 modes in a fresh WSL GCC address/leak/undefined-behavior sanitizer build; all 17 audit tests on Windows and WSL; and the complete 178-test Windows repository suite with its one expected `/dev/full` skip. The regenerated audit remains 675 active inputs, 249 feature IDs, and 484 transforms with exact 184/13/3/13/6 macro-operator totals, and `make check-bootstrap-audit` passes. Independent Standards, semantic Spec, and active-corpus re-reviews are green. No OS source or TempleOS reference file was changed.

## 2026-07-10: C11 conditional expressions and reproducible predefined macros

The next issue #24 slice replaces the basic zero/nonzero conditional shortcut with a bounded C11 integer-expression evaluator. Macro-expanded preprocessing tokens feed the full unary, multiplicative, additive, shift, relational, equality, bitwise, logical, and conditional precedence ladder. Remaining identifiers become zero. Decimal, octal, hexadecimal, character, and opt-in GNU binary literals use Cupid's target contract: 64-bit two's-complement `intmax_t`/`uintmax_t`, signed-overflow diagnostics, unsigned wrap, arithmetic negative right shift, signed 8-bit ordinary characters, left-to-right four-byte multicharacter packing, signed 32-bit `wchar_t`, and unsigned 16/32-bit `char16_t`/`char32_t`. Universal-character names enforce scalar, surrogate, and low-code-point restrictions independently from numeric escape ranges.

Logical and conditional operators still parse skipped operands but suppress arithmetic faults there. The C exception for comma expressions is represented explicitly: a comma may contribute the type of an unevaluated branch, while a selected/evaluated comma is diagnosed. Scratch expansion and parser nodes rewind after every condition, the ordinary expansion-node counter is restored, flat operator chains are iterative, and structural nesting stops at 256. Contracts cover a 10,000-term chain, the exact nesting boundary, integer/suffix/range errors, short circuit, common signed/unsigned types, GNU literals, and useful transactional recovery.

The first `defined` implementation scanned and interpreted raw expression tokens before outer macro expansion. Host comparison disproved it: an unused malformed argument such as `IGNORE(defined())` must disappear without validation, while substitution that produces a `defined` operator enters C's undefined-behavior boundary. Each private work node now records whether `defined` came directly from the conditional source. Argument prescan preserves it without interpretation, ordinary substitution and every paste path clear it, and only a surviving source operator at outer expansion is consumed. CupidC deterministically rejects macro-produced `defined`; the identifier is also reserved against source/configured definition and undefinition while remaining legal as a parameter name.

All seven C11 language predefined macros are now preprocessor-owned in both C11 and Cupid modes. `__FILE__` and `__LINE__` use invocation provenance across primary, forced, and included sources; function-like header macros exercise the same path as every active `ASSERT`, `ASSERT_MSG`, `Z_ChangeTag`, and `kmalloc` use. Generated file spellings escape quotes, backslashes, and control/non-ASCII path bytes with unambiguous octal escapes. `__DATE__` and `__TIME__` do not read a host clock: the request accepts validated Gregorian `Mmm dd yyyy` and `hh:mm:ss` values, and an omitted seed uses `Jan  1 1970` / `00:00:00`. Source and configured attempts to alter any language macro fail transactionally. Presumed locations through `#line` remain an explicit unsupported boundary.

The active audit now tokenizes conditional operands after phase-two splicing and comment masking, failing closed on an unrecognized preprocessing token. It records 97 active `#if` and four `#elif` occurrences, 21 normalized expression spellings, and 22 distinct directive/expression pairs. A checked manifest carries those exact counts plus the freestanding i386/little-endian truth profile. The C contract consumes that manifest directly to generate both `#if` and `#elif` probes; the Make dependency and audit tests prevent the inventory, expected profile, and compiler acceptance corpus from drifting independently.

Review caught and corrected three false-confidence paths before freeze. Clang's analyzer could not infer several parser success postconditions, so values/lists are initialized and closing-delimiter pointers are checked explicitly. A stale temporary object initially made one file-escaping probe appear green; a fresh build exposed that the test had restored a mutated leap-day seed to `Jeb`, not a product failure, and the corrected ownership probe now passes. Directly hand-copying 21 active expressions was also rejected in favor of the checked audit manifest. Macro-expanded include operands, `#pragma once`, pack-state transitions, `#line`, Cupid `#exe`, full-corpus preprocessing, and kernel ownership remain in issue #24; no OS source was weakened.

Final verification is green: all 22 focused preprocessor tests; the complete strict hosted Toolchain suite under Windows Clang and WSL GCC; all 22 preprocessor modes in a fresh WSL GCC address/leak/undefined-behavior sanitizer build; Clang static analysis and GCC `-fanalyzer`; all 21 audit tests on Windows and WSL; and all 189 Windows repository tests with the one expected `/dev/full` skip. A 540-expression differential matrix plus targeted `defined`, lazy-comma, character-range, paste, stringify, and predefined probes matched GCC and Clang under the documented Cupid target policy. The regenerated 675-input/249-feature/484-transform audit and `make check-bootstrap-audit` pass with the 97/4/101 conditional contract. The first sanitizer runner used a Bash `$mode` loop across the PowerShell boundary; interpolation removed the mode and it exercised only the final manifest case, so that invocation is excluded. The explicit PowerShell-driven rerun executed and passed every mode. No OS source or TempleOS reference file was changed.

The workspace Markdown auto-save pushed documentation commit `153a55c` while this implementation was still under test. Pushed history remains forward-only; the coherent implementation/test/audit commit follows that auto-save rather than rewriting remote history.

## 2026-07-10: canonical once and translation-unit pack pragmas

The next issue #24 slice implements the complete active pragma surface without rewriting any OS source. The phase-two active-source contract finds exactly five lexical sites outside `TempleOS/`: `bin/ctxt.cc` has one unconditional `#pragma once`, while `bin/fat16.h` and `kernel/fs/fat16.h` each have one balanced `#pragma pack(push, 1)` / `#pragma pack(pop)` pair. The generated contract records normalized actions and alignments, maximum pack depth one, zero underflow/unmatched pushes, and exact path/line evidence; it fails closed on empty, unknown, vendor, malformed, or `_Pragma` forms. An ephemeral stdin assertion harness included each unchanged FAT16 header while supplying the target integer typedefs and ran both `clang` and WSL `gcc` with `-m32 -std=c11 -ffreestanding -fsyntax-only -pedantic -Werror`; it confirms why the pair is behavior-critical. The packed MBR, FAT16 boot-sector, and directory-entry records retain their 16/512/36/32-byte disk-wire layouts and byte-aligned offsets, while the post-pop filesystem/file records return to natural four-byte ILP32 alignment.

Source-cache membership remains independent from once semantics. Every cache entry privately records whether it has ever been entered and whether an active once directive marked it. The canonical, case-sensitive logical path is the identity, so `once.h` and `./once.h` converge after path resolution while distinct paths with identical bytes remain distinct. A primary source may mark itself and then include itself safely; forced, ordinary, and primary entries share one operation-local table. Inactive once has no effect, a guardless cached header still replays, and a new preprocess operation starts with no sticky state.

Pack state is translation-unit-global across forced includes, ordinary includes, and the primary source. The private grammar implements `pack()`, `pack(n)`, `pack(push)`, `pack(push,n)`, named push with or without a cap, pop, and nearest-name pop for caps 0, 1, 2, 4, 8, and 16. Decimal, octal, hexadecimal, and standard integer-suffixed spellings are accepted. Operands are not macro-expanded: GCC 15 rejects a defined macro operand while Clang 22 expands it, and Cupid deliberately chooses the deterministic GCC-compatible policy until active source requires otherwise. Named pop removes its matched frame and all newer frames. Live depth stops at 256, and popped arena frames move to a free list; a constrained 256 KiB contract replays one cached forced header 16,384 times, which would exceed the arena even with a 24-byte frame if reuse regressed. Each copied direct token receives the current cap before expansion, after which object/function/variadic arguments, predefined tokens, stringify, paste, and replacement paths inherit the invocation cap. Raw cached tapes deliberately stay unstamped so the same guardless source can be replayed under different ambient states.

Several narrower designs were rejected during implementation and review. Restoring pack state at every include boundary would contradict host translation-unit behavior and break headers that intentionally communicate state. Stamping the immutable raw cache would retain the first include's ambient cap. Treating cache membership itself as once would silently suppress valid guardless re-includes. The first negative table treated named pack stacks as unsupported; direct GCC/Clang comparison established their common syntax and nearest-match behavior, so the implementation was expanded instead. The first 2,048-transition scale test did not actually prove free-list reuse under the default 8 MiB arena; the repeated cached forced-input probe above replaced that false-confidence path. Empty pop, missing names, malformed syntax, invalid caps, and depth overflow are transactional errors. Unmatched final pushes remain accepted because GCC 15 accepts them while Clang 22 exposes only a warning-category diagnostic; accepting the broader behavior avoids artificially narrowing source compatibility.

Final verification is green: all 26 focused preprocessing tests; the complete strict hosted Toolchain suite under Windows Clang and WSL GCC; all 26 preprocessing modes in a fresh WSL GCC address/leak/undefined-behavior sanitizer build; Clang static analysis and GCC `-fanalyzer`; all 25 build-graph audit tests; and all 197 Windows repository tests with the one expected `/dev/full` skip. The regenerated audit and `make check-bootstrap-audit` pass with 675 active inputs, 250 feature IDs, 484 transforms, and the exact 1/2/2 once/push/pop contract. Independent Standards and Spec reviews are green after adding cached raw-source replay under two ambient caps, the full cap/suffix boundary, a defined raw macro-operand rejection, and accurate host-divergence documentation. Semantic re-review found no correctness blocker and confirmed that the strengthened reuse probe fails the non-reusing design. No OS source, kernel adapter, or `TempleOS/` reference file changed, so no boot-path behavior claim is made and a new OS boot smoke was not required for this hosted-only seam.

This step transfers pragma semantics into the shared tape but does not yet transfer production frontend ownership. The private kernel preprocessor still handles once independently and discards pack state; its token type lacks shared path/column/cap metadata, record layout is duplicated, and 16-bit types are not yet modeled with the FAT16 ABI needed by these headers. Macro-expanded includes, presumed locations through `#line`, Cupid `#exe`, full active-corpus preprocessing, typed-tape parser/layout consumption, and kernel cutover remain. Workspace Markdown auto-save committed the capability/ADR records as `d3aae29` and the regenerated human audit as `754765b` while code verification continued; history remains forward-only and the coherent code/test/JSON-audit commit follows them.

## 2026-07-10: policy-neutral typed Cupid `#exe` markers

The next issue #24 slice moves the active Cupid `#exe` spelling into the shared preprocessing tape without moving execution policy into preprocessing. An active source-written `#exe` or `%:exe` in Cupid mode must have a raw `{` as its first token on the same phase-two logical directive line; macros cannot manufacture the directive or opener. The operation emits one owned `CTOOL_C_PP_TOKEN_CUPID_EXE` token whose spelling is `exe`, whose location is the introducing marker, and whose pack metadata is the current translation-unit cap. It then expands the ordinary suffix through physical newlines until the next directive. This preserves cross-line function-macro calls while allowing later conditionals, includes, pragmas, and macro definitions to keep their normal preprocessing meaning. Brace matching, placement, nesting, ordered block representation, JIT/AOT lowering, and execution limits remain parser/backend responsibilities. Active C11 use is an explicit unsupported-language diagnostic, malformed active Cupid use is an input diagnostic, and inactive use has no effect.

The exact active-source contract now scans reachable Cupid `.cc` files and reachable headers, fails closed on conditional, case-variant, file/string, identifier, parenthesized, brace-digraph, lexically invalid, and other non-block forms, and records marker/form/depth/path/line evidence. It finds exactly one ordinary unconditional block, `bin/feature6_exe.cc:7`; `TempleOS/` remains excluded. The hosted contract consumes that unchanged file and proves one typed marker at line 7 column 1, its raw brace at column 6, and body provenance at line 8, with no generated `__cc_exe_` prefix. Synthetic coverage proves selected and inactive conditionals, `%:`, phase-two splicing, macro-expanded bodies and predefined locations, forced/ordinary/once include order, guardless replay under different pack caps, borrowed-storage ownership, parser ownership of an unclosed block, failure after marker publication with complete arena/output rollback, recovery in the same job, and 256 markers without inheriting the private kernel's 128-block runtime limit.

Source inspection documents why the current kernel lowering is not the shared seam. It rewrites JIT blocks into generated hidden functions and later discovers them by name prefix, while AOT skips them with a warning. Its private capture can count braces in block comments, omit an EOF diagnosis, cross an include boundary, suppress directives inside a block, and leave single-line AOT skip state active. Those mechanics, hidden-name collision risk, executable-memory behavior, and the 128-block limit were deliberately not copied. The later parser/backend cutover must keep an explicit ordered block representation. Whether AOT executes blocks or only validates them, whether skipped bodies are type-checked, whether blocks are top-level-only or nestable, and whether header-origin markers are language-valid remain explicit later language-design decisions; the preprocessor preserves header provenance rather than deciding them early.

Several failed approaches sharpened the contract. The first dispatcher used one extended suffix boundary for both syntax validation and expansion, which could accept `#exe` followed by a brace on the next logical line; separating the original directive end from the expansion end fixed it. Clang analysis exposed implicit nullable function-macro argument/substitution-plan invariants along the newly traversed expansion path. An initial broad guard rejected valid zero-parameter function macros and two existing tests failed immediately; the final checks distinguish an empty argument array from an invalid referenced parameter and both Clang analysis and GCC `-fanalyzer` are clean. Independent review found that the first exact audit scanned only `.cc` inputs, so a reachable header could hide another marker; the contract now includes `c_header` and its fixture requires exact header evidence. Review also prompted selected-conditional, post-marker rollback, and hidden-prefix assertions.

Final verification is green: all 31 focused preprocessing tests; the complete strict hosted Toolchain suite under Windows Clang and WSL GCC; all 31 preprocessing modes in a fresh WSL GCC address/leak/undefined-behavior sanitizer build; Clang static analysis and GCC `-fanalyzer`; all 29 build-graph audit tests under Windows as part of the repository suite and independently under WSL; and all 206 Windows repository tests with the one expected `/dev/full` skip. The regenerated 675-input/250-feature/484-transform audit and `make check-bootstrap-audit` pass with the exact one-block `#exe` contract. The first sanitizer command lost Bash variables at the PowerShell/WSL boundary and built nothing, a corrected runner initially passed 29 modes but used the repository rather than Toolchain root for the active macro manifest, and a package-style WSL unittest command loaded no tests; none is counted as evidence. Explicit-path sanitizer and discovery reruns passed. A parallel repository test process also became detached when its sibling WSL command failed, so its unobservable result is excluded and the final visible `make test` rerun is the recorded gate.

Independent semantic, Standards, and Spec reviews are green after the reachable-header and test-hardening fixes. No OS source, kernel adapter, or `TempleOS/` reference file changed, so this hosted-only seam makes no boot-path behavior claim and did not require a new OS boot smoke. Production frontend ownership has not moved: the kernel still uses its private textual preprocessor/lowering. Macro-expanded include operands, presumed locations through `#line`, full active-corpus preprocessing, typed-tape parser/layout consumption, and kernel cutover remain. Workspace Markdown auto-save committed the capability/ADR records as `9eab0e6`, the first generated human audit as `9e0441f`, and its final source-hash refresh as `d81665c` while verification continued; history remains forward-only and the coherent code/test/JSON-audit commit follows them.

## 2026-07-10: C11 macro-expanded include operands and exact closure audit

The next issue #24 slice implements C11's third `#include` form without weakening any source or replacing the existing search model. One exact raw contextual header-name token or ordinary string token remains opaque. Every other active operand is copied into the ordinary private macro worklist and fully rescanned, including configured, object, function, nested, stringify-derived, paste-derived, variadic-capable, forced-input, included-header, digraph, and phase-two-spliced paths. The result must be one ordinary unprefixed string token, one surviving contextual header-name token, or a complete split `<` ... `>` sequence with no trailing token. Quotes retain including-parent-first search and angles retain eligible-root-only search. Empty results, numbers, adjacent or prefixed strings, recursive residues, malformed angles, invalid paste, and trailing tokens are transactional input errors; a valid missing path remains a precise not-found error.

C leaves expanded angle-token combination implementation-defined. Targeted GCC 15.2 and Clang 22.1 probes agree on the policy Cupid now contracts: concatenate each surviving token spelling and preserve each surviving separation as exactly one ASCII space, including the first boundary after `<` and the last boundary before `>`. The focused fixtures cover leading, interior, and trailing path spaces as well as raw direct-operand opacity. Macro expansion writes the final logical path into one lazy `path_bytes` buffer allocated before the directive scratch mark. Temporary work nodes and reconstructed spellings are rewound before path resolution or recursive source entry, and the ordinary expansion-node count is restored. A constrained 512 KiB test drives 32 configured aliases through one cached forced dispatcher 32,768 times. That represents more than one million cumulative expansion nodes: it fails if either scratch storage or the counter leaks across directives and passes with the implemented reuse.

The active graph does not currently exercise a macro operand, so this is standards/toolchain compatibility rather than a production-ownership transfer. A new phase-two- and comment-aware audit fails closed before include-closure construction when it encounters a pp-token operand. Its diagnostic records the logical source path, first physical line, ordinary or digraph marker, raw directive, normalized operand, and syntactic conditional stack. The checked contract proves exactly 645 tracked C-family inputs and 2,296 operands: 2,100 direct quoted, 196 direct angle, zero macro-expanded, all ordinary markers, and maximum conditional depth two. Four materialized generated C inputs add 12 direct quotes but remain owned by their explicit generator boundary. A future active macro include must enter through a Cupid-produced dependency trace rather than teaching Python to duplicate macro semantics.

The red and review history exposed several false-confidence paths before freeze. The first positive fixtures reached the old explicit unsupported diagnostic. The first product build then used `memcpy`; the strict freestanding build rejected that implicit host-libc dependency, so a small bounded character copier replaced it. Static analysis found that an impossible inverted token range left local status reasoning implicit; initialization plus an executable internal-range guard closed that path. The first final-form implementation rejected a valid raw contextual `<SURVIVES>` token left after an empty suffix macro; direct GCC/Clang comparison found the mismatch and `<SURVIVES> EMPTY` now contracts the correct angle lookup. The first scale design repeated 64 source `#define` directives and exhausted unrelated persistent replacement copies made before identical-redefinition detection. Installing 32 aliases through ordered request actions isolates the intended scratch/count property instead. Review also added the first-interior-space case and explicit public wording for a surviving contextual header-name. A transient expected-token ordering error placed the leading-space fixture after the ordinary spaced fixture; the focused test caught it before the final suites and the expected order now follows source order.

Final verification is green on the supported hosted compiler families. Windows passed all 34 focused preprocessing tests, all 32 audit tests, a fresh strict Clang Toolchain build with all 83 contract invocations and 22 CupidASM demos, Clang static analysis, MinGW GCC 15.2 `-fanalyzer`, and the complete 212-test repository suite: 211 passed and only the expected Windows `/dev/full` case skipped. WSL passed fresh strict GCC 13.3 and Clang 18.1 Toolchain suites, all 34 modes under Clang address/leak/undefined-behavior sanitizers, and all 32 audit tests. The first WSL attempts used a short outer timeout, unsupported package-style unittest addressing, and one misquoted sanitizer invocation; the corrected explicit build directories, discovery command, and exported flags produced the evidence above, so none of those harness failures is counted as product evidence. The canonical Windows `make check-bootstrap-audit` passes. A WSL attempt to compare its complete JSON byte-for-byte with the Windows-generated checked manifest is retained as a failed gate rather than claimed as a pass: the generated Markdown and all source/capability contracts are identical, while the pre-existing full JSON records host executable suffixes in 199 places and host Make wildcard/link-input ordering elsewhere. That platform-dependent manifest limitation is separate from this slice and must not be mistaken for include-contract drift.

Independent semantic, Standards, and Spec reviews are green after the surviving-header, whitespace, scale-isolation, and public-contract fixes. No OS, kernel adapter, or `TempleOS/` reference file changed, so no boot-path behavior claim is made and no new emulator smoke was required. Presumed locations through `#line`, a checked per-translation-unit full active-corpus preprocessing gate, typed-tape parser/layout consumption, and kernel integration remain. Workspace Markdown auto-save committed the capability/ADR records as `bf66765` and `b0eb4a1`, then refreshed the generated human audit as `677e79d` and `815260e`; history remains forward-only and the coherent code/test/JSON-audit commit follows them.

## 2026-07-10: exact active-corpus CupidC preprocessing gate

The next issue #24 slice turns the shared preprocessing tape into a checked whole-corpus contract without claiming production frontend ownership. The build-graph auditor now asks GNU Make to evaluate the root, `user`, and `toolchain` graphs, classifies every resulting C/Cupid translation-unit transform, and emits one deterministic X-macro manifest consumed by the hosted contract. The tracked cohort is exactly 345 roots: 152 `KERNEL_I386`, six `DOOM_COMPAT_I386`, 80 `DOOM_TREE_I386`, three `USER_I386`, and 104 `CUPID_RUNTIME`. Four strict kernel roots are generated and gated separately: `ksyms_data.c`, the binary-program table, the demo-program table, and the documentation-program table. The explicit boundary also records 22 browser include-only fragments, two delivered non-root headers, and 24 deferred hosted units, split into 15 external-system-header/runtime cases and nine hermetic cases awaiting a supported hosted profile. `TempleOS/` remains reference-only and is absent from every count.

Profiles are source- and Make-derived rather than guessed by the harness. The three kernel/Doom profiles preserve the exact ordered 18-root kernel search list; both Doom profiles add their two source/stub roots, the Doom-tree profile forces `dglibc_compat.h`, and the user profile has only `/user`. The four C11 profiles configure the six target facts currently required by active source, with `__GNUC__=1` documented as a compatibility definedness marker rather than a host version; kernel/Doom add `__SSE2__`, strict kernel adds `DEBUG`, and the Doom tree adds its two build-owned definitions. `CUPID_RUNTIME` deliberately has no configured roots or macros and disables GNU extensions. A Make-variable/recipe gate fails closed on include-order, forced-input, macro, target, SIMD, optimization, literal recipe, source-count, recipe-shape, and generator-boundary drift. GNU Make remains the assignment/function/conditional authority through evaluated sentinels; the auditor does not contain a second Make implementation.

Each manifest row runs in a fresh preprocessing job. The harness copies the public defaults but raises only the operation arena to 256 MiB: the hosted token is 56 bytes and generated `ksyms_data.c` publishes 185,994 tokens, so its public array alone requires 10,415,664 bytes before source, cache, worklist, and spelling storage. The library's lazy 8 MiB default is unchanged. Every success validates the public path, token kind and spelling, physical location, pack cap, pointer/count consistency, and zero diagnostics; a legitimately empty successful tape remains valid. The tracked mode must execute exactly 345 rows. The generated mode must execute exactly four rows after one serialized root Make invocation materializes the sources and pass-one symbol input. That target stops after the pass-one CupidLD kernel link and generators: it does not build the pass-two kernel, flattened kernel, or disk image.

### Red, review, and failed-approach history

- The first red contracts had no manifest type, classifier, renderer, CLI sidecar, or corpus modes. The completed path now makes all five pieces one checked interface.
- The first include-root extraction omitted the root `/kernel` entry because its pattern assumed a leading space. Fixing the first assignment raised the exact live closure from 2,296 to 2,297 operands: 2,101 quoted and 196 angle operands across 645 inputs.
- Early harness revisions incorrectly required a nonempty token tape, treated counted logical paths as NUL-terminated, and imposed include-root assumptions on the rootless Cupid profile. Focused failures replaced those assumptions with the public counted-storage contract.
- The first profile table incorrectly enabled GNU mode for Cupid source. It is now false for `CUPID_RUNTIME` and true only for the four C11 compatibility profiles.
- The first generated-source recipe launched four recursive root Makes in parallel. Their shared pass-one/generated outputs raced; one serialized root invocation now owns all four prerequisites before the four-row check.
- Sampled profile assertions initially let wrong endian values compare equal at `0 == 0`. Exact ordered include, macro, forced-input, dialect, and target sets replaced the samples, and every Make-owned `-D` is linked to the emitted action.
- Review found unsafe C-literal rendering around trigraphs, a malformed binary-wrap input path, and unguarded literal/SIMD/`OPT` drift. Octal-safe UTF-8 rendering, exact wrap shape, and negative drift contracts close those paths.
- A bespoke parser for raw Make assignments was rejected after `:=` timing proved it could disagree with the build. GNU Make evaluated sentinels replaced it, and a regression pins immediate-versus-recursive assignment behavior.
- Final standards review found that `${NAME}` recipe references, escaped shell references, and computed Make functions could hide flags, while required-flag membership accepted contradictory target/pass-through options and incomplete risk lists missed alternate language, character, `wchar_t`, and accelerator-macro modes. Red cases reproduced each family. The recipe scanner now accepts only simple `$(NAME)`/`${NAME}` references and automatic variables, rejects every other dollar form, and positive allowlists make every current literal or expanded option explicit. `-m64`, `-mno-sse2`, `-Wp,-D...`, `-xc++`, signedness/width options, and `-fopenacc` all fail closed. A final review then exposed non-dash injection through compiler response files and per-recipe environment assignments; `@extra.rsp` and `CPATH=private` red cases now pin their rejection too.
- The first final root gate passed all 223 Python tests and then reported a stale audit. A fresh comparison found exactly one differing field: the `toolchain/Makefile` provenance digest after its final target comment/wiring edit. Markdown and the X-macro manifest were already byte-identical, proving this was not generated-file state drift; refreshing the digest made the checked audit pass.
- One WSL audit command used a dotted module path even though `tests/` is not a package, and one analyzer wrapper lost a quoted `mkdir` operand. Corrected discovery and quoting reruns supply the recorded evidence; neither harness-only failure is counted as a product result.

### Verification and portability boundary

| Command/check | Result | Evidence |
| --- | --- | --- |
| Fresh strict Windows Clang Toolchain suite | PASS | 84 contract invocations, including 35 preprocessing modes and the 345-root corpus, plus all 22 assembly demos; 14.972 seconds with no skip. |
| Focused preprocessing Python suite | PASS | All 35 cases passed in 5.709 seconds. |
| Windows repository suite | PASS | All 223 tests passed in 317.995 seconds; only the expected Windows `/dev/full` case skipped. The separately rerun checked audit passed in 29.270 seconds. |
| Generated-source gate | PASS | All four generated roots passed in 27.597 seconds; scope stopped at pass-one materialization and left tracked status unchanged. |
| Static analysis | PASS | Strict Clang analysis and GCC `-fanalyzer` passed on both the preprocessor and corpus harness. |
| WSL GCC and Clang Toolchain suites | PASS | GCC 13.3 and Clang 18.1 ran the same 35 modes/99 successful outputs with zero diagnostics in 58.48 and 58.31 seconds. |
| WSL sanitizers | PASS | Clang address, leak, and undefined-behavior sanitizers ran the full Toolchain suite in 95.80 seconds with zero sanitizer diagnostics. |
| WSL build-audit suite | PASS | All 42 cases passed in 546.557 seconds. |
| Cross-host generated contracts | PASS | The 13,696-byte Markdown audit and 35,223-byte X-macro manifest are byte-identical between Windows and WSL; translation-unit and include contracts compare equal. |

The complete legacy JSON remains intentionally Windows-canonical rather than falsely claimed byte-portable. A fresh Linux rendering has 276 index-level scalar differences: 200 are `.exe` suffix representation, while 76 are equivalent transform/input ordering. Normalized root/user/toolchain transform sets are identical at 437/8/39, input multisets match, and non-transform metadata matches after suffix normalization. Linux `make check-bootstrap-audit` therefore retains the already documented expected stale result against the Windows snapshot; this is not a profile or corpus drift.

After the response-file/environment fix, the definitive Windows build-audit discovery passed all 42 tests in 219.363 seconds and the checked regeneration passed in 26.348 seconds. The two focused methods also passed under WSL. Independent final Standards and Spec re-reviews report zero findings.

No OS source, kernel adapter, assembly source, or `TempleOS/` reference file changed. This hosted-only seam does not justify a new boot claim, and no emulator smoke was required. The active graph remains 675 source inputs, 250 feature IDs, 484 transforms, and 39 accounted unreachable inputs. Production kernel parsing still uses the private frontend, hosted tools still depend on a host C compiler, and the 24 deferred hosted units remain explicit debt. Presumed locations through `#line`, typed-tape parser/layout consumption, private-preprocessor removal, kernel ownership, and hosted-tool self-compilation are the next capability boundaries.

Workspace Markdown auto-save committed the active-source summary as `c681d64`, the bootstrap overview as `1b77e79`, the capability/host/migration matrices as `7f2865b`, and ADR 0012 as `ba6a934` while implementation and review continued. History remains forward-only; this log and the coherent code/test/JSON-audit commit follow those documentation commits.

## 2026-07-10: C11 `#line` and dual source provenance

The next issue #24 slice implements active standard `#line` and digraph `%:line` without changing an OS source. Directive operands pass through normal macro rescanning and must finish as one decimal preprocessing number in 1..2147483647, optionally followed by one ordinary narrow string literal. Filename escapes are decoded into counted source bytes, including simple, octal, hexadecimal, universal-character, embedded-NUL, empty, and opt-in GNU `\e` cases. `__FILE__`, `__LINE__`, emitted tokens, downstream structured diagnostics, and unmatched-conditional diagnostics consume the resulting presumed name/line; eager whole-source lexical errors remain physical because phase-three tokenization necessarily precedes directive execution. A dedicated `CTOOL_C_PP_DIAG_LINE` code owns malformed operands and unsupported digit-led markers.

Every public preprocessing token now carries two explicit locations. `location` is compiler-facing presumed provenance with a physical column, while `physical_location` is the immutable canonical source origin. Direct tokens and substituted macro arguments retain their own physical origin; replacement-list, stringify, paste, predefined, and Cupid `#exe` output uses the invocation or directive-marker origin. Both names and paths are job-arena owned counted strings. The hosted x64 public token grows from 56 to 80 bytes; generated `ksyms_data.c` therefore requires 14,879,520 bytes for its 185,994-token public array. The existing lazy 256 MiB corpus-job ceiling remains sufficient and the library's 8 MiB default remains unchanged.

Presumed state belongs to a physical source entry, not the translation unit globally. Each forced, ordinary, or cached replay begins with its canonical path and physical line one, replays its own directives, and restores the parent's current mapping on return. Relative lookup, cache identity, and once identity continue to use the physical source path, so two distinct headers that both presume `shared-v.h` remain distinct and a virtual parent name cannot redirect a sibling include. A compact phase-two newline scan records the exact physical line following each directive; operand continuations follow GCC's post-splice behavior rather than Clang's differing count. Presumed names are interned after directive scratch is rewound, and 32 aliases across 32,768 cached replays prove both bounded arena reuse and restoration of more than one million cumulative expansion nodes.

GNU digit-led markers remain explicit unsupported syntax until flags 1/2/3/4 and include-stack semantics have a real typed representation. Both GCC and Clang confirmed that such a line inside an inactive conditional group must be ignored, so the unsupported check occurs only after skipped-group handling. Empty presumed names remain exact in structured tokens and diagnostics; the existing generic job renderer displays any empty diagnostic path as `<toolchain>`, a display fallback rather than a mutation of structured provenance. The private kernel frontend was not extended: it cannot preserve counted paths or dual locations and remains the later typed-tape cutover target.

The active-source audit adds a deterministic `c_preprocessor_line_directives` contract over the same exact 645 tracked C-family inputs. It distinguishes ordinary/digraph markers, direct decimal/direct filename/raw pp-token forms, filename certainty, syntactic conditional depth, and GNU numeric markers without pretending to macro-expand source itself. The repository has zero occurrences in every category. Comments, strings, phase-two splices, generated inputs, malformed forms, and case-insensitive `TempleOS/` boundaries are tested. Review found that an adversarial Make edge could previously make a reference-tree C file active even though ordinary unreachable reference files were excluded; the audit now fails explicitly if any active C-family edge enters `TempleOS/`.

### Red, review, and failed-approach history

- The first product contract reached the old unsupported-directive path. Direct, macro, digraph, include, cache replay, once, pack, Cupid `#exe`, error, and scale cases now reach the completed implementation.
- Independent review found that digit-led GNU markers were rejected before inactive-group handling. A GCC/Clang stdin oracle reproduced the mismatch; the new inactive-marker case failed, the dispatcher order changed, and all 39 modes returned green.
- Review also found a mixed diagnostic after `#line 50 "condition-name.c"`: raw `#ifdef __VA_ARGS__` reported the presumed path with physical line two. The regression reproduced it, and the central reserved-identifier failure now maps raw tokens through the presumed-location helper and reports line 50.
- The first scale fixture expanded only one alias per replay and could not prove expansion-count restoration. The 32-alias fixture crosses the one-million-node limit when the line-handler checkpoint is removed and passes when restored. An initial mutation accidentally removed the separate include-handler checkpoint and stayed green; that invalid mutation was excluded, then the correctly targeted mutation failed with the expected macro-expansion limit before the final source was restored and rerun green.
- ABI review found missing lifetime and generated-token provenance wording plus the empty-name renderer seam. The public contract now states ownership and origin policy, and a structured empty-path diagnostic regression pins the exact data while documenting the renderer fallback.
- The checked JSON was intentionally stale while the audit schema and product were under review. Regeneration added the exact zero-count contract and final source hashes; `make check-bootstrap-audit` passes. Markdown auto-save committed the generated human audit as `4382317`, the overview/ADR as `76ebae4`, and the capability/host/migration matrices as `ddc4f78` while verification continued.

### Verification and remaining boundary

| Command/check | Result | Evidence |
| --- | --- | --- |
| Fresh strict Windows Clang Toolchain suite | PASS | All 88 contract invocations passed, including all 39 preprocessing modes and 22 assembly demos, in 14.9 seconds. |
| Windows repository suite | PASS | All 232 tests passed in 376.390 seconds; only the expected Windows `/dev/full` case skipped. The enclosing Make gate completed in 408.2 seconds. |
| Generated-source gate | PASS | All four materialized generated roots preprocessed successfully; the gate stopped after pass-one link/generation and completed in 29.9 seconds. |
| Checked active-source audit | PASS | All 47 audit methods passed inside the repository gate; the regenerated audit records 645 source files and zero named/direct/pp-token/filename/digraph/ordinary/numeric-marker occurrences. A standalone drift check passed in 37.9 seconds. |
| WSL GCC and Clang Toolchain suites | PASS | Fresh isolated GCC 13.3 and Clang 18.1 builds each passed all 88 modes, including 39 preprocessing modes and 22 demos, in 60.22 and 60.45 seconds. |
| WSL sanitizers | PASS | Clang address, leak, and undefined-behavior sanitizers passed all 88 modes in 95.51 seconds with no finding. |
| Static and mutation analysis | PASS | GCC `-fanalyzer` and Clang `--analyze` found nothing in the product or harness. Removing the line expansion-count restore makes `line-scale` fail at the global token limit; restoring it returns green. |
| Independent review | PASS | Audit Standards/Spec, preprocessor semantic, and ABI/provenance reviews report zero remaining correctness findings after their regressions landed. |

No OS source, kernel adapter, assembly source, or `TempleOS/` reference file changed. This is a hosted preprocessing capability, not a production frontend-ownership transfer, and it adds no host-dependency retirement or new boot claim. The active graph remains 675 language inputs, 250 feature IDs, 484 transforms, and 39 accounted unreachable inputs. Production parsing still uses the private kernel frontend; typed-tape parser/record-layout consumption, private-preprocessor removal, kernel cutover, hosted-tool profiles, named GNU variadics, digit-led marker semantics, and staged self-compilation remain.

## 2026-07-11: immutable indexed CupidC i386 type layout

The next issue #24 slice establishes the first parser-neutral semantic layer beneath ADR 0003. `ctool_c_layout_types` accepts borrowed immutable arrays of type nodes, record members, and function-parameter indices, then publishes request-ordered type/member layouts owned by the Toolchain job. References are indices rather than pointers or private parser objects. The operation never asks the compiler hosting the bootstrap for target facts: signed plain `char`, ILP32 scalar/pointer/enum representation, ordinary i386 64-bit scalar alignment, the active 16-byte vector forms, record layout, and bit allocation are fixed Cupid target policy. Distinct C scalar identities remain distinct even when size/alignment match.

By-value type dependencies are resolved with an iterative colored stack, so arbitrary graph order and large graphs do not consume the host call stack. Pointer recursion remains a weak edge; incomplete by-value use and strong cycles receive precise presumed-location diagnostics. The same traversal caches two semantic facts for every type: whether it is `_Bool` after any aligned wrappers, and the target atomic minimum alignment for supported scalar, pointer, and enum types. Member and bit-field layout consume those arrays in constant time, keeping the complete operation `O(types + members)`. Checked request validation covers all counts, slices, references, member ownership, alignments, array products, final sizes, function parameter references, and unsupported variable bounds. Success keeps its published arrays stable across later operations while reclaiming only traversal scratch. Failure zeros the result and rewinds the entire operation while retaining the job diagnostic, and a later valid operation in the same job recovers normally.

The graph represents scalars, pointers, fixed/unspecified arrays, enums, active vectors, function types as semantic markers, structs, unions, Cupid classes, and alignment-carrying wrappers. A wrapper records the exact effective alignment attached through a typedef or type attribute without mutating the wrapped type, so it may lower or raise natural alignment. A positive oracle gives a naturally four-aligned `int` effective alignment one; the active per-CPU wrapper raises its record from four to 64. Record explicit alignment remains a separate raise-only operation. Atomic representation is fixed target policy: atomic `long long` and `double` have a minimum alignment of eight, while other supported scalar, pointer, and enum cases retain their target minimum. An atomic aligned wrapper composes the policies as `max(exact wrapper alignment, target atomic minimum)`, so an exact one cannot under-align an atomic eight-byte scalar. Atomic aggregate wrappers remain a useful explicit unsupported diagnostic. Function return/parameter legality, qualifier placement, duplicate names, anonymous-member legality, and namespaces remain parser responsibilities rather than being guessed by the layout engine.

Pack state is sampled per member declaration from the typed tape's first declaration-specifier token. Every declarator from that declaration shares the sample; there is no record-entry snapshot and no pragma replay inside layout. Local i386 host probes exposed a real GCC/Clang divergence: with `pack(push, 1)` for the first member and `pack(pop)` before the second, GCC produces size/alignment `8/4` and places the second member at byte 4, while Clang produces `5/1` and byte 1. Cupid deliberately follows GCC's per-member result because ADR 0012 already preserves the effective cap at each token. Additional probes pin that a packed record can have an explicitly aligned member raise alignment, an active pragma cap can limit that member alignment, and explicit record alignment raises the final result. Bit-field probes pin least-significant-bit allocation in the current integer storage unit and the zero-width boundary policy.

### Active ABI oracles and boundaries

The contract's active-source fixtures are manually transcribed semantic graphs, not parsed source. All 54 record members from unchanged `kernel/fs/fat16.h` are pinned: partition, MBR, boot-sector, and directory-entry records are `16/1`, `512/1`, `36/1`, and `32/1`; the runtime filesystem and file records are `36/4` and `24/4`; every member offset is checked, including MBR partitions at 446/signature at 510 and the boot-sector 32-bit totals at byte 32. The active Doom 32-bit color channels remain pinned at bit offsets 0/8/16/24, and `drivers/e1000.c` contributes the real 16-byte/16-aligned `e1000_rx_desc_t` with member offsets 0/8/10/12/13/14. Synthetic fixtures separately check nested and anonymous records, unions, classes, flexible-array placement, record/member packing, zero-width and unnamed fields, explicit member/record alignment, and pointer-recursive records.

Adversarial active shapes close gaps that a convenient synthetic record would miss. A 64-byte CPU context feeds the 656-byte, 16-aligned process shape with its 512-byte floating-point state at byte 80. The syscall table has 103 function-pointer members and is exactly 412 bytes, proving that the old private compiler's 32-member ceiling is not imported. The `per_cpu_t` body is 128 bytes with ordinary alignment four, while its aligned typedef wrapper raises alignment to 64 without altering the body; 64-bit and function-pointer member offsets are pinned. Prior results are reread after later layout calls to prove arena lifetime. Twenty-four repeated 257-type/128-member layouts fit a 256 KiB operation budget only because scratch is reclaimed, and an 8,193-type array/record graph runs twice with identical immutable outputs. A second scale fixture chains 4,096 exact-alignment wrappers above `_Bool`, then uses the outermost wrapper for 4,096 one-bit fields. Two deterministic runs produce the exact 512-byte, one-aligned record with byte/bit offsets `index / 8` and `index % 8`; an implementation that unwraps the chain for every field performs roughly 16 million wrapper visits and violates the declared `O(types + members)` contract.

The adversarial review caught concrete implementation errors before the seam was accepted. `_Bool` bit-field validation incorrectly used `sizeof(_Bool) * 8` as the maximum width and therefore accepted widths two through eight instead of the C semantic maximum one. An explicitly aligned bit field could remain in the preceding allocation unit. Unnamed or pack-capped union bit fields incorrectly occupied the full base type, while a zero-width field ignored its explicit alignment. Atomic `long long` and `double` incorrectly retained four-byte alignment. Successful operations retained traversal scratch, and an array of an aligned wrapper was accepted even when element size was not a multiple of its required alignment. Late complexity review also found that discovering `_Bool` and atomic properties by re-walking aligned-wrapper chains for each consumer would make the combined graph/member operation quadratic. Per-type fact caches and the 4,096-by-4,096 regression now pin the linear design. Red regressions cover each correction. The aligned typedef wrapper, independent function slice/reference checks, output-lifetime probes, and the syscall/process/per-CPU fixtures further prevent the old parser's simplifying assumptions from becoming this interface. The negative mode pins 25 useful diagnostics and same-job recovery rather than accepting a generic failure.

Spec review corrected two ownership mistakes in the initial graph contract. An aligned wrapper had been described and implemented as raise-only, but GCC/Clang alignment attributes can give an `int` an exact effective alignment of one; a non-atomic wrapper now copies size/category and replaces alignment exactly, while record explicit alignment still only raises. Atomic composition remains stricter: an atomic wrapper takes the greater of its exact alignment and the referenced scalar/pointer/enum target minimum, and an atomic wrapper over an aggregate is rejected. The first flexible-array rule also counted only directly named members and rejected a graph equivalent to `struct outer { struct { int promoted; }; unsigned char data[]; };`. That declaration is valid because the anonymous record promotes `promoted` into the containing structure. Layout cannot decide language-mode legality or recursively promoted names from byte-layout metadata, so it now enforces only final structure position. The declaration parser must reject a true sole-FAM declaration and accept the anonymous-promoted-name counterexample. The former sole-FAM layout rejection is therefore removed from the regression narrative rather than preserved as a false success.

Adapting the private kernel frontend was rejected. It re-lexes flattened text, collapses important C type identities, loses counted spellings, columns, dual physical/presumed provenance, and pack stamps, retains a fixed record-member ceiling, and emits machine code while parsing. Publishing an adapter would make that direct parser-to-code path the new interface. The selected next step is instead a declaration frontend that consumes the immutable ADR 0012 tape, applies C/Cupid language legality, constructs the ADR 0013 graph, and reproduces the FAT16 oracles from the unchanged header before typed-AST/IR lowering.

### Red, audit, and verification record

- The first focused test was red because the Make graph had no `cupidc_type.c` rule or contract artifact. The completed graph adds one freestanding product object, one contract object, one hosted executable, and six focused modes: scalars, FAT16, records, exact errors/recovery, scale, and adversarial active shapes.
- Adding the product and contract introduced two hosted C roots before the checked audit was regenerated. The drift gate failed closed instead of silently accepting them. The first Windows audit rerun then exposed stale exact source/include expectations, and the first WSL suite exposed a still-hard-coded 24-unit preprocessing deferral sentinel. Correcting those contracts and regenerating the audit records 678 active inputs, 487 transforms, 250 feature IDs, 265 C files, 261 headers, 126 Cupid C files, and 50 prioritized toolchain sources.
- The include inventory is now 2,305 direct operands across 648 C-family inputs: 2,106 quoted and 199 angle. The supported preprocessing delivery manifest remains 345 tracked roots plus four generated roots. Hosted deferrals increase transparently to 26: 16 external-header/runtime cases and ten hermetic units awaiting a hosted profile.
- The host compiler owns 286 transforms after adding two native objects and the fifteenth hosted artifact. This is explicit bootstrap debt, not a retirement claim.
- The focused Python contract passes all six tests. The first sanitizer command was misquoted and truncated before it exercised the intended modes, so that invocation is discarded rather than counted. Final whole-repository, cross-compiler, corrected sanitizer, analyzer, and audit timing evidence belongs to the implementation gate and is not pre-claimed here.

No OS source, kernel adapter, generated OS artifact, or `TempleOS/` reference file changed. The module and contract are hosted only, so no kernel ownership or boot behavior is claimed and no emulator smoke follows from this slice. Production CupidC still uses its private frontend. Parsing unchanged declarations through the typed tape is next; private-preprocessor removal, typed AST/linear IR, kernel cutover, host-runnable source compilation, and staged self-hosting remain.
