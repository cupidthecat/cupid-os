# Reproducible oracle baseline

The baseline runner freezes a committed normal build, including Cupid-owned and remaining host-owned stages, as reproducibility and oracle evidence during ownership transfer. It is not part of the fixed-point toolchain.

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

1. Resolves and hashes Git, Make, Python, the C compiler, and QEMU, then records their version output. The compiler preflight must also produce a real freestanding i386 object with the configured `CC_TARGET`; a version string without 32-bit capability is a failed prerequisite. Linux evidence records `/etc/os-release` distribution identity in addition to the kernel/platform tuple. Retired host assembler, standalone linker, object-copy, and symbol-reader tools are no longer preflight requirements. NASM and GNU/LLVM `nm` availability are recorded separately under `optional_oracle_tools` and never fail preflight. The runner also records the availability, versions, and hashes of optional PATH-dependent JPEG normalizers (`jpegtran`, `djpeg`/`cjpeg`, and FFmpeg) that can change embedded bytes. Conversion can fall through per file, so artifact hashes, rather than this availability inventory, determine what was produced.
2. Creates two independent worktrees for the requested revision.
3. Builds all three supported roots in every worktree: root `all` after `distclean` with fixed disk geometry and `WAD_SRCS=`, user `all` after `user:clean`, and hosted toolchain `all` after `toolchain:clean`.
4. Asks each Make root for its declared final artifacts. The root cohort contains the ordered `KERNEL_OBJS` plus boot, trampoline, pass-1 ELF, generated symbols, final ELF, raw kernel, and freshly formatted disk-image boundaries; the user cohort contains the three CupidLD executables; the hosted cohort contains all 25 declared artifacts in the current graph. Nineteen are native contracts or commands, including five CupidC contracts and the native compiler. The remaining six are the static Cupid-built i386 commands and runtime contract. `make check-bootstrap-audit` independently proves that every linked OS object is present in the root manifest. The checked `1e079d1` evidence predates those twelve additions and therefore contains thirteen hosted artifacts.
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

The comparison requires the canonical Windows Clang and Linux GCC compiler families, two-run same-host reproducibility recomputed from validated manifests, every named check, all three supported roots in both runs, an identical platform-neutral logical artifact cohort, all four canonical size metrics, and the fixed 200 MiB disk geometry. Optional oracle-tool identities do not gate the comparison. Windows hosted `.exe` suffixes make physical path equality observational. The gate records physical paths, aggregate digests, and size deltas but explicitly does not require cross-toolchain hash equality. Canonical comparison evidence is `baselines/windows-linux.json` with schema `cupid.bootstrap-host-comparison.v1`.

Checked evidence belongs under `docs/bootstrap/baselines/`. Ordinary runs stay under ignored `build/bootstrap/`. Three-root evidence has schema identifier `cupid.bootstrap-baseline.v2`; the prior v1 schema described only the 431-path root manifest and is deliberately rejected by the cross-host gate. Schema changes require tests and a bootstrap-log entry.

| Host oracle | Source revision | Result | Evidence |
| --- | --- | --- | --- |
| Windows AMD64, Clang/LLVM | `1e079d1` | PASS: two clean 447-artifact builds matched aggregate SHA-256 `fc3e626f85780e4973b57a010528e4e3e59d72c63c54cc3701e61936555bc960`; all required checks passed | `baselines/windows-amd64.json` |
| Linux x86_64, GCC/binutils | `1e079d1` | PASS: two clean 447-artifact builds matched aggregate SHA-256 `38bd2192e3d973b8c5b03d04ea69ed4397769913931ef0127c8fe8fee0536c0d`; all required checks passed | `baselines/linux-x86_64.json` |

The checked `baselines/windows-linux.json` comparison passes for revision `1e079d1`. Both hosts expose the same 447 logical artifacts: the checked 431-path root cohort covering all 424 final-link objects, three user executables, and thirteen hosted contract/CLI executables. Physical paths differ because Windows uses `.exe`, and the two host compilers produce different aggregate hashes; both observations are intentionally non-gating. The 200 MiB disk geometry and every required cohort, check, manifest-integrity, compiler-capability, and tool-family invariant agree.

The Windows run passed the 162-test host suite in 80.915 seconds, `/bin/ls.cc` through CupidC in 19.723 seconds, and `as /demos/hello.asm` through CupidASM in 24.245 seconds. Root builds took 16.168 and 15.549 seconds, user builds 0.203 and 0.204 seconds, and hosted-tool builds 2.676 and 2.831 seconds. Its kernel ELF is 6,252,832 bytes, raw kernel 6,078,269 bytes, `.text` 1,396,388 bytes, and evidence-file SHA-256 `63a9a7ea060f640f88b3c6add7bba242ee49d16a43702c71c8a658ffdc3c91c3`.

The Ubuntu 24.04 WSL2 run passed all 162 host tests in 100.209 seconds, the CupidC smoke in 22.066 seconds, and the CupidASM smoke in 25.249 seconds. Root builds took 12.210 and 10.619 seconds, user builds 0.070 and 0.071 seconds, and hosted-tool builds 4.036 and 3.362 seconds. Its kernel ELF is 5,947,852 bytes, raw kernel 5,770,439 bytes, `.text` 1,078,952 bytes, and evidence-file SHA-256 `ffa89f0132397dbd68ff838225a2a35cb651f4211dc1ca6b5c5533de96a1f454`. The comparison file SHA-256 is `459da66b60684a44c9e1bccb62c2e7d08f931fa91a7b24f5d87d426a9e78fe1f`. All times and sizes are oracle observations, not performance gates.

## Build-output hygiene

`.gitignore` excludes compiler objects, OS binaries/images, generated build intermediates, Python caches, runtime logs, and ordinary baseline output. `.gitattributes` fixes text checkouts to LF so embedded Cupid C, Cupid ASM, documentation, and other text do not change bytes with host checkout policy. Binary fixtures and assets are explicitly marked binary.

`make clean` removes objects, generated C intermediates, runtime logs, and Python caches while preserving `cupidos.img`. `make clean-image` removes that image. `make distclean` performs both and also removes the disposable USB image, root `build/`, and hosted `toolchain/build/` output. Checked fixtures, packet captures, the `TempleOS/` reference tree, `.agents/`, and `skills-lock.json` are not hygiene targets.
