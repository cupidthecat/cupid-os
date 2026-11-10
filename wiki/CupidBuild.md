# CupidBuild

CupidBuild coordinates checked compiler, assembler, object and linker commands.
It verifies the selected seed cohort, captures the command's inputs, checks
the result and publishes it under an output lock. Failed checks preserve the
existing published output.

## Build the operating system

Run these commands from the repository root:

```sh
make
make -C user
```

GNU Make schedules the build graph. CupidBuild owns 449 of its 452 transforms
across the OS, user and Toolchain roots. Python still coordinates final disk
images and Toolchain verification/publication. GCC, Clang and NASM serve
optional development builds and comparison tests.

See [Getting Started](Getting-Started) for host requirements and
[Toolchain Bootstrap](Toolchain-Bootstrap) for staged rebuilds and releases.

## Inspect the command line

Linux or WSL:

```sh
bootstrap/seeds/i386-linux/cupidbuild.elf --help
```

Native Windows:

```powershell
./bootstrap/seeds/i386-windows/cupidbuild.exe --help
```

Keep the six tool images, manifest and `bootstrap/seeds/release.json` together.
Seeded commands accept `--seed-release`; the normal Make recipes supply it.
Use the manifest for the host that will execute the command.

| Command group | Work |
| --- | --- |
| `assemble-*` | Assembly objects, bootloader, SMP trampoline and ISO pattern |
| `compile-kernel`, `compile-doom` | Normal kernel sources and the Doom profiles |
| `compile-production`, `compile-user`, `link-user` | Generated install tables and external examples |
| `embed-jpeg`, `generate-ksyms`, `flatten-kernel` | JPEG embedding, kernel symbols and flat kernel output |
| `generate-profile-manifest`, `publish-iso-fixture` | Frozen Doom input profile and test ISO publication |
| `verify-artifact-sizes`, `verify-user-abi` | Artifact policy and the user syscall table |
| `run` | Checked invocation of CupidC, CupidObj or CupidLD |

The source and output paths for normal transform commands follow their declared
build profiles. `run` also requires an operation-specific argument shape.
The Makefiles contain the complete argument and input lists. Use CupidBuild's
own `--help` to inspect the command inventory.

## When a command fails

Read the diagnostic before replacing files. A manifest or release mismatch
means the selected images do not form the expected cohort. An input-drift
diagnostic means a captured file, path binding or directory membership changed
during the command. Use a complete reviewed seed pair and rerun with stable
inputs. Each output has an exclusive publication lock; let its owning process
finish before another command targets that path.

[Architecture](Architecture) describes the tool adapters and object formats.
[ELF Programs](ELF-Programs) covers the separate user-program build and loader.
