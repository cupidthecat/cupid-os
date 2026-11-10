# ADR 0272: Adopt a checked native Windows execution seed

## Status

Accepted on 2026-08-13.

## Context

ADRs 0268 and 0269 proved that both source-head fixed-point stages produce the
same native Windows images for CupidASM, CupidC, CupidDis, CupidLD, and
CupidObj. Windows ran useful success and failure cases for all five tools. The
normal build still selected the checked Linux executables and launched them
through WSL.

The native images are suitable production executors, but they are not yet a
native self-rebuilding fixed point. Their provenance comes from paired Linux
bootstrap stages built from revision
`384c74d026b50e469f60dd7f6409f4b185df4ef7`. The seed format must state that
boundary rather than reuse the stronger Linux fixed-point claim.

## Decision

Carry the five PE32 images under `bootstrap/seeds/i386-windows/` with a strict
`cupid.execution-seed.v1` manifest. The manifest binds each file name, size,
SHA-256, producer role, target profile, parent Linux seed, 50-input source
snapshot, producing revision, paired-stage result, and producer lineage.

Validate the PE layout and exact `KERNEL32.dll` imports before any tool runs.
CupidLD has four publication imports in addition to the shared twelve imports.
Reject extra `.exe` files in the seed directory.

Select a production seed by host. Windows uses the checked PE cohort directly;
Linux keeps the checked static ELF cohort. Root and user output-bearing recipes
receive the production manifest explicitly. Fixed-point reconstruction,
Toolchain contract publication, the user ABI contract, and artifact-size policy
continue to use the Linux bootstrap manifest because those operations consume
its build-plan provenance.

`run_seed_tool` still freezes the complete five-tool cohort and rechecks the
live manifest and every image after execution. Its runner now selects direct
Windows execution for PE files and WSL only for Linux ELF files on Windows.

## Evidence

The checked native images are:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,664 | `02db72024a1e337e6890a310cf06532eae04732c14ec55df4f58597da27e263e` |
| CupidC | 2,594,304 | `209b493c73ff2b30ef38f0161491dacd5564f995a019876d96e8bc805b5c83e9` |
| CupidDis | 378,368 | `d7bcb02bf3c1491de3c3adc37ecb4e966501e49e9eebd2c7d7d18b65d2c3fa91` |
| CupidLD | 296,448 | `afe3c34e892a70e30774dfa2358d615f87598ea5ade74f6b15d94ef9a75e8439` |
| CupidObj | 375,808 | `3546e71ad17ea9729a948c7144cbb08ca0991066950129ecf18919d76ba0e36d` |

Manifest verification passes for both platform seeds. On Windows, all five
native tools return 0 for help and 2 for an invalid option without probing for
WSL. A real native CupidC invocation builds `kernel/core/string.o`. Native
CupidASM and CupidDis assemble and validate the SMP trampoline. Native CupidC
and CupidLD build and link `user/examples/hello.cc` as a static Cupid OS ELF.
The later ADR 0274 promotion gives every image a fully committed one MiB stack;
native CupidC compiles the unchanged keyboard driver to the same 11,740-byte
object as the Linux seed.

The final promotion bootstrap started from the clean named revision and
finished in 801.9 seconds. Its 50-input snapshot is
`5bfbca2cbe30f2fa4b638cbf462b306cc05dc50a4604fd887f89426dbe091e63`.
All 19 C objects, the startup object, and the five Linux tools match between
stage two and stage three. The five native PE images also match between those
stages, and the 5-help, 18-success, and 16-failure behavior matrix passes. The
38,164-byte report has SHA-256
`3c63664f08e7bcdc639a88ca6ada6cf5143100eac966d748660b65d537b01e10`.

Negative tests reject an unlisted PE file and preserve the existing production
output when a checked tool fails.

The source-current poisoned-host `make -j2 all` build used the checked PE seed
for every output-bearing Cupid invocation. Its first command stopped at the
602.5-second harness limit; the resumed build completed in 968.5 seconds. The
final link, 430-input CupidDis gate, nine-artifact size check, and image
publication all passed. The resulting image then passed the four-vCPU RTL8139
frontier in 820.7 seconds.

## Consequences

Normal Windows output-bearing Cupid tool calls no longer require WSL. Linux
behavior is unchanged. Host Python and Make still coordinate the build.

This execution seed is one step short of a native Windows fixed point. A later
stage must use the checked PE producers to rebuild and compare the next PE
generation, then promote that stronger provenance. The checked Linux seed
remains the fixed-point root until that proof exists.

No source was reduced or renamed to fit the tools. `TempleOS/` remains
read-only reference material.
