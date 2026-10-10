# ADR 0459: Coalesce required frozen-input revalidation

## Status

Implemented in separate private source-four copies on 2026-10-09. All four
five-program builds, complete runtime selections and independent small-image
checks pass. Seven of eight clean 200 MiB cases pass; checked Linux reuse still
exceeds its original limit. Complete large-image and boot acceptance remain
open. Normal source and installed seeds are unchanged.

## Context

The accepted required-file handoff retains every ordinary input and frozen
capture. Its checked 200 MiB reuse attempts exceed their original 600-second
limit. Phase traces reach transaction close after publication returns.

An unchanged-algorithm diagnostic performs 49 complete 200 MiB digest passes.
The complete eight-MiB FAT-reuse method supplies a shorter work-count check:
eleven calls reach live-input validation, and five calls have both a private
output row and a previous-output row. Each of those calls validates the same
complete frozen set twice. Both red runs detect those duplicate passes.

## Decision

Within one live-input validation call, remember whether any previous/private
output row requires frozen-input validation. Validate every ordinary live
observation, then validate the entire registered frozen set once when required.
Calls without a qualifying row retain their existing ordinary-input path.

Keep the complete frozen set, file digests, borrowed-observer lifetimes and
previous-output rules. All other publication, candidate, output and cleanup
guard entry points retain their existing checks. No validation is cached across
calls or across a publication boundary.

The work-count feedback rejects both repeated full passes and a missing required
pass. Coalescing makes all eleven actual FAT-reuse calls pass that feedback.
The diagnostic instrumentation remains isolated from the clean source.

## Evidence and limits

The checked Linux diagnostic retains the complete 200 MiB geometry, original
600-second case limit and 32 MiB address-space limit. It closes in 542.555
seconds, with 44 whole-image digest passes. Recorded SHA update bytes fall from
11,843,761,977 to 10,642,251,917. Its complete image matches the unchanged
diagnostic's SHA-256:
`2980876d2bfde8b210505b138b7a7ba7d07420b4a803da7eabcf796ae4fdfd01`.

The unchanged diagnostic also closes, in 587.114 seconds. That observation
does not repair the original failed attempts. Concurrent load and instrumented
code layout prevent a stable timing-ratio claim; the reduction in counted work
is exact. One diagnostic pass does not accept the complete large-image cohort.

The clean copies retain all 209 controls and 109 private producer inputs from
the accepted handoff. Only `toolchain/cupidbuild_host.cc` changes from source
three. All four builders retain the original producer bounds. Full native
Windows and Linux regressions close in 323.663 and 270.108 seconds. Checked
Windows and Linux close in 2,763.886 and 1,996.940 seconds. Independent checking
closes in 234.852 seconds and verifies all 664 selections, 646 executions,
eighteen skips and 652 calls. It rereads every control and all 23 complete checked
object pairs, reconstructs every positive image and checks negative bytes,
timestamps and namespaces. Every other checked object equals source three.

The original eight-case large queue closes with seven successes. Both Windows
checked cases now finish under 600 seconds, as do all four native cases and
checked Linux fresh publication. Checked Linux reuse still times out at 600
seconds with 32 MiB. Its incomplete namespace and original failure remain held.
This does not qualify the complete large cohort or authorize guest-boot/adoption
claims. The separate instrumented Linux success remains diagnostic evidence.

Evidence includes `required-handoff-frozen-repeat-red-linux{1,2}.json`,
`required-handoff-frozen-repeat-green-linux1.json`, the counts/coalescing
diagnostic `closed.json` files, `disk-required-handoff-source4` and its paired
Linux copy. Closed small acceptance is
`disk-required-handoff-independent8-products/closed.json`. The large queue is
`required-handoff-source4-large8.json`; its rejected child is
`disk-required-handoff-checked-large-linux5.json`. All eight clean-source large
cases and their independent FAT/image checks must pass before strict guest boots
and adoption.

The original disk-full, WSL I/O and bounded-time failures remain retained.
Lossless compression of closed outputs checks complete hashes, lengths and
modification times without deletion. Root seed adoption, normal command
ownership, producer qualification and retirement of the three Python
coordinators remain open. TempleOS stays read-only and excluded.
