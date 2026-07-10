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

1. Resolves and hashes Git, Make, Python, the C compiler, `nm`, and QEMU, then records their version output. The compiler preflight must also produce a real freestanding i386 object with the configured `CC_TARGET`; a version string without 32-bit capability is a failed prerequisite. Linux evidence records `/etc/os-release` distribution identity in addition to the kernel/platform tuple. Retired host assembler, standalone linker, and object-copy tools are no longer preflight requirements. NASM availability is recorded separately under `optional_oracle_tools` and never fails preflight. The runner also records the availability, versions, and hashes of optional PATH-dependent JPEG normalizers (`jpegtran`, `djpeg`/`cjpeg`, and FFmpeg) that can change embedded bytes. Conversion can fall through per file, so artifact hashes—not this availability inventory—are the authority for what was produced.
2. Creates two independent worktrees for the requested revision.
3. Builds all three supported roots in every worktree: root `all` after `distclean` with fixed disk geometry and `WAD_SRCS=`, user `all` after `user:clean`, and hosted toolchain `all` after `toolchain:clean`.
4. Asks each Make root for its declared final artifacts. The root cohort contains the ordered `KERNEL_OBJS` plus boot, trampoline, pass-1 ELF, generated symbols, final ELF, raw kernel, and freshly formatted disk-image boundaries; the user cohort contains the three CupidLD executables; the hosted cohort contains all thirteen contract/CLI executables. `make check-bootstrap-audit` independently proves that every linked OS object is present in the root manifest.
5. Records per-root and combined artifact byte sizes and SHA-256 values, then compares the combined cohort from every later run with run 1. Any missing, extra, or changed artifact fails the capture and is named in `reproducibility.mismatches`.
6. On run 1, executes all host unit tests, the explicit `/bin/ls.cc` CupidC GUI smoke, and the `as /demos/hello.asm` CupidASM GUI smoke. Each check records its command, status, duration, output digest, and diagnostic tail.
7. Reads the final ELF `.text` size without host `objdump` and records kernel/image sizes. Host wall-clock durations are observational evidence only; they are not the future 20% guest-performance gate.

The runner writes failure evidence when tool preflight, build, tests, runtime smoke, artifact collection, or reproducibility comparison fails. Dependent checks are marked skipped after the first command failure.

## Cross-host interpretation

Windows Clang/LLVM and Linux GCC/binutils evidence are separate oracle baselines and are not expected to be byte-identical to each other. Reproducibility means two clean builds match on one recorded host/tool set. Later Cupid stages have the stronger byte-identical stage-2/stage-3 contract described in `../adr/0004-deterministic-bootstrap-seeds.md`.

After both checked files describe the same revision, generate and check the cross-host gate:

```sh
make bootstrap-host-comparison
make check-bootstrap-host-comparison
```

The comparison requires the canonical Windows Clang/LLVM and Linux GCC/GNU-binutils tool families, two-run same-host reproducibility recomputed from validated manifests, every named check, all three supported roots in both runs, an identical platform-neutral logical artifact cohort, all four canonical size metrics, and the fixed 200 MiB disk geometry. Windows hosted `.exe` suffixes make physical path equality observational. The gate records physical paths, aggregate digests, and size deltas but explicitly does not require cross-toolchain hash equality. Canonical comparison evidence is `baselines/windows-linux.json` with schema `cupid.bootstrap-host-comparison.v1`.

Checked evidence belongs under `docs/bootstrap/baselines/`. Ordinary runs stay under ignored `build/bootstrap/`. Three-root evidence has schema identifier `cupid.bootstrap-baseline.v2`; the prior v1 schema described only the 431-path root manifest and is deliberately rejected by the cross-host gate. Schema changes require tests and a bootstrap-log entry.

| Host oracle | Source revision | Result | Evidence |
| --- | --- | --- | --- |
| Windows AMD64, Clang/LLVM | `0969300` | PASS: two clean 431-artifact builds matched; aggregate SHA-256 `c5fd401aff3dd914d72d3b8b2fff97d6f89df0dcf5fcb8e7de905b745c0b2f55` | `baselines/windows-amd64.json` |
| Linux, GCC/binutils | Pending | A separate capture is required; cross-toolchain equality is not expected | Not captured |

This capture supersedes the CupidLD evidence from `92b3684` and proves the production CupidASM cutover at `0969300`. The checked audit contract proves that the 431-path manifest includes all 424 final-link objects. The current capture passed 145 host tests in 74.943 seconds (144 pass, one Windows-only skip), `/bin/ls.cc` through CupidC in 20.487 seconds, and `as /demos/hello.asm` through CupidASM in 25.598 seconds. Its two isolated builds took 18.364 and 17.169 seconds. The resulting kernel ELF is 6,252,832 bytes, its raw binary is 6,078,269 bytes, `.text` is 1,396,388 bytes, and the disk image is 209,715,200 bytes. Relative to the prior baseline, only the two CupidASM-produced relocatable objects and the two linked ELF containers changed; boot/trampoline bytes, flattened kernel bytes, and the disk image remained identical. All times and sizes are oracle observations, not performance gates.

## Build-output hygiene

`.gitignore` excludes compiler objects, OS binaries/images, generated build intermediates, Python caches, runtime logs, and ordinary baseline output. `.gitattributes` fixes text checkouts to LF so embedded Cupid C, Cupid ASM, documentation, and other text do not change bytes with host checkout policy. Binary fixtures and assets are explicitly marked binary.

`make clean` removes objects, generated C intermediates, runtime logs, and Python caches while preserving `cupidos.img`. `make clean-image` removes that image. `make distclean` performs both and also removes the disposable USB image, root `build/`, and hosted `toolchain/build/` output. Checked fixtures, packet captures, the `TempleOS/` reference tree, `.agents/`, and `skills-lock.json` are not hygiene targets.
