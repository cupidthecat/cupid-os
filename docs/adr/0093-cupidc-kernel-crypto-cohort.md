# Compile the first kernel crypto cohort with CupidC

- Status: Accepted
- Date: 2026-07-23

Current status: ADR 0098 expands this production cohort from 16 sources to
all 20 `kernel/crypto` sources. The account below preserves the first cutover
and its memory-map decision.

## Context

The normal root build compiled every C object with Clang or GCC even though the
checked i386 Linux CupidC seed could compile most of `kernel/crypto`. A source
frontier found 16 working translation units and four clear blockers:

- `csprng.c` still needs GNU inline assembly.
- `asn1.c`, `x509.c`, and `x509_chain.c` still need a conversion that CupidC
  reports as `CTD000006`.

The 16 working sources produce valid i386 ELF32 relocatable objects with
CupidC, but they are not optimized yet. Their combined object size is 165,112
bytes. The equivalent host objects total 47,820 bytes. The first complete link
failed because the larger code moved `_kernel_end` to `0x00B12910`, 76,048
bytes into the fixed kernel stack.

ADR 0084 rejected an address change as a way to hide checkout-dependent line
endings. That decision still stands. This failure came from intentional
compiler ownership and deterministic object bytes, so the memory map needed a
separate answer.

## Decision

The root Makefile compiles these 16 sources with the checked CupidC seed:

`aes.c`, `aes_gcm.c`, `bigint.c`, `chacha20.c`,
`chacha20poly1305.c`, `ct.c`, `ecdsa.c`, `ed25519.c`, `hkdf.c`,
`hmac.c`, `p256.c`, `poly1305.c`, `rsa.c`, `sha256.c`, `sha512.c`,
and `x25519.c`.

The build wrapper accepts only that list. It verifies and freezes the seed
before use, runs it from a private temporary directory, checks the emitted
object as i386 `ET_REL`, and replaces the requested output only after
validation. A failed compile or invalid object leaves an existing output
untouched.

Every migrated object depends on the wrapper, shared cohort definition,
bootstrap verifier, checked manifest, all five checked seed binaries, and the
root Makefile. Changes to the production path therefore rebuild the cohort
instead of leaving an older object current.

The frontier tool owns the same source list and the four declared blockers. It
rejects any unclassified `kernel/crypto/*.c` file. A run captures all 20 crypto
C sources and every header or include file under the kernel profile, checks
those inputs after each compile, compiles every approved source twice, and
requires byte-identical objects. It publishes the result directory with one
rename after all checks pass.

The frontier and normal wrapper call the same i386 relocatable-object
validator. On Windows, the frontier deletes a staged WSL seed only when the
returned path matches its dedicated `/tmp/cupid-kernel-frontier.XXXXXX`
namespace exactly.

The build graph records each migrated object as a CupidC compile assisted by
host Python. Its closed recipe check fixes the source, output, and
`KERNEL_I386` profile. Ownership is accepted only when GNU Make evaluates the
launcher to Python followed by `tools/cupidc_kernel_compile.py --root .`.
Provenance covers that wrapper, the shared frontier, the seed verifier, and the
seed manifest. The four blocked sources remain ordinary host C transforms.

The fixed memory regions move upward by one MiB:

- The kernel must end at or below `0x00C00000`.
- The two-MiB kernel stack is `[0x00C00000, 0x00E00000)`.
- The two-MiB external ELF arena is `[0x00E00000, 0x01000000)`.
- CupidC still begins at `0x01000000`.

The stack and external arena keep their former sizes. Compile-time assertions
pin their size and adjacency. CupidLD keeps the kernel/stack overlap check, the
bootloader and kernel entry use the new stack top, and the user build links
fixed-address programs at the new external arena base. Its executable rule
depends on `user/Makefile`, so an address change relinks existing outputs.

## Rejected alternatives

Rewriting or shortening the crypto sources was rejected. These files define
the required language and code-generation surface.

Keeping the old map by shrinking the kernel stack was rejected. The one-MiB
gap below CupidC can preserve both fixed regions at their full sizes.

Removing the linker assertion was rejected. An overlapping image would
corrupt the running stack.

Calling the current output optimized was rejected. CupidC has no optimizer
that matches the host `-Os` path yet, and the size difference remains tracked
work.

## Consequences and evidence

The normal build has its first C source cohort whose object production no
longer invokes a host C compiler. Python and WSL remain bootstrap
orchestration dependencies, and most kernel C sources remain host-owned.

The real frontier compiles all 16 sources twice and reports 165,112 matching
bytes. A forced Make build with `CC` set to a nonexistent command rebuilds all
16 objects successfully. The complete two-pass kernel link then ends at
`0x00B12910`, leaving 972,528 bytes below the relocated stack. `_loaded_end` is
`0x006F1B6D`, which remains 2,153,107 bytes below the bootloader's file-backed
kernel limit.

The kernel loader and process contracts pass with the new external arena.
CupidLD still rejects an image that reaches the stack. The three user examples
link as static i386 executables with entry `0x00E00000`.

A current-image QEMU smoke passes all 48 TLS checks, reaches the desktop, and
runs a GUI terminal command. The boot vectors execute code from all 16
migrated sources. SHA-512, SHA-384, bigint, RSA, and Ed25519 now have direct
vectors, including corrupted RSA and Ed25519 signatures that must fail. The
embedded kernel bytes match `kernel/kernel.bin`, and the serial log contains
no panic, exception, memory-corruption, stack-overflow, or stack-guard failure.
ADR 0095 records the added runtime-evidence boundary.

An isolated-image smoke also runs `/home/hello` twice from `0x00E00000`.
The first process exits and releases the external arena before the second
process claims it and loads at the same address. This covers the relocated
external-program path independently of CupidC's `0x01000000` JIT arena.

Persisted external executables linked at the former `0x00D00000` base are not
compatible with this map because that address is now inside the kernel stack.
They must be relinked before use.

The remaining crypto blockers, normal-build host C objects, CupidC
optimization, and removal of Python and WSL from this path remain open.
