# ADR 0363: Run kernel links with CupidBuild

## Status

Accepted on 2026-08-29.

## Context

The promoted `cupidbuild run` command admits CupidLD and already has fixed-point
coverage for a real fixed-address ELF link. The normal two-pass kernel build
still used the Python checked-seed runner to launch CupidLD. Python added no
kernel-specific validation or publication work on either link edge. It only
provided the checked native-tool invocation contract that CupidBuild now
implements.

Kernel flattening and kernel-symbol generation are separate composite paths.
They combine several tools with inspection, parity, drift, locking, or atomic
publication rules. Moving the two direct links does not transfer those
contracts.

## Decision

The root Makefile defines `CUPIDLD` as an immutable command that selects the
promoted platform CupidBuild image, its production manifest, the repository
root, and the `cupidld` role. `CUPIDLD_INPUTS` is also immutable and contains
the Makefile plus all six production seed images. Command-line Make overrides
cannot replace either binding.

Both `kernel/kernel.elf.pass1` and `kernel/kernel.elf` now run through this
command. Their CupidLD arguments, object order, linker script, and output paths
are unchanged. The active-build audit derives the runner owner from the
evaluated `CUPIDLD` binding, so Python-backed fixtures, direct CupidLD commands,
and native CupidBuild commands remain distinct.

## Evidence

A source contract failed while `CUPIDLD` still expanded to
`tools/bootstrap_toolchain.py` and passed after the handoff. The contract also
injects poisoned `CUPIDLD` and `CUPIDLD_INPUTS` command-line values. The
evaluated graph retains the promoted command, Makefile prerequisite, and
complete six-image seed closure.

The regenerated audit contains 452 transforms, including 443 under root
`all`. CupidBuild participation rises from 190 to 192, while Python falls from
262 to 260. The pass-one and final kernel link edges each name CupidLD and
CupidBuild, and no transform is Python-only.

A production-sized private run linked the 428 audited pass-one object inputs
with the promoted native Windows CupidBuild and CupidLD images. It completed in
5.5 seconds and produced a 9,596,984-byte ELF with SHA-256
`43e5b716ce0eca66ebdae61c19fc4ca0e7451e388987379c55563de499e0f357`.
The result matched the existing `kernel/kernel.elf.pass1` byte for byte.

The first proof attempt obtained the object list through a Make `echo` command.
That shell path truncated one token to `ker`, and CupidLD rejected the missing
file. The corrected proof read the exact 428-input inventory from the generated
build audit. This was a test-harness error, not a linker or runner failure.

The first complete `make -j4 all` attempt reached the native CupidBuild wrapper
cohort but one parallel CupidObj process reported `no_memory`. The same wrapper
passed during a `make -j2 all` retry. That retry compiled every active source,
including all 83 Doom roots, ran both kernel links through CupidBuild, passed
the 431-input strict CupidDis gate, and then stopped at the expected stale
flat-kernel size row. The edited embedded manuals made `kernel/kernel.bin` 432
bytes larger. After the measured row was updated, `make -o FORCE -j2 all`
accepted all 16 exact artifacts and published the disk image.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,596,984 | `1b8fd538abd6577e817908fd6b308c3398381d1dbd446d105f9b799a3341378e` |
| `kernel/kernel.elf` | 9,728,056 | `73b120e46d19e3cba2117083a9b590c15cfdde2d08078b6e4b2d51566f96f379` |
| `kernel/kernel.bin` | 9,502,016 | `e5761dcd0d2851fc01f52f64669ea4b47fca9d1242bcd5b5c97af836ed8de7c3` |
| `cupidos.img` | 209,715,200 | `5aeed8c2d792a2bc21efe6bc9c1a05e6fa2f1512385b900ab8f0ca5f02e7240d` |

The 3,382-byte exact-size policy covers 38,167,344 bytes and has SHA-256
`ba48bf4609616faad06da2d2f6910d8091b4084951778d68fb7331d9d447a4ff`.
The regenerated 2,769,536-byte audit has SHA-256
`66645a6009c63dc13591202e38f001d89b6cc89b1d6a491dbeb7c6f786c3f60c`,
and its independent checked replay passed.

All 112 audit selectors passed in fresh Python 3.12 processes with no failure
or allocation retry. The three ownership-focused selectors passed together in
49.231 seconds. The three artifact-policy modules passed all 54 tests in 3.032
seconds, with four expected platform skips.

A private four-vCPU E1000 QEMU smoke ran the final image with `--cpu max` and
`--verify-smp-runtime`. It brought the required runtime boundary online and ran
`/bin/ls.cc` to normal JIT completion. The 33,786-byte serial log has SHA-256
`c92606af6f3e30d5e0a7f674078dd380992474fa11bd94e729b2f670c36f08ce`.

## Consequences

The normal kernel links no longer need Python to launch CupidLD. The generic
runner still does not claim output locking, linked-image inspection, or atomic
publication. Those rules remain in the later kernel-symbol and flat-kernel
transactions.

Six composite CupidObj paths and broader bootstrap coordination remain on
Python. All active Toolchain and OS C sources already use `.cc`, so this
handoff requires no source rename. `TempleOS/` remains read-only reference
material.
