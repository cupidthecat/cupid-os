# Reproducible oracle baseline

The baseline runner freezes what the current external host toolchain produces before Cupid tools take ownership. It is an oracle and regression aid, not part of the fixed-point toolchain.

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

1. Resolves and hashes the Make, Python, C compiler, NASM, linker, `objcopy`, `nm`, and QEMU executables and records their version output. It also records the availability, versions, and hashes of optional PATH-dependent JPEG normalizers (`jpegtran`, `djpeg`/`cjpeg`, and FFmpeg) that can change embedded bytes. Conversion can fall through per file, so artifact hashes—not this availability inventory—are the authority for what was produced.
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
| Windows AMD64, Clang/LLVM | `c1df30c` | PASS: two clean 431-artifact builds matched; aggregate SHA-256 `11febd09886aba270b9e3780774f1d0ab5e825146bf496e18e7cf9fb23ff596f` | `baselines/windows-amd64.json` |
| Linux, GCC/binutils | Pending | A separate capture is required; cross-toolchain equality is not expected | Not captured |

This capture supersedes the 430-artifact shared-ELF32 evidence from `1b5901c`. The checked audit contract proves that the 431-path manifest includes all 424 final-link objects, including the shared runtime core, ELF32 module, x86 instruction model, and kernel adapter. The current capture also passed 48 host tests in 12.096 seconds, `/bin/ls.cc` through CupidC in 18.628 seconds, and `as /demos/hello.asm` through CupidASM in 23.229 seconds. Its two isolated builds took 8.754 and 7.792 seconds. The resulting kernel ELF is 6,249,088 bytes, its raw binary is 6,039,445 bytes, `.text` is 1,369,924 bytes, and the disk image is 209,715,200 bytes; all times and sizes are oracle observations, not performance gates.

## Build-output hygiene

`.gitignore` excludes compiler objects, OS binaries/images, generated build intermediates, Python caches, runtime logs, and ordinary baseline output. `.gitattributes` fixes text checkouts to LF so embedded Cupid C, Cupid ASM, documentation, and other text do not change bytes with host checkout policy. Binary fixtures and assets are explicitly marked binary.

`make clean` removes objects, generated C intermediates, runtime logs, and Python caches while preserving `cupidos.img`. `make clean-image` removes that image. `make distclean` performs both and also removes the disposable USB image, root `build/`, and hosted `toolchain/build/` output. Checked fixtures, packet captures, the `TempleOS/` reference tree, `.agents/`, and `skills-lock.json` are not hygiene targets.
