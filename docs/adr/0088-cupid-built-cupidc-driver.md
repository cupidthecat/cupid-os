# Run CupidC as a Cupid-built i386 Linux compiler

- Status: Accepted
- Date: 2026-07-23

## Context

ADR 0086 produced four static Cupid-built commands, but the compiler still had no command-line driver. The object contract could call the compiler operations in process, yet a Cupid-built compiler could not receive a source path, create an object file, or begin a compiler generation.

The first compiler executable has to preserve the same source and profile used by generation zero. Native Windows paths and WSL paths cannot enter the object identity. The driver must also keep the output transaction intact when preprocessing, parsing, or emission fails.

## Decision

`toolchain/cupidc_main.c` provides a compile-only driver:

```text
cupidc -c INPUT -o OUTPUT [-I PATH] [-D NAME[=VALUE]] [-U NAME]
       [--gnu] [--freestanding] [--root NATIVE_ROOT]
```

The default is hosted C11. `--freestanding` changes the hosted-environment fact, while `--gnu` enables the represented GNU language surface. The target pointer width is always four bytes. An attempt to define or undefine `__SIZEOF_POINTER__` on the command line is rejected.

The driver uses `ctool_invoke` for input loading, ordered diagnostics, and a commit gate before writing output. Its body runs the shared preprocessor, frontend, and ELF32 emitter. A failed compilation therefore leaves an existing output untouched. The current file adapter writes the destination directly after compiler success, so an operating-system write failure can still leave a partial file. Compiler-sized jobs may use 64 MiB for source and output and 512 MiB for arena storage.

Without `--root`, native paths are converted to canonical logical paths. With `--root`, input, output, and include arguments must already be absolute logical paths. The native root is used only by the host adapter. This keeps object bytes stable when the same source is compiled by the Windows bootstrap executable and the static compiler under WSL.

The static compiler closure contains twelve objects: startup, runtime, the driver, `ctool`, `ctool_host`, preprocessing, types, frontend, IR, emission, ELF32, and x86. Every C object uses the checked four-byte i386 Linux profile from ADR 0082. The runtime now supplies `memcmp`, and the link helper accepts the twelve-object closure without loosening symbol or relocation checks.

The active preprocessing audit follows the complete static Make closure. Nineteen C files use strict `HOSTED_I386_LINUX`; `runtime.c` alone uses `HOSTED_I386_LINUX_GNU` for its represented variadic built-ins. Both profiles use `/toolchain`, the checked i386 Linux angle-header root, and `__SIZEOF_POINTER__=4`. The audit also pins the object-contract input and the `self-host-link-tools` recipe so a dependency list alone cannot claim this ownership.

The first generation check uses the native CupidC driver as generation zero. The static Cupid-built driver compiles unchanged `toolchain/cupidc_ir.c` twice as generation one. All three objects must be byte-identical valid i386 relocatable ELF files.

## Rejected alternatives

A wrapper around the object contract was rejected because it would not establish a real compiler process or commit-gated compiler output.

Reading host system headers was rejected because the compiler target is i386 Linux and the repository already owns the checked ABI declarations.

Reusing objects from `HOSTED_TOOLCHAIN_64` was rejected because those preprocessing facts describe an eight-byte bootstrap process. They do not describe the four-byte executable being linked.

Comparing a small fixture alone was rejected. `cupidc_ir.c` is a substantial compiler implementation source and exercises the compiler on its own unchanged code.

One reproduced source is not called a fixed point. A full generation must rebuild every compiler object, link the next compiler, and repeat the comparison.

## Consequences and evidence

The static compiler is a 1,812,712-byte i386 `ET_EXEC` at `0x08048000`. Its SHA-256 is `476522EEAC38F26E6BE201F9C435F3617BD7F7ECEF472B98D053E1DE575BCD84`. It has no undefined symbols and defines `_start`, `main`, and `ctool_c_emit_object`.

Generation zero and both generation-one runs produce the same 371,240-byte object for `cupidc_ir.c`. Its SHA-256 is `3E8C4F8EA98C303C4AE3272F493B3B09E20B9DB362688B99058D32FB1E7B776C`.

The command contract also compares hosted and Cupid-built output for include roots, definitions, undefinitions, GNU mode, and freestanding mode. A binary conditional constant proves that GNU mode reaches preprocessing as well as parsing, while the strict C11 command rejects the same source. Malformed input and a missing source file produce matching diagnostics while preserving a sentinel output. Pointer-width definition and undefinition attempts fail before creating output. The static artifact group recovers when the compiler executable is removed.

This is a real compiler-on-compiler generation step, but it covers one compiler source. The host compiler still builds generation zero, the contract runners, and normal Cupid OS C objects. A complete stage-two compiler, a stage-two to stage-three fixed-point comparison, checked seeds, and production build ownership remain open under issue #27.
