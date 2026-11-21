# ADR 0362: Run direct CupidObj recipes with CupidBuild

## Status

Accepted on 2026-08-29.

## Context

ADR 0361 promoted `cupidbuild run` in both active six-tool seeds. The normal
graph still sent 186 ordinary CupidObj calls through the Python checked-seed
runner. Make was not using the native runner for 175 text wrappers, eight
binary wrappers, and three installation-source generators.

The other six CupidObj transforms are different. JPEG wrapping adds an
independent image validator. Kernel flattening, kernel-symbol generation,
disk-image publication, ISO publication, and Doom profile generation each add
format, parity, or publication rules around CupidObj. A generic process runner
does not replace those contracts.

## Decision

Make defines `CUPIDOBJ` as an immutable command that selects the promoted
platform CupidBuild image, its paired production manifest, the current
repository root, and the `cupidobj` tool role. The command cannot be replaced
by a Make command-line override. Its prerequisite closure contains the
Makefile and all six production seed images.

The handoff covers exactly 186 direct recipes: 175 `wrap-text` transforms,
eight `wrap` transforms, and three `install-source` transforms. The
active-build audit assigns both CupidBuild and CupidObj to those edges and
removes Python from them. The six composite CupidObj transforms retain their
existing Python safety, parity, and publication layers.

## Evidence

A source-level Make contract was added before the handoff. It failed while
`CUPIDOBJ` still expanded to the Python helper and passed after the promoted
CupidBuild command became immutable. The complete production contract module
then passed all 61 tests in 55.214 seconds. The three artifact-policy modules
passed all 54 tests in 3.977 seconds, with four expected Windows skips.

The first audit change attached CupidBuild ownership to the literal
`$(CUPIDOBJ)` recipe marker. The two-axis review caught that a graph could bind
the same variable to Python and still receive the new ownership label. The
final audit resolves the evaluated binding for each graph. Native CupidBuild
and Python-backed fixtures now receive different owners, while a direct
CupidObj command receives no invented coordinator. Nine focused positive and
negative cases passed after the correction. A final fresh-process sweep then
passed all 111 audit selectors without using its allocation retry.

Before any handoff rebuild, the 186 existing outputs were frozen as a sorted
inventory of path, byte length, and SHA-256. They contained 4,021,395 bytes,
and the inventory digest was
`4a865114885b6d7aac7ef2813366d0c585f83ee865dd010f9a86dd3631c97300`.
The implementation then updated five embedded CTXT manuals, so the final
cohort intentionally changed. It contains 4,022,487 bytes and has inventory
digest
`95a983d4869a08570ba99218f2f1f221dd64dc90169898419925f8142f7798e8`.
Direct comparisons isolated the runner change from those manual edits. The old
Python runner and CupidBuild produced identical objects for one command from
each migrated family:

| Command family | Bytes | SHA-256 |
| --- | ---: | --- |
| `wrap-text` (`bin/ls.cc`) | 2,004 | `9474d113853416f8e87207d317b5514d680a0b50ca4ff91747e4d853074a2f13` |
| `wrap` (`image.bmp`) | 12,848 | `139e697eaa1bec2cb94473397437d1421a7f01dd087e2437dc741a3c2bb8ea7c` |
| `install-source` (demos) | 12,845 | `0d1f7ee032b13abbbe1767d75fe32c6f1ffa8b7014db44ae35c9d4c47ebb8305` |

The Make contract also injects command-line `CUPIDOBJ` and
`CUPIDOBJ_INPUTS` values. The evaluated graph retains the promoted runner and
the complete seed closure, proving that an ordinary Make override cannot
bypass the boundary.

The regenerated three-root audit retains 452 transforms and 443 under root
`all`. CupidObj still participates in 192 transforms. CupidBuild rises from
four to 190 participations, while Python falls from 448 to 262. No transform
is Python-only.

The complete forced rebuild reached the exact-size gate after all 83 Doom
roots, both CupidLD links, and strict CupidDis inspection passed. The gate
rejected the old kernel row because the edited embedded manuals made
`kernel/kernel.bin` 9,501,584 bytes. After the policy was updated, a top-level
`make -o FORCE all` replay rebuilt the changed documentation and profile
edges, repeated both links and the strict scan, accepted all 16 exact
artifacts, and published the image.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 9,596,984 | `43e5b716ce0eca66ebdae61c19fc4ca0e7451e388987379c55563de499e0f357` |
| `kernel/kernel.elf` | 9,728,056 | `9985ea478d8a5b6a95580ff4452bc2e7f4ed91ccd75656502f65880b624ee3c8` |
| `kernel/kernel.bin` | 9,501,584 | `1cb193cbbc59288fc1f35e2466c8a981ed44f6aeb673c16a637fb2137666c920` |
| `cupidos.img` | 209,715,200 | `64ffda977acfb06327cce3c3620faf48986fb7ee3feb403a401e306638b183d8` |

The 3,382-byte policy covers 38,166,912 bytes and has SHA-256
`5add471e9e19c63623fdd30ee539892a047eefb90c4de1176605ea99e42d69ec`.
A deterministic audit regeneration and its independent checked replay both
passed. Two complete 111-case audit runs exercised every selector, but Windows
ran out of paging-file commit inside one scanner subprocess on the first run
and three on the second. The failed traces were `MemoryError`, not assertion
failures. Each affected scanner contract passed from a fresh process, including
the final pragma case in 65.690 seconds.

A preceding 9,501,220-byte checkpoint, which differed only in embedded manual
text, passed a private four-vCPU E1000 smoke with
`--cpu max --verify-smp-runtime` and ran `/bin/ls.cc`. The final image smoke
could not start before its timeout while the Windows host was out of
paging-file capacity, and it produced no serial log.

## Consequences

Ordinary source and binary wrapping no longer needs Python to launch CupidObj.
The same is true for the three generated installation tables. This is an
invocation handoff, not a claim that the generic runner provides output
locking, format inspection, or atomic replacement. Those guarantees stay with
the specialized transactions that implement them.

Six composite CupidObj paths still use Python, and broader fixed-point,
publication, image, and oracle work remains host-coordinated. No source suffix
changes in this step: all active Toolchain and OS C sources already use `.cc`.
`TempleOS/` remains read-only reference material.
