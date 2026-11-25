# Run private i386 compiler oracles directly on Windows

The private CupidC call tests emit fixed-address i386 machine code and execute
independent native ABI stubs. Windows now runs that same code in a test-owned
PE32 process. Linux retains its existing GNU assembler, linker and process
execution path.

## Execution boundary

`tests/windows_i386_oracle.py` builds one launcher for the test class. Clang
and LLVM `ld.lld` assemble and link the existing GAS oracle source into ELF32.
The launcher reads that ELF, copies its load segments to their recorded
addresses and calls the emitted CupidC entry. Its console trampoline writes
the original bytes to standard error through `WriteFile`. The result reaches
the test through `ExitProcess`.

Each invocation starts a new PE32 process with an eight-MiB zero-filled stack,
sixteen-byte call alignment and `FNINIT` initialization. The assembly bridge
restores the launcher's stack and preserved registers after the oracle returns.
This preserves the test process-entry conditions; it establishes no language
guarantee for uninitialized local objects.

The launcher accepts bounded static little-endian i386 ELF32 images. It checks
header and program-table sizes, file ranges, segment memory ranges, overlap,
the executable file-backed entry and the complete console trampoline. Dynamic
and interpreter headers reject. Allocation, file and console errors return
controlled status codes. Instruction-cache flushing precedes execution. The
helper retains a thirty-second aggregate assembly, link and execution deadline.

The mapped oracle region is executable and writable because these tests
exercise emitted JIT bytes. The launcher is a test utility for repository-owned
oracle programs, not a general ELF execution service.

## Tests and preserved failures

The integrated ten-module Windows replay passes all 503 methods with no skips
in 57.829 test seconds. The matching Linux replay passes 503 selections in
9.811 test seconds, with only the eight Windows-specific launcher methods
skipped. Both runners recheck their captured compiler and fixture source bytes.

The eight launcher methods execute native return values, exact console bytes,
Unicode paths, fresh stack memory and x87 initialization. They also check
argument and file failures, malformed ELF headers and segments, overlap,
non-executable entries, missing trampoline bytes, gaps and BSS entries,
nonreturning code and an assembler failure. A valid execution still succeeds
after all malformed-image controls.

The first prototype used the launcher's existing stack. Its 325-method replay
failed the const SIMD array case, whose fixture expects untouched stack bytes
to be zero. Supplying the fresh stack and initialized x87 state fixes that
process setup; the compiler and fixture retain their behavior. The original
failed replay and the subsequent passing prototype remain archived.

Complete repository test discovery also needs its explicit package root.
The normal test recipe now uses `-s tests -t .`, so relative imports in five
existing modules load correctly. A separate fresh Cupid-built ABI-command
fixture now captures the root `.inc` files alongside its headers, including
`fat16_name_profiles.inc`. Its complete ten-method Windows replay passes in
226.871 test seconds, with one expected POSIX-only skip.

The preceding complete-suite process ended after its scratch subtree became
unavailable. Its missing final log prevents a complete-suite success claim.
New validation logs are retained outside the repository's `build` directory.
The cause of that subtree's removal remains unverified.

## Ownership and reproduction

Clang, LLVM's linker and the Windows SDK x86 Kernel32 import library remain
optional development-oracle dependencies. They do not enter the normal OS
build. The helper reports missing prerequisites as an explicit test skip;
compiler, assembler, linker and launcher failures remain failures.

The audit records the two launcher source files outside the supported Make
roots. Its explicit suffix policy records the native launcher `.cc` file as
`not_reached`; the assembly remains outside active production assembly.
Neither file increases Cupid-owned OS progress. Normal ownership remains
449 CupidBuild actions and three Python coordinators across 452 transforms.
Installed seeds and accepted OS image bytes retain their preceding contracts.
TempleOS remains reference material and is excluded.

Run the focused launcher and compiler modules with:

```powershell
python -m unittest -v tests.test_windows_i386_oracle tests.test_private_cupidc_call_abi
python -m unittest -v tests.test_cupidbuild_user_abi_cli
python -m unittest discover -v -s tests -t . -p "test_*.py"
```

The [evidence index](evidence/native-windows-private-oracle-20261012.json)
binds the integrated replays, source bytes, prototype failures and raw logs.
A complete repository suite remains pending at this record's publication.
