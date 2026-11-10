# Cold builds with the qualified tools

The private qualified seed projection builds the complete normal image and
three user programs from empty output directories on Linux and Windows.
Installed seeds remain unchanged. Each build uses two workers, nine forbidden
conventional producer variables and the original 7,200-second command bound.

| Check | Native Linux | Windows controller with WSL tools |
| --- | ---: | ---: |
| Normal image build, seconds | 2,264.471 | 3,263.970 |
| Separate user build, seconds | 22.027 | 29.838 |
| Normal objects | 429 | 429 |
| User objects / executables | 3 / 3 | 3 / 3 |
| Private `ls` boot, seconds | 58.727 | 51.151 |
| Private ISO fixture boot, seconds | 70.417 | 62.630 |

Independent readback validates all 432 ELF objects, the three user executables,
syscall ABI, bootloader, both kernel ELFs, raw kernel and disk image. All 440
products and 331 headers match across hosts. The normal products also match
the preceding formatted image. Its user outputs were absent, so their
agreement is established between the two fresh builds.

The 200 MiB disk image has SHA-256
`c4129c8777b8056d4b30a4e4ce5567112d5c7c1bc4b6a1efac29265fb5d00bbf`.
The exact 16-artifact size gate passes. The verifier checks the current
`[0x01C00000, 0x01E00000)` executable arena and the five-sector bootloader
before comparing the embedded kernel. Earlier observers assumed an obsolete
arena and a one-sector layout; their failed checks remain retained. No build
output changed to satisfy those corrections.

Each private boot retains four CPUs, the `max` CPU model, e1000, the strict SMP
validator and required terminal completion. All four logs have 62 TLS successes,
JIT completion followed by terminal completion, and no compiler error. The ISO
fixture's pass marker precedes its JIT completion. Inner and outer bounds stay
600 and 660 seconds. Source and original image bytes remain unchanged.

The [evidence archive](evidence/formatted-sdk-cold-matrix-20261110.json.gz)
contains the complete object and kernel payloads, both long-path SDK
publications, controllers, logs and independent checks. Unchanged source bytes
use verified Git/Git LFS references; repeated SDK bytes use preceding archives.
Whole disk images remain retained locally with measured hashes.

The archive has 5,399 file records, 469 local payloads, 4,392 Git/Git LFS
references and 99 parent references. All pass readback. Its 18,510,233 bytes
have SHA-256
`6601e7a642a3433e6da2d41e724cf1b5819fc8907019b6b6d25396496f807d33`.

These boots exercise `ls` and the ISO fixture. Complete repository acceptance,
installed seed adoption, external user-program guest coverage, Doom gameplay
and audio, and rebuilding inside Cupid OS remain separate work.
