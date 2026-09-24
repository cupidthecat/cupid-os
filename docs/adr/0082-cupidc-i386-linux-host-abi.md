# Compile the hosted adapters against a checked i386 Linux ABI

- Status: Accepted
- Date: 2026-07-22

## Context

Issue #27 has a fourteen-file CupidC, CupidASM, and CupidDis source cohort. Hosted CupidC already emits deterministic i386 ELF32 objects for ten hermetic cohort files and the `kernel/lang/as_elf.c` bridge. The same gate also covers the adjacent CupidLD and CupidObj cores. The remaining cohort sources are `ctool_host.c`, `cupidasm_main.c`, and `cupiddis_main.c`.

Those three sources include ordinary hosted headers for file I/O, allocation, strings, `errno`, and the working directory. Letting CupidC read whichever system headers happen to be installed would make the bootstrap depend on host-specific macros, extensions, include paths, and declarations. Keeping the files outside the source gate would leave the cohort incomplete. Rewriting them around compiler limitations would put bootstrap policy into otherwise normal C.

The emitted objects use the i386 cdecl ABI, so the existing `HOSTED_TOOLCHAIN_64` profile is also wrong for this job. That profile describes the 64-bit compiler process which builds the current contracts. It does not describe the target that will run a CupidC-built ELF32 tool.

## Decision

The repository owns a small i386 Linux hosted declaration set under `toolchain/hosted/i386-linux/include`. It declares only the hosted ABI surface used by the three adapters:

- `errno.h` provides `errno`, `ENOENT`, and `ERANGE` through `__errno_location`.
- `stdio.h` provides the opaque `FILE` type, standard streams, and the required file and formatted-output calls.
- `stdlib.h` provides allocation and release calls.
- `string.h` provides the required memory and string calls.
- `unistd.h` provides `getcwd`.

`cupid_host_abi.h` owns the target `size_t` definition and rejects any profile where `__SIZEOF_POINTER__` is missing or is not four. The declarations are an ABI boundary, not a C library implementation. A later host link must still supply a compatible i386 C runtime.

The checked profile runs in hosted C11 mode with two ordered roots. `/toolchain` resolves project headers and `/toolchain/hosted/i386-linux/include` resolves angle includes. The profile explicitly defines `__SIZEOF_POINTER__` as four and does not define `_WIN32`, so the unchanged adapters select their Linux branches.

The object contract preprocesses, parses, lowers, and emits each adapter twice. It reads each result through Cupid's ELF32 reader, locks its public inventory, and requires byte-identical output. A missing ABI root, a missing pointer-width fact, and an eight-byte pointer profile fail with exact preprocessing diagnostics. The same job then succeeds with the four-byte profile.

## Rejected alternatives

Using the build machine's system headers was rejected because those headers are not a deterministic language input and expose a much larger GNU and platform surface than this cohort requires.

Copying complete libc headers into the repository was rejected because the bootstrap only needs stable declarations. Full headers would add unrelated macros, implementation details, and maintenance work.

Changing the adapters to avoid `FILE`, `errno`, or ordinary allocation and string calls was rejected. Their current source already expresses the hosted platform adapter cleanly.

Reusing `HOSTED_TOOLCHAIN_64` was rejected because an eight-byte pointer fact does not match i386 object layout or cdecl.

## Consequences and evidence

All fourteen issue #27 source files now reach deterministic CupidC object emission without changing their implementation source. The new rows are:

| Source | Functions | `.text` bytes | Object bytes | Fingerprint | Symbols | Relocations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `toolchain/ctool_host.c` | 11 | 5,522 | 6,944 | `28739C3F` | 25 | 38 |
| `toolchain/cupidasm_main.c` | 13 | 9,455 | 12,384 | `561BBC22` | 56 | 88 |
| `toolchain/cupiddis_main.c` | 13 | 13,816 | 17,420 | `E33C130C` | 67 | 106 |

The three objects have exact undefined import sets of 10, 31, and 31 symbols. Their relocation splits are 28 `R_386_PC32/-4` plus 10 `R_386_32/0`, 72 plus 16, and 74 plus 32. Every record belongs to `.rel.text`, names a known symbol-table entry, has a bounded four-byte field, and carries a recovered addend. This is a checked linker-facing inventory, not a successful link.

An independent `gcc -m32 -nostdinc` syntax pass accepts each unchanged source against the checked declarations. This confirms the declaration set under a second C frontend, but it does not prove a linked runtime.

This decision completed the source-to-object boundary. ADR 0085 later links the `ctool_host.c` object with CupidASM startup and CupidLD and runs its initialization call. That tracer uses test-only symbol providers rather than a C runtime. It does not build a complete host-runnable tool, compare tool behavior, create bootstrap generations, or transfer production ownership. GCC or Clang still builds every hosted executable and every normal C object.
