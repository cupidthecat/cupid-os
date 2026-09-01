# Link one CupidC host adapter with CupidASM and CupidLD

- Status: Accepted
- Date: 2026-07-22

## Context

CupidC emits deterministic i386 ELF32 objects for every source in issue #27's toolchain cohort. The three hosted adapters use the checked four-byte Linux profile from ADR 0082. Their symbol and relocation inventories were known, but no test had passed one of those objects through a linker or executed code from the result.

A complete hosted tool needs startup code and working file, allocation, string, and diagnostic services. Those services are still unfinished. Linking a main adapter against functions that always fail would produce a file called a tool without proving tool behavior. Waiting for the whole runtime, however, would leave basic object compatibility and relocation handling untested.

The current WSL environment lacks the 32-bit startup files and libraries needed by `gcc -m32`, but its kernel can run a static i386 executable that uses `int 0x80`. CupidLD already writes static i386 executables and understands both relocation kinds in `ctool_host.c`.

## Decision

The CupidC object contract has a static i386 link tracer for `ctool_host.c`. It performs these steps in one bounded toolchain job:

1. CupidC compiles the unchanged adapter with the checked four-byte Linux profile.
2. CupidC compiles a small link provider for `stderr`, file calls, allocation, and release. A separate object supplies `__errno_location`.
3. CupidASM assembles `_start`. The startup aligns ESP, calls `ctool_host_adapter_init`, checks the returned status and both fields of the `{data, size}` result, and exits through Linux system call 1.
4. CupidLD links the four objects at `0x08048000`. A repeat link must produce the same bytes and result record.
5. A negative link omits the errno object. CupidLD must report an unresolved symbol, keep the output empty, zero its result, and restore its scratch arena. The same job must then reproduce the successful executable.

The provider exists only to resolve the adapter's exact ten imports. Its file functions and allocator do not implement a usable runtime, and `_start` never calls them. The public runtime test executes the file on Linux or through WSL when static i386 execution is available. The cross-platform contract still builds and inspects the executable when that kernel facility is absent.

## Rejected alternatives

Calling `cupidasm_main` or `cupiddis_main` with failing file and heap stubs was rejected. Such a program could only exercise an error path, and calling it a runnable tool would hide the missing runtime.

Using the native Windows ABI was rejected because this object profile is i386 Linux ELF with cdecl and Linux runtime symbols. A Windows host tool needs a separate PE/COFF and runtime design.

Using an unresolved-symbol linker option was rejected because it would bypass the compatibility question this test is meant to answer.

Adding a partial production libc was rejected for this step. The first real hosted tool should use a deliberate runtime boundary and run existing behavior fixtures. The tracer does not decide whether that boundary is a temporary 32-bit system libc link or a later Cupid-owned runtime.

## Consequences and evidence

The linked executable is 21,592 bytes and has SHA-256 `80F037F75B085DFE9A047EB442F30FE8601321A2E055DF8F7FA02B7C48F49BDC`. It contains four allocated sections, five program headers, 24 resolved symbols, and 45 applied relocations. Its ELF symbol table has 25 entries including the null entry, no relocation records, and concrete definitions for all ten former adapter imports.

The contract locks the entry at `0x08048000`, the file-backed end at `0x0804B002`, and the memory end at `0x0804C010`. Its text section is 5,897 bytes. WSL executes the static i386 file with exit status zero, which proves that CupidC code, CupidASM startup code, and CupidLD layout agree on this narrow ABI call.

This is a link and execution tracer for adapter initialization. It is not a hosted C runtime, a runnable CupidASM or CupidDis, a CupidC driver, a bootstrap generation, or a production ownership transfer. Every object in a future executable closure must use the four-byte target profile. The existing hermetic source gate's eight-byte hosted process fact cannot be mixed into that closure.
