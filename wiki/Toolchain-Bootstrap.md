# Toolchain Bootstrap

Cupid OS carries a checked static i386 Linux seed for its five hosted tools:
CupidC, CupidASM, CupidDis, CupidLD, and CupidObj. The seed starts a complete
toolchain rebuild without GCC, Clang, NASM, a host linker, `nm`, or `objcopy`.

The seed lives under `bootstrap/seeds/i386-linux/`. Its manifest records each
file's size and SHA-256 value, the static i386 Linux ABI, entry point, source
revision, producer lineage, all 19 C sources, startup, include arguments, and
the exact link order for every tool. Verification pins every source name,
path, position, and GNU-mode flag rather than trusting a manifest-supplied
digest alone. It also rejects unknown fields, dynamic images, an interpreter,
duplicate JSON keys, numeric and Boolean type substitutions, writable
executable load segments, entry points outside executable file bytes, unlisted
ELF files, and unexpected target metadata.

```sh
make verify-bootstrap-seed
```

This command validates the seed without executing it.

```sh
make bootstrap-from-seed
```

The full command captures the hashes of all 40 current source inputs: 19 C
sources, startup, 19 project headers, and `link.ld`. Checked CupidC compiles
stage two, checked CupidASM assembles its startup, and checked CupidLD links all
five tools. The stage-two producer trio repeats the build for stage three.

The gate compares all 19 C objects, both startup objects, and all five linked
images. It also runs five help checks, ten successful operations, and six
failure cases across compilation, assembly, disassembly, symbol inspection,
linking, wrapping, and flattening. A source edit during either stage stops the
build instead of publishing mixed evidence.

Before execution, the harness reads the manifest and each seed binary once. It
verifies those captured bytes, keeps the manifest hash, and runs private copies
of the five binaries. A later replacement of a checked-in file cannot change
that run.

The default output is `build/bootstrap/checked-seed/`. It contains both stages,
the behavior fixtures, and `bootstrap-report.json`. The report keeps the
historical seed source revision separate from the current source snapshot.

Linux runs private copies of the static tools directly. Windows stages each
copy in a mode-0700 WSL directory created by `mktemp`. Native Windows seed
executables are not available yet.

This seed makes the hosted static toolchain reproducible from a clean checkout.
It does not complete the normal OS build migration. Native contract runners,
hosted development commands, and the remaining normal C objects still use a
host C compiler.
