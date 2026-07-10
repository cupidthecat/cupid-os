# Reproducible oracle baseline

The baseline runner freezes a committed normal build—including Cupid-owned stages and remaining host-owned stages—as reproducibility and oracle evidence during ownership transfer. It is not part of the fixed-point toolchain.

## Entry points

Run all host-side unit tests:

```sh
make test
```

Capture a full baseline for the committed `HEAD` revision:

```sh
make bootstrap-baseline
```

The default evidence path is `build/bootstrap/<system>-<machine>.json`. To freeze evidence at a reviewed path or capture another committed revision, invoke the runner directly:

```sh
python tools/bootstrap_baseline.py \
  --revision <commit> \
  --output docs/bootstrap/baselines/<host>.json
```

The revision must resolve to a commit. Working-tree changes and untracked files are deliberately excluded: each run gets a disposable detached Git worktree. This makes the recorded revision reproducible and prevents the baseline from overwriting a developer's objects or guest-mutated disk image.

## What one capture proves

The runner:

1. Resolves and hashes the Make, Python, C compiler, `nm`, and QEMU executables still required by the normal build or runtime gates and records their version output. Retired host assembler, standalone linker, and object-copy tools are no longer preflight requirements. NASM availability is recorded separately under `optional_oracle_tools` and never fails preflight. The runner also records the availability, versions, and hashes of optional PATH-dependent JPEG normalizers (`jpegtran`, `djpeg`/`cjpeg`, and FFmpeg) that can change embedded bytes. Conversion can fall through per file, so artifact hashes—not this availability inventory—are the authority for what was produced.
2. Creates two independent worktrees for the requested revision.
3. Runs `distclean`, then a complete image build with fixed disk geometry and `WAD_SRCS=` so optional host WAD discovery cannot change the image.
4. Asks Make for the final ordered `KERNEL_OBJS` cohort after every conditional/discovered append, plus the boot, trampoline, pass-1 ELF, generated symbols, final ELF, raw kernel, and freshly formatted disk-image boundaries. `make check-bootstrap-audit` independently proves that every linked object is present in this manifest.
5. Records each artifact's byte size and SHA-256 and compares every later run with run 1. Any missing, extra, or changed artifact fails the capture and is named in `reproducibility.mismatches`.
6. On run 1, executes all host unit tests, the explicit `/bin/ls.cc` CupidC GUI smoke, and the `as /demos/hello.asm` CupidASM GUI smoke. Each check records its command, status, duration, output digest, and diagnostic tail.
7. Reads the final ELF `.text` size without host `objdump` and records kernel/image sizes. Host wall-clock durations are observational evidence only; they are not the future 20% guest-performance gate.

The runner writes failure evidence when tool preflight, build, tests, runtime smoke, artifact collection, or reproducibility comparison fails. Dependent checks are marked skipped after the first command failure.

## Cross-host interpretation

Windows Clang/LLVM and Linux GCC/binutils evidence are separate oracle baselines and are not expected to be byte-identical to each other. Reproducibility means two clean builds match on one recorded host/tool set. Later Cupid stages have the stronger byte-identical stage-2/stage-3 contract described in `../adr/0004-deterministic-bootstrap-seeds.md`.

Checked evidence belongs under `docs/bootstrap/baselines/`. Ordinary runs stay under ignored `build/bootstrap/`. The JSON has schema identifier `cupid.bootstrap-baseline.v1`; schema changes require tests and a bootstrap-log entry.

| Host oracle | Source revision | Result | Evidence |
| --- | --- | --- | --- |
| Windows AMD64, Clang/LLVM | `6731dd6` | PASS: two clean 431-artifact builds matched; aggregate SHA-256 `a1f4a1b10fd326318ad0c2b861a050ca95dc225d6aea95caa6e7864d8d6d5fdf` | `baselines/windows-amd64.json` |
| Linux, GCC/binutils | Pending | A separate capture is required; cross-toolchain equality is not expected | Not captured |

This capture supersedes the shared-CupidASM evidence from `50c5b04`. The checked audit contract proves that the 431-path manifest includes all 424 final-link objects, including the shared runtime core, ELF32 module, x86 instruction model, CupidDis, CupidASM, and every CupidObj-produced wrapper. The current capture passed 82 host tests in 52.467 seconds (one Windows-only skip), `/bin/ls.cc` through CupidC in 18.882 seconds, and `as /demos/hello.asm` through CupidASM in 23.246 seconds. Its two isolated builds took 14.387 and 12.975 seconds. The resulting kernel ELF is 6,276,332 bytes, its raw binary is 6,064,029 bytes, `.text` is 1,390,468 bytes, and the disk image is 209,715,200 bytes; all times and sizes are oracle observations, not performance gates.

## Build-output hygiene

`.gitignore` excludes compiler objects, OS binaries/images, generated build intermediates, Python caches, runtime logs, and ordinary baseline output. `.gitattributes` fixes text checkouts to LF so embedded Cupid C, Cupid ASM, documentation, and other text do not change bytes with host checkout policy. Binary fixtures and assets are explicitly marked binary.

`make clean` removes objects, generated C intermediates, runtime logs, and Python caches while preserving `cupidos.img`. `make clean-image` removes that image. `make distclean` performs both and also removes the disposable USB image, root `build/`, and hosted `toolchain/build/` output. Checked fixtures, packet captures, the `TempleOS/` reference tree, `.agents/`, and `skills-lock.json` are not hygiene targets.
