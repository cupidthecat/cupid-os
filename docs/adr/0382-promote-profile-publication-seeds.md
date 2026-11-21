# ADR 0382: Promote the profile-publication seeds

Date: 2026-09-19

Status: Accepted. Final-manual parallel OS replays and the private four-CPU
runtime frontiers on the Linux image with E1000 and RTL8139 pass. The
Windows image with preserved FAT contents separately passes an SMP and `ls`
smoke.

Both clean proofs from `16a86f5b` passed. Independent verification matched
the canonical 59-input inventory and every stage-three/stage-four object and
tool. The identities below bind the repaired checked pair. Its complete CLI
suite also passed on both hosts. Production regression suites passed on
Windows and Linux. The pre-correction Linux normal build passed all 16
artifact rows and published the image used by the successful validation
frontier. The first serial Windows replay timed out in broad CupidDis
inspection; a later isolated guarded replay passed with unchanged deadlines.
Its original cause remains unknown. Both later default OS builds passed the
same corrected manual checkpoint. After the final manual freeze, parallel
replays pass on both hosts, and the private four-CPU runtime frontiers on
the Linux image with E1000 and RTL8139 pass. The separate pinned-IWAD Doom
gameplay gate remains open.

## Context

The source-current CupidBuild command can publish the closed Doom profile
manifest and launch checked CupidC. Its publication protocol also depends on
CupidASM's caller-owned output mode and the matching Linux and Windows runtime
changes. The previous checked pair predates those capabilities.

Initial candidates from `34b597aa` converged, but a later post-install drift
test exposed a Windows rollback sharing violation. ADR 0385 records the repair;
ADR 0384 adds the typed ISO pattern transaction. The next `962e476b` proofs
passed, but their full Windows replay exposed the command-line limit: 431
absolute frozen-input paths occupied 45,255 bytes. The old raw kernel remained
intact. ADR 0387 records the short-name repair, ADR 0389 records the POSIX
working-directory repair, and ADR 0388 defines the retained-parent recovery
limit on DrvFS.

The repaired proofs bind the same 59-input cohort from revision `16a86f5b`. Linux
reconstructed a clean Git archive on WSL's native filesystem; native Windows
used a clean detached worktree. Every reported source size and SHA-256 was
checked against the clean source inventory. Every
stage-three object and tool was also compared with its stage-four counterpart.

## Decision

Promote the paired stage-four cohorts from revision
`16a86f5b1693e017c36c6d902df9946c5d674b17`. Both manifests bind source snapshot
`54b411b6ed05725101f5859106facb43639f2d02cb2b0ea2f6334e21375bda55`.

