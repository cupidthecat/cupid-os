# ADR 0365: Provide hosted stddef for bootstrap

## Status

Accepted on 2026-08-29.

## Context

ADR 0364 added a typed JPEG transaction to source-head CupidBuild. Its public
header now uses `size_t` in the JPEG validation interfaces and includes
`<stddef.h>` when the platform has not already supplied that type. The paired
seed refresh was the first complete reconstruction to compile this public
interface through the hosted Linux include profile.

Both candidate proofs failed closed before they could publish a report or
candidate cohort. Linux stopped after about 412.25 seconds, and native Windows
stopped after about 449.43 seconds. Each reported the same source error at
`/toolchain/cupidbuild.h:5:1`:

```text
error CT9000009: CupidC include file was not found
```

The missing file was `<stddef.h>`. The hosted include root already provided
`size_t` and `NULL` through `cupid_host_abi.h`, but it did not provide the
standard header that public source is entitled to include. Changing the API
back to an ad hoc integer type or relying on include order would hide that
library gap and make the public interface less faithful to C.

## Decision

Add `toolchain/hosted/i386-linux/include/stddef.h` to the hosted bootstrap
library. It includes `cupid_host_abi.h`, keeps the target's unsigned 32-bit
`size_t` and null pointer definition, and declares the represented i386
`ptrdiff_t`, `wchar_t`, and `max_align_t` types. Its `offsetof(type, member)`
macro uses CupidC's typed `__builtin_offsetof` operation.

Treat `__builtin_offsetof` as compiler implementation machinery in strict C,
not as a GNU-mode extension. The existing semantic path still requires a
complete record or union, resolves the member against the target layout, and
rejects invalid, missing, and bit-field members with their specific
diagnostics. GNU-only alignment spellings remain controlled by GNU mode.

Add the new header to every frozen bootstrap, manifest, artifact, and audit
source closure. Source head now has 59 inputs with snapshot SHA-256
`b69906a897a10f0a0b2464024ee4255aa2d10f2fe75014c3ab34b7be983e387b`.
The Linux plan remains
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`,
and the native Windows plan remains
`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`.

This is a source repair. The active manifests and checked seed images stay on
their preceding 58-input snapshot until a fresh paired fixed point converges
from a named commit. The normal JPEG recipe remains on Hostbuild for the same
reason.

## Evidence

A focused checked-seed regression first failed with the same missing-include
diagnostic while compiling a source that includes `cupidbuild.h` and uses the
public `size_t` API. It passed after the hosted header was added. The same test
also compiles `ptrdiff_t`, `wchar_t`, `max_align_t`, and `offsetof` through the
old checked CupidC seed and validates the resulting i386 relocatable object.

A strict frontend regression first failed because `__builtin_offsetof` still
required GNU extensions. It now passes in strict mode and folds the target
member offset. Its companion negative names a missing member and receives the
existing exact diagnostic.

The frozen-source contracts now require the 59th header and the source
snapshot above. Artifact-size and manifest contract coverage was updated with
the same closure. The first audit replay exposed two incomplete Makefile
closures: the root artifact-size contract and the user syscall ABI contract
did not yet name the new header. Adding it to those declarations brought the
Makefiles, native contracts, Python oracles, and generated audit back into
agreement.

The complete frontend and Toolchain groups passed 162 tests in 19.761 seconds.
The manifest and artifact-policy groups passed 119 tests in 94.918 seconds,
with seven expected platform skips. The regenerated active-source audit is
2,777,757 bytes with SHA-256
`2e332146990c657026c200fc67e668e7d3f0d15cb9d0193943572db9abe01e3f`;
its independent checked replay passed. The two final audit selectors that had
found stale conditional and ABI counts passed in 68.727 seconds after their
expectations were corrected.

The complete build-graph module passed all 112 tests in 1,060.065 seconds.
The final uncontended `make -j4 all` replay passed in 1,117.168 seconds. It
compiled all 83 Doom roots, completed both CupidLD links, passed whole-image
CupidDis validation, accepted all sixteen exact-size artifacts, preserved the
existing FAT contents, and staged `hello.iso`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,605,176 | `8d0b8c56b24ed44d118d7a546bdcb4ee59681a2c5ded3464831c87ef10558b2a` |
| `kernel/kernel.elf` | 9,736,248 | `202a9280f5ea24c86843a1d9729db6e036e9b5438488573f1c3a7e7b8522f8ea` |
| `kernel/kernel.bin` | 9,507,352 | `6f396b741e9a119675d0c62f717f70b5539bbcdc4f21f781252af8cc4c0e6951` |
| `cupidos.img` | 209,715,200 | `b7d131263d7243e20cabd00760626ffa75ec8a1cfef88ceb81139ab135f787b1` |

The 3,382-byte artifact-size policy covers 38,189,064 bytes and has SHA-256
`ba505b1fc1440de997eb2fd75f4da0ef9ae1a6df9403a3f1c67ba8dcb6ec1b37`.

A complete Linux and native Windows candidate rerun is pending; no candidate
report, manifest, or seed image from the failed attempt is promotion evidence.

## Alternatives considered

Changing the public JPEG validator sizes from `size_t` to `unsigned int` was
rejected. It would make one header compile while leaving the hosted standard
library incomplete.

Including `cupid_host_abi.h` before every public header was also rejected.
Public headers must compile from their own declared includes, and a hidden
include-order requirement would recur as more standard interfaces move into
Cupid tooling.

Keeping `__builtin_offsetof` behind GNU mode was rejected because the standard
`offsetof` macro needs target-aware implementation support in strict C. The
macro does not expose a GNU-only language requirement to its caller.

## Consequences

Hosted bootstrap source can include `<stddef.h>` without importing a host libc
or changing the i386 ABI. Public CupidBuild declarations remain typed with
`size_t`, and strict CupidC can provide the standard `offsetof` macro through
its checked target-layout path.

The new source is a header, so no `.c` suffix is introduced. `TempleOS/`
remains untouched reference material. Paired seed promotion and the JPEG Make
handoff remain separate changes with their own convergence and production
evidence.
