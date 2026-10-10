# ADR 0361: Promote the checked-tool runner

## Status

Accepted on 2026-08-28.

## Context

ADR 0360 extended `cupidbuild run` to checked CupidObj and CupidLD calls and
made the fixed-point behavior gates exercise a successful CupidObj
`wrap-text` request and a useful invalid-option failure. The active seeds
predated that work. Make could not move a direct CupidObj recipe to the native
runner while the checked CupidBuild image lacked the command it would execute.

The Linux runner also needed new static runtime and startup support. Every
Linux tool links that shared closure, so all six Linux images changed even
though CupidBuild is the only tool with a new command. The Windows service
layer already had the required process boundary; five PE images remained
byte-identical and only CupidBuild changed.

## Decision

Promote the Linux and native Windows six-tool cohorts together from revision
`a4eee4c2c4b8f1cbb7ca22fbe7688f5958912e4f`. Both bind the 58-input source
snapshot
`a2e8e5c97672c2d0bd8ba4f4166860cc9686a1838cefeab8bc46d5b1c9fbe09d`.
The Linux build plan remains
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`,
and the native Windows plan remains
`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`.

The 6,602-byte Linux manifest has SHA-256
`f1bee18b9b1506ff5a665e76d57d028702ae7c701c4e9d432ed4b87c68ee258b`.
The 2,852-byte Windows manifest has SHA-256
`917817122a36331a0ec77ba06d6ce40a8eacacc4224d8ed468d8d77272b8b974`
and names the exact Linux manifest bytes in its pairing field. The v2 parent
fields continue to identify the original five-tool transition manifests;
they are lineage records, not a claim that those parents directly produced
this later refresh.

The promoted images are:

| Platform | Tool | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Linux | CupidASM | 496,664 | `1517bff9353ae7663825dbcee20084a50e296061b3085bab2c0719eea714c770` |
| Linux | CupidBuild | 298,540 | `0ef036d352f360610d73303b79686f00baf3f509b250d4234455347582a9f9b5` |
| Linux | CupidC | 2,691,756 | `de94a135ed2b55ee0c38cc07c5e5e2aa57af9bddda1e690c909c591cfb328759` |
| Linux | CupidDis | 538,556 | `4a1326e12291c83e2193cf27630b9271d1c299faf39db9ad7fa74d11cd52fc47` |
| Linux | CupidLD | 312,928 | `0dd697544f4806cf1d769cf59a8a7c37d7355f8360f3513458bfff2261c8a5cb` |
| Linux | CupidObj | 392,820 | `e9958b28c3230fe83c4bf409797208d735887c54d4ebffd0565b4a91f45142fb` |
| Windows | CupidASM | 479,744 | `9c50e204262a0b05b12d4fc0924670c66092d053ad12b99134ab79a254ef07ae` |
| Windows | CupidBuild | 316,928 | `6e54143304713399dc82f6db84369bae8c60de53773a99958a760321a3e4b5f1` |
| Windows | CupidC | 2,620,416 | `73252f25a44ff0308f0a9403e942af0e582e9cac222e5738412af9c313f6d19c` |
| Windows | CupidDis | 516,608 | `588485d496209eecf437e6f6fc9d02474d5c4ac1f236af86bdaad9f3f2d705ce` |
| Windows | CupidLD | 296,960 | `aaa7b51a290646ef1d972f4904b1ed176a4dc912e53c1bc4cbdd8d1e39d8495f` |
| Windows | CupidObj | 375,808 | `b6f6a5b66f8e2bcb4b779a16428d7b77a956113c5ca301344537b35839611572` |

This change updates the seed manifests, the host verifier's exact identities,
the native Toolchain manifest contract, and the sixteen-artifact size policy
as one trust-unit change.

## Evidence

The source-head Linux candidate compared 22 C objects, the startup object,
and all six tools between stages three and four. Its behavior gate passed 25
failure, six help, and 32 success cases. The 51,396-byte report has SHA-256
`3efd5d9c436a94726b6cc57f07567737c4e7f4d08a9e1521a24dc16caa85dc25`.
All six initial Linux images differed from the older checked seed, as expected
from the shared runtime and startup change.

The native Windows candidate compared 23 C objects, three assembly objects,
and all six PE images. Its behavior gate passed 14 failure, six help, and 19
success cases. The 64,500-byte report has SHA-256
`154d5ce7922aa43cd2920eba01448c3dbe483ae47d5ad9d298c238e2eb740a12`.
Five initial images matched the older seed; CupidBuild was the only mismatch.

Both promoted manifests then rebuilt the complete cohort from their new
checked inputs. The Linux reproof matched all six initial images, retained the
25/6/32 behavior inventory, and produced a 51,390-byte report with SHA-256
`0e8f340a29000582b9ef4fef66f1ca0ea04066a9ffc3c854952b9cb94cfb4df1`.
The Windows reproof also matched all six initial images, retained 14/6/19,
and produced a 64,499-byte report with SHA-256
`d0e9b5f34949c5f7d968fb01794b12d3ffebf93549913ef303b4d71318305d13`.

The manifest and artifact-size contract suites passed 64 tests. A separate
unittest selector used an obsolete class name and failed before discovery;
the real suites in that command all passed. The paired fixed-point commands
above are the authoritative self-consumption checks.

The corrected promoted-seed selector passed five focused tests. The manifest
and artifact contract group then passed 80 tests with four expected Windows
locking skips. Both checked manifests verified all six images, and active-audit
generation plus checked comparison passed.

The first complete OS replay compiled all 83 Doom roots, linked both kernel
stages, and passed strict CupidDis inspection. Its exact-size gate rejected the
9,500,380-byte flat-kernel row because the settled CTXT text produced
9,500,492 bytes. After that single measured row changed, the complete replay
passed all sixteen exact paths and published the disk image. The final evidence
is:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,596,984 | `cf2f6d08ceddbc86900ebfa3ff8d8ddf038819812f36696142f4e35720dd26ef` |
| `kernel/kernel.elf` | 9,728,056 | `6ad75c6cb5235403edfcf10d114aca2926c7b9228029ce29eac075849a843197` |
| `kernel/kernel.bin` | 9,500,492 | `7e58359449bed98b02a514e0ee5f578470a3647b190c21d4b9110b965cb41bff` |
| `cupidos.img` | 209,715,200 | `cad08d77d4fe9bfc6150d7f559211ce2c7ac45239575635ea87df4438995efd5` |

The 3,382-byte sixteen-row policy has SHA-256
`1e97d2816719e544ec5fc9960474c61f3e0dc5b777e7e553bd69e4eef139aa26`
and covers 38,165,820 bytes. A private four-vCPU `max`/E1000 boot passed the
strong SMP runtime contract and ran `/bin/ls.cc` through in-OS CupidC. Its
34,596-byte serial log has SHA-256
`f20d3ddac10cf56af9f37842da05ded545ed275dc26a4d0aa1592f2bbba2b2bb`.

## Consequences

Both active CupidBuild images now carry the checked CupidObj and CupidLD
runner. Seed carriage makes a direct Make handoff possible, but it does not
perform that handoff. The four guarded assembly publications remain direct
CupidBuild transactions; the 186 direct root CupidObj recipes still use the
Python checked-seed runner at this boundary. The graph therefore remains at
four CupidBuild and 448 Python participations until a separate ownership
change is proved.

No active source changes suffix in this promotion. The owned Toolchain and OS
C sources are already `.cc`. `TempleOS/` remains read-only reference material.
