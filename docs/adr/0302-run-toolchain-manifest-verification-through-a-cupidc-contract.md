# ADR 0302: Run Toolchain manifest verification through a CupidC contract

## Status

Accepted on 2026-08-20.

## Context

The Toolchain publisher builds 21 stage-four artifacts and records their
identity in `toolchain/build/cupidc-contracts/manifest.json`. Its `all` target
used Host Python alone to verify that publication. The verifier checked the
artifact set, the 68-file publication inventory, and the 50-file bootstrap
source closure before returning success.

That decision belongs in the checked toolchain. Native filesystem inspection
does not. The verifier must reject symbolic links and Windows reparse points,
hold directory identities stable, detect restored in-place edits, and notice
membership changes in wildcard-discovered source directories. The hosted
Cupid runtime does not yet provide that boundary.

The publication has two separate seed roles. Its manifest records the static
i386 Linux seed that produced the published contract cohort. The verifier
itself must run on the current host, so Windows uses the checked native PE seed
and Linux uses the checked static ELF seed.

## Decision

Keep path capture, private staging, process launch, and an independent oracle
in Python. Move the publication decision into
`toolchain_manifest_contract.cc`, a strict C11 program built and run by the
host-selected checked Cupid seed.

The runner creates a length-prefixed `CUPMAN2` request from one pinned
repository handle. Directory membership, file bytes, the checked build, and
the final checks all use that handle. The request contains the raw publication
manifest, observations for all 21
published artifacts, the exact 68 publication inputs, the exact 50 bootstrap
sources, the raw Linux publication-seed manifest, and observations for its
five tool images. Every observation carries a safe logical path, file kind,
size where the published schema records it, and SHA-256 digest. The contract
rereads the request before success.

The contract fixes the publication schema, artifact names, comparison names,
68 input paths, 50 bootstrap paths, Linux seed path, build-plan digest, and
Linux seed-manifest digest. It compares the pinned observations with the
manifest claims and rejects missing, extra, duplicate, unsafe, malformed,
truncated, or trailing data. JSON object order does not affect the result. A
successful check emits one canonical JSON report.

The publication's `object_comparisons` map does not publish the compared
objects. The contract therefore checks its exact key set and lowercase digest
syntax, but it does not claim an independent byte-level proof of those values.
Stage convergence remains the publisher's evidence.

Python builds a private copy of the captured publication and runs the existing
verifier as an independent oracle against those exact bytes. It also verifies
the live input closure before and after the Cupid contract. POSIX directory
membership comes from the open directory descriptor. Windows holds
parent-relative directory handles that deny replacement while it enumerates by
path. The checked build reuses the same pinned repository reader. The final
boundary recaptures file bytes, directory memberships, seed membership, and
the repository root identity. An added source, an in-place edit with restored
metadata, an intermediate link, a persistent root replacement, or an artifact
swap blocks success. A transient POSIX rename and restore cannot redirect any
read because the complete operation stays on the original pinned handles.

The checked build freezes 20 source and support inputs. Checked CupidC compiles
the contract and runtime, checked CupidASM assembles the host startup, and
checked CupidLD links either a static ELF or native PE. The original execution
seed is kept under pinned no-follow handles and rechecked byte for byte after
the command. The private seed copy must contain exactly its five declared tool
images.

Make declares the 68 publication inputs, 50 bootstrap inputs, six Linux
publication-seed files, 20 verifier build inputs, 21 artifacts, and the
host-selected six-file execution seed. The checked audit deduplicates that
closure to 126 direct inputs on Windows. Only the verifier build inputs carry
compiler or assembler ownership. Source files that the request merely hashes
do not inherit a tool owner.

No checked seed changes in this decision. The contract is a new consumer of
the existing CupidC, CupidASM, and CupidLD capabilities.

## Evidence

The direct contract suite has 16 tests and passes in 1.743 seconds. It covers
the canonical report, exact 21/68/50 membership, reordered JSON keys,
malformed framing, duplicate keys, unsafe paths, count drift, and independent
observation changes in the artifact, source, bootstrap, and seed lanes.

The runner suite has 24 tests. On Windows, 21 pass in 23.227 seconds and three
POSIX rename and hard-link cases are skipped. All 24 pass through WSL in
24.068 seconds. The
suite builds and runs the real PE or ELF contract with the checked host seed.
It also covers manifest ABA, a second oracle failure, same-metadata byte drift,
source-membership additions, linked parents, rogue seed images, unsafe CLI
paths, malformed seed plans, Make-style path resolution, a persistent root
replacement, a swap-away and restore attempt against the pinned reader, a
wildcard-directory replacement made from hard links plus a new header, and
controlled diagnostics.

The focused host-branch Make test passes for both Windows and Linux. The
generated audit records 739 active language inputs, 452 transforms, and 25
accounted unreachable files. Tool participation is CupidC 249, CupidObj 192,
CupidASM 8, CupidLD 8, CupidDis 6, and Cupid-built semantic contracts 3. Host
Python still participates in all 452 transforms.