The 6,602-byte Linux manifest has SHA-256
`d16626ec2dc1fde37114b080e8e855022a4d5ac768eddb3777862ce24ad3ac9d`.
Its build plan remains
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`.

The 2,852-byte Windows manifest has SHA-256
`bd4d5435301972fba4ba55e0edfe7451a876fd56b3dbe73fc60a4deca61e43dc`.
It names the new Linux manifest as its plan seed and binds native plan
`98e09aab876a9fa37ec07c38a0a57a014549a14c0ab10c740b3f80ede9d65669`.

Both retain the preceding `0232cb57` cohorts as parents. The host verifier
pins the new source identity and every artifact's exact size and hash. Its
promoted Windows branch now requires the current ordinary, linker, and
CupidBuild import profiles. Historical v1 validation keeps its original
profiles. The Cupid-built readers already accept the complete parent and plan
transition described by ADRs 0380 and 0381; their source closure does not
change during this promotion.

The artifact-size policy records the new tool sizes, and the Toolchain
manifest contract binds the new Linux manifest. The separate normal profile
recipe transfer is described by ADR 0383.

## Evidence

The failed `962e476b` full-cohort replay linked a 9,642,316-byte pass-one kernel and a
9,773,388-byte final ELF. An independent checked CupidObj probe flattened the
final ELF to 9,543,692 bytes with SHA-256
`ff6a3da23840ae3680322a551fe6edfa9b3edf41d361bb8f6edaeeec6939e47a`.
That probe is diagnostic evidence only; it did not publish the normal raw
kernel or complete the guarded flatten transaction.

The clean `16a86f5b` Linux proof matches 22 C objects, one startup object, and all six
tools: 29 final artifact pairs. It passes 33 failure, seven help, and 38 success
groups. Its 49,189-byte report has SHA-256
`f1640d5a58399969a004a6e8732b9aaf5da9b5c2cbf69ceb734713c046233512`.

The clean `16a86f5b` native Windows proof matches 23 C objects, three assembly
objects, and all six tools: 32 final artifact pairs. It passes 21 failure,
seven help, and 25 success groups. Its 68,212-byte report has SHA-256
`3baa028b0d976a0823329d825ffa8263837b7e1b9e12abb37901067864804447`.

Independent verification rehashed both canonical manifests and compared all
29 Linux and 32 Windows final artifact pairs. The clean proof worktree was at
the recorded revision without tracked changes. The CupidBuild images are:

| Target | Bytes | SHA-256 |
| --- | ---: | --- |
| Linux | 511,572 | `6f5de7a82c66fbc8a1c47ca0d63539271762511330c2f31ade144cfadb06ad62` |
| Windows | 525,824 | `595b2700ceacad6dc6ebc4958a91d6d5f79f527514ed7b9c72c66d0321b435b3` |

Final-cohort CLI acceptance ran 155 tests per host: native Windows passed in
267.792 seconds with 12 platform skips; Linux on `/mnt/c` passed in 1,528.642
seconds with 16 Windows-only skips. Both 500-input cases passed without an
old-capability skip. The source and both manifests retained the identities
above. The bootstrap log records the complete log sizes and hashes. These
runs are separate from the earlier `962e476b`-based source preflight.

The corrected production suite passed 167 tests in 1,827.789 seconds with
two platform skips. The isolated Linux handoff suite passed 48 tests in
114.080 seconds with one Windows-junction skip. Linux ran both cases omitted
on Windows, covering profile-parent symlinks and a replaced output directory,
alongside parallel ordering and ISO publication. The log preserves the earlier
conditional-count failure and records both successful reruns. Corrected-manual
OS builds, final runtime acceptance, the complete Toolchain contract suite,
and the user build subsequently pass.

The full native Windows post-promotion reproof passed. Independent checks
matched all 32 final artifact pairs and every checked seed tool against its
stage-two, stage-three, and stage-four image. All six initial-seed equality
fields were true, and the 21/7/25 behavior inventory passed. Its 68,206-byte
report has SHA-256
`c099012813d313f3b6694f5874e9f5642f342e5218dffb5271d1dbc83eb1a2e1`.
This run uses the current Windows and Linux manifests above as parents and
retains the same source snapshot. It proves promoted-seed self-consumption,
separately from the clean proof built with the original `0232cb57` parents.

The Linux Toolchain publication also reproduced all six checked seed images
exactly. Its author and Python oracle agreed on all 62 stage pairs before
publishing 22 artifacts. The final Windows `CUPMAN2` verifier rejected
concurrent repository-root directory drift, then passed unchanged in a
stable copy containing the verified sources, all 76 contract inputs, all 59
bootstrap inputs, and the actual published artifacts. The manifest has SHA-256
`ba5e531172f4861237238d403f8119f229c3e6d1267a483277efc3f19e615c92`.
This proves Linux promoted-seed self-consumption and stable final manifest
verification for that exact contract cohort without weakening the drift guard.
The full contract test then failed on a stale 398-occurrence preprocessor
expectation. The source-current `403u` count, ABI-consumer repair, and three
refreshed self-host object expectations now pass a fresh publication. All
62 stage pairs agree; an independent comparison also matches the six
published tool images to the checked Linux seed. Native `CUPMAN2` verification
passes all 22 artifacts on Linux and Windows. Its 29,619-byte manifest has
SHA-256 `419d1e6f3b4a90b1cbfae04f5aa397c900449efe7d4197b7116f0cadb5070afd`
and binds 76 publication inputs and 59 bootstrap inputs. The Windows stable
copy includes all 21 verifier build inputs as well as both seeds and the
exact published cohort. The bootstrap log records the copy-preparation
failures and their corrections. The full executable suite and user build pass.
The 59-input bootstrap source closure and checked seeds are unchanged.

The pre-correction clean Linux OS build compiled all 240 kernel and Doom roots,
completed both links, and published the guarded raw kernel. It exited with
status 2 at the old exact-size policy. After the policy took the measured
sizes, a separate native verification passed all 16 artifacts. The raw
kernel at that checkpoint was 9,550,776 bytes, the final ELF was 9,777,484
bytes, and the pass-one ELF was 9,646,412 bytes; the bootstrap log records
their exact hashes.
The separately named validation image passed a private four-CPU E1000 runtime
frontier with SMP verification and `ls`. Its guest log is 179,931 bytes with
SHA-256 `6abe35942e91296694bbd9f8eb7bf8f4ff3716d48b71e7cf6305a025c16b3717`.
The subsequent pre-correction Linux Make retry passed all 16 exact artifacts
and published a normal image byte-identical to that validation image. Its
237,592-byte log has SHA-256
`8dbdf822f7fcdad4124ab41209df05bb8917ab77f227dff50b3af46df2dc4c5a`.
These build, measurement, and runtime results precede the historical-label
correction in `04CUPIDC.CTXT`. The corrected Linux build then published both
ELFs and a 9,550,804-byte raw kernel before rejecting the old raw-size row.
Only that policy row changed. The bootstrap log records all corrected hashes;
later final-manual replay and runtime results are recorded below.

The Windows serial replay exited with status 2 when the broad checked
CupidDis request reached its 300,000-millisecond timeout. Both published ELFs
match Linux, but the previous raw kernel remained unchanged at 9,536,524
bytes. Read-only inspection of all 431 inputs later passed in 225.053 seconds.
An isolated full guarded replay then published the Linux-matching raw kernel
in 699.9794829 seconds, using the unchanged 300/600/300-second inspection and
CupidObj deadlines. The bootstrap log records the copied cohort and hashes.
This is diagnostic-root publication, not main Windows Make acceptance. The
original timeout's cause remains unknown; no source or timeout change was
made for this replay.

The subsequent main Windows `make -j1 all` passed all 16 size checks, ISO
pattern publication, and image publication. Its corrected-manual kernel
artifacts match Linux exactly; the raw kernel is 9,550,804 bytes with SHA-256
`3f2b5d85a9d6925c0151e5c5334c04a94332a4505bb0b8d86d96d1216816db99`.
The 209,715,200-byte Windows image has SHA-256
`6a05ec0ccce3c9af0c70e95699f50694e4c39724d8f32dec1f2cd369db7a1830`
and preserves existing FAT contents. This records a completed checkpoint,
not cross-host disk-image parity. Further CTXT historical-count corrections
require a content-only `make -o FORCE all` replay, renewed size measurements,
and final normal-image smoke before handoff acceptance.

Native Linux `make -j2 all` also passed all 16 exact-size checks at the same
149,324-byte manual checkpoint. Its 237,580-byte log has SHA-256
`a3f4e8cd293bac53a9022a8e6193b3da782eaa5f4461530d1fc5365831a4be74`. The
209,715,200-byte image has SHA-256
`b04bc14e034c983604bd5dd2881e9c8a4edfb136e9db83006282109d9c337d70` and
preserves the native tree's earlier FAT contents. Both default builds are
complete at this checkpoint. The later final five-manual freeze passes
parallel replays on Windows and native Linux, with all 16 artifact checks
and image publication. Both produce the 9,550,844-byte raw kernel with
SHA-256 `9db47c925e44352530ccddc20955a80fecca025eb789395152427f60663d703c`.
Its private four-CPU frontiers with E1000 and RTL8139 pass compiler, ISO,
swap, graphics, audio, USB replug, and SMP checks. The bootstrap log records
exact logs and image hashes. The separate pinned-IWAD probe panics during
HomeFS rewriting and does not establish Doom gameplay acceptance.
The separately published Windows image also passes a private four-CPU
E1000 boot smoke with SMP verification, CupidC-built `ls`, and the required
post-command survival interval.

The other five tools on each platform are byte-identical to the
`962e476b` candidates. Candidate convergence, promoted-seed self-consumption,
and normal OS build and runtime results remain separate checks in the
bootstrap log.

## Consequences

Both checked CupidBuild images carry the profile publisher, checked CupidC
runner, typed ISO pattern transaction, and repaired publication protocol.
They also carry the 500-input Windows launch repair and pinned `/proc`
working directory for anonymous POSIX author and inspection calls. Generic
checked commands retain their requested directory. DrvFS recovery evidence
does not claim restoration of an ambiguously rebound public name.
Linux CupidC, CupidDis, and CupidLD
remain byte-identical to the previous seed. Linux CupidASM, CupidObj, and
CupidBuild change; all six Windows images change with their shared runtime.

This promotion adds no language restriction and removes no OS source. Active
C source already owned by CupidC has the `.cc` suffix. The remaining `.c`
files belong to the audit's explicit historical, dormant, or host-oracle
cohorts. Disk and ISO publication, guarded compilation, contract verification,
and fixed-point coordination still contain Python-owned work, so issues #32
and #34 remain open. `TempleOS/` remains read-only reference material.
