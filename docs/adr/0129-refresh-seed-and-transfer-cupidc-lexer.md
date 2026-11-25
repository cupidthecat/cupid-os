# ADR 0129: Refresh the seed and transfer the CupidC lexer

## Status

Accepted on 2026-07-26.

## Context

ADR 0125 added decimal `float` and `double` constants, represented integer
and floating conversions, and mixed scalar arithmetic. That work let compiler
head compile the unchanged in-kernel lexer, but the checked seed did not carry
the new code. The normal `kernel/lang/cupidc_lex.o` recipe therefore still
used the host C compiler.

The checked seed is a trust boundary, not a convenient binary cache. A new
compiler image must retain exact source provenance and complete the staged
bootstrap before it can own another production object.

## Decision

Promote the stage-three tools built from commit
`fe3bdfe451d7e019a052c7c8ba53f1f9f3f1fb3d` into
`bootstrap/seeds/i386-linux/`. CupidASM, CupidDis, CupidLD, and CupidObj are
unchanged. The promoted CupidC image is 2,080,288 bytes with SHA-256
`e4eb5b0846a580bb5a2826c97ce646eedec1a077581cb6e87dada6845806761b`.

The manifest and the bootstrap verifier name that commit as the seed source
revision. The existing 19-source `.cc` build plan remains unchanged, with
SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
All 40 files in the seed input closure are byte-identical between candidate
commit `b9332075e94916dbfcec561a2c1e42c1fafc2389` and this reachable
integration revision. Their path-to-blob map has SHA-256
`77cfea29f502c24605f10c7220af810ec10d49610036f17cac5757af25f42690`.

After proving the promoted seed, rename `kernel/lang/cupidc_lex.c` to
`kernel/lang/cupidc_lex.cc` and compile it through the checked kernel wrapper.
The recipe includes the common seed controls and exact recursive header
closure. The wrapper and strict frontier allowlists include the new path, so
an unapproved source or host-compiler fallback still fails.

## Evidence

`make verify-bootstrap-seed` accepts the five promoted binaries and their
manifest.

`make bootstrap-from-seed
BOOTSTRAP_SEED_OUTPUT=build/bootstrap/reachable-strict-frontier` completes
with host code-generator commands poisoned. Every checked seed image matches
stage two. All 19 C objects, startup, and five images match between stage two
and stage three. Both stages pass five help cases, ten successful operations,
and six useful failures. The 40-input source snapshot is
`c3aaf91d6133d0382e5ddb7b33cca665a7344fb7f38688c467db2d28a1a82aa4`.
The 14,878-byte report has SHA-256
`ff1f5b1df59d2945542d47a578e8df27a0f7af816d4dd1dcc2a20b959adf88cc`.

Two independent wrapper compiles of `kernel/lang/cupidc_lex.cc` produce the
same validated 32,408-byte i386 ELF32 object with SHA-256
`b43874f8602b2ee4ffde6587fd1ff5cb586ee7804a401a5a44de786dbd95fec1`.
A clean `make -j2 all` then completes the normal image. Its 7,880,008-byte
kernel payload has SHA-256
`55f40a4fdd4a65fbf0705a5a9e5fa06ff2ca761449c656bf78749bb485d9dd2a`,
and the 209,715,200-byte image has SHA-256
`b35eb263023a50627d8fe96250dd7bc3ba7f9cf62ada3489733fb7083568dbec`.
The canonical active-source audit passes after the recipe change.

## Consequences

Checked-seed CupidC now owns 145 checked-in normal-build roots and the
generated kernel symbol translation. All 146 normal CupidC sources use
`.cc`. Across the three supported build roots, CupidC owns 152 transforms,
the host C compiler owns 145, and host Python owns 165. Python's extra
transform verifies the external-program syscall ABI. The host compiler now
produces 93 normal root objects, with nine strict checked-in roots left.

Python still verifies and launches the seed. Windows still crosses WSL to run
the static i386 tools. Native contracts, development commands, Doom, vendored
code, and the remaining normal roots continue to use the host compiler.

The clean normal image now covers the 145-root checked-in cohort. The latest
four-vCPU runtime evidence describes the earlier 144-root cohort, so the lexer
transfer still awaits its runtime gate.