The first complete publication attempt stopped after 1,720.471 seconds when
stage-three CupidC could not find `x86_catalogue_contract.inc`. The private
snapshot already contained that file and `x86_inline_cases.inc`, but the x86
contract plan did not declare their sibling include directory. The plan now
adds `/toolchain/tests` only for `x86_contract.cc`. Three focused publisher
tests pass in 0.457 seconds, all 53 publisher tests pass in 7.161 seconds, and
the checked Linux CupidC seed compiles the corrected x86 contract in 93.5
seconds.

The second complete publication attempt stopped after 1,728.003 seconds at
the catalogue's nested `#include "../x86.cc"`. The quoted include root was
correct, but the private publication snapshot did not contain that source.
The publisher, Make prerequisites, checked contract, and audit now include
`toolchain/x86.cc`, which raises the exact publication inventory to 68. The
file was already one of the 50 bootstrap inputs, so the deduplicated verifier
transform remains at 126 inputs. A recursive check of all 17 translation
roots found 157 include edges and no other dependency outside the snapshot.
The failed run published nothing.

A third publication launch reached stage four but outlived its 3,604-second
wrapper. Its detached child inherited a closed output pipe, so that run is not
publication evidence. A clean run with a three-hour allowance passed in
3,933.424 seconds. All 21 stage-four artifacts matched stage three, the hosted
runtime contract passed, the live input checks passed, and the native Windows
manifest contract returned success. The 22,931-byte publication manifest has
SHA-256
`8909105d516ef53d3c5081e5752fbef8596458fdfa673ec08275e7e435cd059a`.

The first final-source poisoned-host build reached only the artifact-size gate
in 633.542 seconds. It measured `kernel.bin` at 9,203,248 bytes instead of the
prior 9,202,060-byte policy value. The policy changed only that row. All 38
artifact-size and semantic-contract tests then passed in 2.784 seconds, with
two Windows replacement cases skipped because pinned handles deny those
operations. The repeated poisoned build passed in 651.193 seconds. It produced
these identities:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 9,299,616 | `7fe8556bae4262a1c16a206182b704310791874cb9d8ac61be6b9c5f671d2b90` |
| `kernel/kernel.elf` | 9,422,496 | `8ceee43d12586d9fff73f1752940d295a2fc20e9e6364e37d7078c6ca2418027` |
| `kernel/kernel.bin` | 9,203,248 | `403034fe4d727bba0fc4ee15545b5bc6f47840c541e95761f9cdc841ce19372f` |
| `cupidos.img` | 209,715,200 | `3a2e5acc63b50d27aca68e4e7e8872adbfcab96674040a08a22c2c6aa614bebc` |

A private four-vCPU e1000 boot compiled `/bin/feature14_simd.cc` with in-OS
CupidC and passed in 61.926 seconds. It printed the direct-call marker, the
callback marker, overall PASS, and clean JIT completion. The 33,483-byte log
has SHA-256
`1bfea969c354abd447aada31982011082538fe1de6a9ea1dff61927bd76c73bb`.
The private run left the source image unchanged.

Final audit generation passed in 63.0 seconds and check mode passed in 62.6
seconds. The graph has 739 active inputs, 452 transforms, 255 feature
requirements, and 25 accounted unreachable files. Its active-source digest is
`20eb8f85d95d7a6acb071a81e1884dd0fb8a45dd52157763324f147c54ad6f52`.
The 2,700,777-byte JSON has SHA-256
`924000ec9449d4874142c4240094aa4865c015f9af9dfc3f23c0b4b2677e0ae4`,
and the 12,502-byte Markdown summary has SHA-256
`56a05868915f15f3db58cd1d5d0a26cc60ebee1b3d625d1356e0dd0aa8059a41`.

## Rejected alternatives

Leaving the decision entirely in Python was rejected because the checked
CupidC seed can parse and decide the complete published schema.

Reading the live repository directly from the contract was rejected. It would
replace a mature cross-platform no-follow boundary with a weaker hosted
runtime API.

Using the Linux seed through WSL on Windows was rejected. The native checked
PE seed can compile, link, and run this contract directly. The Linux seed is
publication provenance, not the Windows execution engine.

Treating every observed source as compiled by this transform was rejected.
Hashing a startup file does not prove that CupidASM can assemble it.

## Consequences

`toolchain:all` now has CupidC, CupidASM, CupidLD, a Cupid-built semantic
contract, and Host Python as participants. The separate manifest publication
transform remains Python-only. Across the supported graph, this leaves one
Python-only Toolchain transform instead of two.

Python still owns pinned filesystem access, process launch, the independent
oracle, drift detection, and private staging. The Toolchain build is not yet
Python-free, and Windows still needs WSL for the complete published Linux
contract cohort.

The new production source is already named `.cc`. The residual `.c` census is
unchanged. `TempleOS/` remains read-only reference material and is excluded
from every progress count.
