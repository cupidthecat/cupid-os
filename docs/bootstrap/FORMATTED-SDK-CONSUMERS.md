# Formatted SDK consumer checks

All four SDK publications pass with the private qualified tool projection.
All 22 published ELF files match across hosts and profiles. The complete
manifest matches between hosts for each profile.
Installed seeds remain unchanged.

| Check | Native Linux | Windows controller with WSL tool execution |
| --- | ---: | ---: |
| Publication time, seconds | 5,498.510 | 6,269.436 |
| Long-path publication time, seconds | 5,665.619 | 5,952.578 |
| Published ELF files | 22 | 22 |
| Contract inputs | 113 | 113 |
| Bootstrap inputs for this profile | 113 | 113 |
| Complete stage pairs checked during build | 84 | 84 |

Each build keeps two workers, nine forbidden conventional producer variables,
the 7,200-second outer deadline and the existing individual tool limits. Its
checked manifest author and independent Python oracle compare 17 contract
objects, 16 contract executables, 45 bootstrap objects and six tools. The
hosted runtime contract passes before publication.

Separate readback checks the actual 22 published files, executable structure,
manifest and frozen inputs. Each of the six published tools matches the
qualified producer. The private stage directories have already been retired
by normal build cleanup; their pair comparison is established by the completed
build, rather than a later read of those directories.

The whole manifest has 44,525 bytes and SHA-256
`abe65f0a6c1033139a29853a5df1f98f93fc90d728126292799d2de1fcfc5f4e`.
The long-path manifest has 44,714 bytes and SHA-256
`a599f4626ac1c6b38dcba299c7385ee0a0c07d18d19498d53880abbe91fd3834`.
Its only difference from the ordinary manifest is the additional
`toolchain/hosted/i386-windows/utf8_long_path_start.asm` input and the resulting
producer count and digest. That profile has 114 bootstrap inputs. Both retain
the same 113 contract inputs and all 84 complete stage comparisons.

Paired [cold OS/user builds and four private boots](FORMATTED-COLD-CONSUMERS.md)
also pass. Complete repository replays and final seed installation remain open.

The [ordinary SDK archive](evidence/formatted-ordinary-sdk-20261110.json.gz)
contains 57 file records and 34 deduplicated payloads, including both complete
publications and the independent verifier. Every exported payload passes size
and SHA-256 readback. The archive has 5,210,984 bytes and SHA-256
`86a63a9c3ba07a5e76a7274cec24208c66fbdc8348f52fcf05a2f5d6a22ab09e`.

The [SDK/cold-build archive](evidence/formatted-sdk-cold-matrix-20261110.json.gz)
adds both long-path publications, independent matrix readback, build products,
frozen inputs and boot logs. Existing payloads use verified parent archives;
unchanged sources use exact Git or Git LFS references. Whole disk images remain
local hash references. No installed seed changes belong to this evidence.
