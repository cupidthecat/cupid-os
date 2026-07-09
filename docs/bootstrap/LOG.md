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
