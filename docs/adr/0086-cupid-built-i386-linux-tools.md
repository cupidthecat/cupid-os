# Run Cupid-built tools on a repository-owned i386 Linux runtime

- Status: Accepted
- Date: 2026-07-23

## Context

ADR 0085 linked one CupidC-emitted host adapter with CupidASM startup code and CupidLD. Its controlled providers proved object, relocation, and calling-convention compatibility, but they did not implement file or heap services and no tool `main` ran.

The unchanged CupidASM, CupidDis, CupidLD, and CupidObj command adapters need a small hosted C surface. Their combined imports cover allocation, unbuffered files, standard streams, formatted diagnostics, strings, `errno`, and the working directory. Relying on a system i386 libc would add host headers, startup objects, and multilib availability to the bootstrap. Replacing those calls with stubs would create executables that could not perform their advertised work.

The current Linux and WSL kernels can run static i386 ELF files that enter through `int 0x80`. CupidC already emits every C object in the four command closures under the checked four-byte ABI. CupidASM and CupidLD already provide the remaining assembly and link operations.

## Decision

Cupid OS owns a narrow i386 Linux runtime under `toolchain/hosted/i386-linux`. CupidC compiles `runtime.c`, and CupidASM assembles `start.asm`.

The startup code reads `argc` and `argv` from the initial process stack, aligns ESP for the i386 cdecl call to `main`, and exits with `main`'s status. It also exports one-, two-, and three-argument Linux system-call wrappers.

The C runtime implements the checked declaration set:

- a reusable `brk` allocator with `malloc`, `calloc`, `realloc`, and `free`;
- unbuffered `FILE` objects for standard output and standard error;
- open, close, seek, tell, read, write, flush, and sticky stream-error operations;
- `errno` storage and `getcwd`;
- the required memory and string functions;
- formatted output for the conversions used by the hosted commands.

`calloc` rejects multiplication overflow with `ENOMEM` and zeroes each successful allocation. The runtime uses CupidC's supported variadic built-ins for `fprintf`, so only `runtime.c` enables the GNU extension profile. Every unchanged tool source remains in strict C11 mode.

The object contract builds four complete command closures and one runtime-test closure. Each of the fifteen source objects is emitted twice and must be byte-identical. Every C source uses the four-byte target profile, startup is assembled by CupidASM, and CupidLD links each static executable at `0x08048000`. Each link repeats in the same job and must produce the same bytes and result record. Removing the final runtime object must produce an undefined-symbol diagnostic, empty link output, a zero result, and a rewound scratch arena before the successful link can be reproduced.

The public behavior contract runs the generated commands on Linux or through WSL. It compares them with the native hosted commands for raw and ELF32 assembly, raw disassembly including mixed mode maps, fixed-text linking, object wrapping, include resolution, missing files, invalid assembly, and malformed linker input. Exit status, standard output, standard error, and produced bytes must agree. Failed linking must preserve the existing output file. The generated runtime contract checks process arguments, heap reuse and tail release, allocation overflow, file and seek behavior, formatting errors, working-directory errors, and the checked memory and string functions.

## Rejected alternatives

Linking a system i386 libc was rejected because the bootstrap would depend on host headers, startup files, ABI defaults, and multilib packages. The checked declaration set and runtime keep that boundary in the repository.

Keeping the tracer's failing providers was rejected because they cannot run a real command.

Reserving a large static heap in BSS was rejected because it would inflate every executable and impose an arbitrary fixed ceiling. The `brk` allocator grows on demand and can release a free tail.

Rewriting the command adapters around a smaller runtime was rejected. Their ordinary C interfaces are appropriate hosted code and should drive the toolchain surface.

Implementing all runtime logic in assembly was rejected. Cupid C is the intended systems language, while Cupid ASM is the right boundary for process entry and system calls that CupidC cannot yet express.

## Consequences and evidence

The four commands and runtime contract are:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,036 | `2742A053D6D72B058A4A911BF9D70A57A03E11B634E67F05E941555C91245AF9` |
| CupidDis | 366,944 | `41494249AAAD604B0147E393600F9BDB6A2FD20F0F9B97F7B97E54933B126783` |
| CupidLD | 258,268 | `4E7BCE5C46547253E90BB1CEAC434A1663063F89DF0F7D6B0C7436BBA00EDBE5` |
| CupidObj | 182,684 | `D23072BC96E5736AA5F09E30151B2C6844DC3831D37B4674C669E134269D9965` |
| Runtime contract | 42,696 | `01EF2F94AD6330B59E3AC7C751776F88BBE67FBEA1CF82BBD2510928516A8BCE` |

Windows Clang and Linux GCC builds of the native contract produce byte-identical copies of the four commands. WSL runs each generated command and the runtime contract through real positive and failure cases. The 45 public CupidC object tests pass. The default hosted-tool build regenerates all five Cupid-built executables and its LF-only hash manifest on every run, so a missing or damaged side output cannot remain hidden behind a current stamp.

This runtime is deliberately small. It is single threaded, uses unbuffered streams, supports only the checked headers, and targets static i386 Linux executables. It is not a general libc or a Windows runtime.

The host compiler still builds the native oracle, contracts, and every normal Cupid OS C object. There is no host-runnable CupidC driver, compiler-generation comparison, checked seed, or production build handoff yet. Issue #27 remains open for that compiler stage.
