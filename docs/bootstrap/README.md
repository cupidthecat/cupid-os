# Cupid Toolchain bootstrap

This directory is the durable record for moving Cupid OS from its current host-produced build to a self-hosting Cupid Toolchain. The implementation map is [GitHub issue #13](https://github.com/cupidthecat/cupid-os/issues/13).

The current baseline is host-owned: the Makefile defaults to Clang/LLVM tools on Windows, GCC/binutils on Linux, and NASM on both. CupidC, CupidASM, and CupidDis run inside Cupid OS and are themselves compiled into the kernel by the host C compiler. They are useful native tools, but they are not yet host-runnable bootstrap tools.

## Records

- `LOG.md` is the chronological bootstrapping log. Add an entry for every completed implementation step, failed approach, user answer, important decision, and test run.
- `HOST-DEPENDENCIES.md` records every external build dependency and whether it belongs in the final normal build.
- `CAPABILITY-MATRIX.md` records implemented and missing CupidC, CupidASM, CupidDis, object, linker, and bootstrap capabilities.
- `MIGRATION-MATRIX.md` records which tool owns each source and artifact cohort today and at the self-hosting fixed point.
- `../adr/` records stable architectural decisions; `../../CONTEXT.md` defines project vocabulary.

## Update contract

Every toolchain implementation commit must update the affected records here and include relevant positive and negative tests. Claims in these files must distinguish source inspection from executed verification. The `TempleOS/` reference tree is excluded from all progress metrics. Generated objects, images, and logs are excluded from source-migration counts and ordinary commits unless they are intentional bootstrap inputs such as checked seeds; their hashes, ownership, layout, and runtime behavior remain required acceptance evidence.

Progress means transferring ownership without reducing Cupid OS behavior:

1. A Cupid tool gains the real feature required by an active source cohort.
2. Tests prove successful behavior and useful failures.
3. The cohort moves from the legacy host/oracle path to the Cupid path.
4. The OS build and applicable boot smoke tests remain green.
5. Host dependencies are removed from the normal build only after the replacement path is proven.
