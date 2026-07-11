# Cupid Toolchain bootstrap

This directory is the durable record for moving Cupid OS from its current host-produced build to a self-hosting Cupid Toolchain. The implementation map is [GitHub issue #13](https://github.com/cupidthecat/cupid-os/issues/13).

The current OS baseline is still compiler-hosted: the Makefile defaults to Clang on Windows and GCC on Linux. A platform-neutral Cupid Toolchain foundation, a typed transactional CupidC preprocessing tape, deterministic ELF32 relocatable-object/read-only executable module, and shared typed 16/32-bit x86 instruction model now build through supported hosted contracts and real kernel adapters. The preprocessing module owns translation-phase tokenization, ordered object/function/variadic macros and operators, C11 conditionals and reproducible predefined macros, active C11 `#line` presumed locations with immutable physical provenance, direct and macro-expanded includes, forced inputs, guarded/cache-aware traversal, canonical once identity, translation-unit pack metadata, and policy-neutral typed Cupid `#exe` markers. Checked manifests now classify all 2,297 tracked include operands as 2,101 direct quoted plus 196 direct angle forms with zero macro operands, all 101 active `#if`/`#elif` occurrences, zero named `#line` directives or GNU numeric markers across 645 active C-family inputs, the exact five active pragma sites, and the unconditional block-form `#exe` at `bin/feature6_exe.cc:7`. A Make-derived per-translation-unit manifest drives the same operation across all 345 tracked OS-delivery roots: 152 strict kernel, six Doom compatibility, 80 Doom-tree, three user, and 104 Cupid programs. A separate non-skipping gate covers the four generated kernel roots. The audit also keeps 22 browser fragments under `bin/browser.cc`, two delivered headers without an invented standalone context, and 24 hosted units explicitly deferred (15 on external headers/runtime and nine pending a hosted Cupid profile). Named GNU variadics, hosted-tool preprocessing ownership, parser/layout consumption, and the kernel cutover remain.

CupidDis is fully shared between its native CLI and kernel adapters. One freestanding CupidASM implementation produces raw, ELF32 relocatable, and fixed-image artifacts for both its hosted CLI and the in-kernel JIT/AOT commands; it owns all four production assembly transforms as well as runtime demo assembly. CupidObj owns all 181 normal-build binary wrapping, final-form SMP wrapping, and linked-ELF flattening transforms. CupidLD owns the two-pass kernel link and all three separate user-program links. No standalone host assembler, ELF linker, or `objcopy` command produces an OS or user artifact. The host C compiler still invokes its native linker backend while bootstrapping hosted Cupid executables, CupidC still runs only inside Cupid OS, and the host symbol reader still owns kernel-symbol extraction. NASM remains optional oracle tooling only. Checked revision `1e079d1` independently reproduces the 447-artifact root/user/toolchain cohort on Windows Clang/LLVM and Linux GCC/binutils; it predates the hosted preprocessor and active-corpus contracts.

## Records

- `LOG.md` is the chronological bootstrapping log. Add an entry for every completed implementation step, failed approach, user answer, important decision, and test run.
- `HOST-DEPENDENCIES.md` records every external build dependency and whether it belongs in the final normal build.
- `CAPABILITY-MATRIX.md` records implemented and missing CupidC, CupidASM, CupidDis, object, linker, and bootstrap capabilities.
- `MIGRATION-MATRIX.md` records which tool owns each source and artifact cohort today and at the self-hosting fixed point.
- `BASELINE.md` documents the reproducible oracle-build interface and evidence format.
- `ACTIVE-SOURCE-AUDIT.md` is the generated human summary of the root OS image, separate user-program, and hosted toolchain build roots, including ownership, source features, ABI requirements, unreachable files, and source-driven priorities.
- `audits/active-build.json` is the deterministic machine-readable companion. Regenerate it with `make bootstrap-audit`; `make test` and `make check-bootstrap-audit` reject drift or a failing audit contract.
- `../adr/` records stable architectural decisions; `../../CONTEXT.md` defines project vocabulary.

## Update contract

Every toolchain implementation commit must update the affected records here and include relevant positive and negative tests. Claims in these files must distinguish source inspection from executed verification. The `TempleOS/` reference tree is excluded from all progress metrics. Generated objects, images, and logs are excluded from source-migration counts and ordinary commits unless they are intentional bootstrap inputs such as checked seeds; their hashes, ownership, layout, and runtime behavior remain required acceptance evidence.

Progress means transferring ownership without reducing Cupid OS behavior:

1. A Cupid tool gains the real feature required by an active source cohort.
2. Tests prove successful behavior and useful failures.
3. The cohort moves from the legacy host/oracle path to the Cupid path.
4. The OS build and applicable boot smoke tests remain green.
5. Host dependencies are removed from the normal build only after the replacement path is proven.
