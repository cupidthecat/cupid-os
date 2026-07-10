# Cupid Toolchain bootstrap

This directory is the durable record for moving Cupid OS from its current host-produced build to a self-hosting Cupid Toolchain. The implementation map is [GitHub issue #13](https://github.com/cupidthecat/cupid-os/issues/13).

The current OS baseline is host-owned: the Makefile defaults to Clang/LLVM tools on Windows, GCC/binutils on Linux, and NASM on both. A platform-neutral Cupid Toolchain foundation, deterministic ELF32 relocatable-object/read-only executable module, and shared typed 16/32-bit x86 instruction model now build through supported hosted contracts and a real kernel adapter. CupidDis is fully shared between its native CLI and kernel adapters. CupidASM now has a host-runnable freestanding frontend that produces raw, ELF32 relocatable, and fixed-image artifacts; its in-kernel adapter and the four production NASM transforms have not cut over yet. CupidC still runs only inside Cupid OS, and all three tools are themselves built by the host C compiler. Assembly and inspection semantics have begun transferring to Cupid tooling, but source-build ownership and the normal GCC/Clang/NASM/LLVM paths have not.

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
