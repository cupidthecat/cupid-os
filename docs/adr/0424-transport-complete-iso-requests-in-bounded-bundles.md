# ADR 0424: Transport complete ISO requests in bounded bundles

## Status

Implemented and tested in the isolated ISO worktree on 2026-10-05. Seed
promotion, guarded publication and the normal recipe handoff remain pending.

## Context

ADRs 0420 through 0423 establish inventory validation, retained kind observation,
complete input capture and an independent image check. The producer already
accepts 512 declared entries with 127-byte components. Passing each logical
name and frozen native file path on the command line cannot carry that full
request on Windows.

The real 512-file launch used 182,582 UTF-16 units including the terminator.
Windows rejected it with error 206 before CupidObj started. The existing
output survived. The equivalent Linux launch succeeded and passed complete
independent checking. This is a transport limit; the producer's supported
inventory must remain intact.

## Decision

Add the portable `iso_fixture_bundle.cc` codec and the hosted command
`cupidobj iso-fixture-bundle BUNDLE -o OUTPUT`. The bundle contains the exact
manifest, ordered entry kinds, logical names and file payloads. It carries no
native source path. Bundle mode rejects per-entry `--file` and `--directory`
options and never falls back to loading fixture files.

The `CUPISO1` framing starts with the eight bytes `CUPISO1\0`, followed by
little-endian 32-bit manifest length and entry count, then the manifest bytes.
Each entry contains four little-endian 32-bit words: kind, name length, payload
length and a zero reserved word. Name and payload bytes follow without padding.
Directory kind is 1 and has no source or payload. File kind is 2; an empty file
retains a source view with zero payload bytes.

The codec checks framing, kinds, view validity and storage bounds. It accepts
up to 512 entries, 1,023 bytes per name and 524,800 manifest bytes. The encoder
checks every view and the complete 64-bit size before allocation or payload
copying. Sizes beyond 32-bit storage fail during that preflight. The decoder
checks the entire framing before allocating entry and source arrays. Manifest,
name and payload views borrow the immutable bundle; the arrays belong to the
caller arena. Failure clears the result and restores the caller's arena mark.

The codec does not decide whether a portable name, parent graph or manifest
membership is valid. The command decodes the request and calls the existing
`CTOOL_OBJ_BUILD_ISO_FIXTURE` producer, which retains those checks and the
complete deterministic layout. Its existing semantic output gate preserves
prior output on rejection, including inherited output descriptors on POSIX.

Bundle input allows the existing 64 MiB payload allowance plus the maximum
1,056,784 bytes of framing and names: 68,165,648 bytes in total. The output
limit remains 64 MiB. Metadata must not displace an input that otherwise fits
the image limit.

Native Make, the current candidate plan, Windows behavior images and SDK
bootstrap comparisons carry the codec object. Current plans upgrade the
CupidObj link without changing installed seed plan identities. Conflicting
reserved sources and duplicate or altered codec links are rejected. Repeating
the plan upgrade returns the same plan.

## Evidence

Twenty methods pass with native and Cupid-built codec/producer callers on each
host: 80 selected, 78 executed and two expected Windows descriptor skips. They
cover exact framing round trips, borrowed views, empty files/directories, maximum
metadata and entry count, every truncated prefix, invalid headers and lengths,
trailing data, useful bounded diagnostics, null arguments, invalid source
views, overflow before payload access, allocation rollback, recovery and
concurrent arenas. Producer cases include the active fixture, identifier
collisions, reordered entries, CRLF without a final newline, missing parents,
unsafe names, manifest mismatch, rejected mixed CLI options and output
preservation.

The 44-method regression selection also passes on both hosts: 88 selected,
86 executed and two expected Windows skips. It includes all older hosted
CupidObj commands, the new plan cases and the updated candidate plan locks.
All 82 SDK model contracts also pass on each host after extending the current
fixed-point count and synthetic source/link fixtures. The bootstrap selection
is recorded separately as it closes; these source/model proofs do not stand
in for a fresh staged tool publication.

Checked CupidC compiles the codec, caller, producer core, ELF core and hosted
adapter/main on both platforms. Checked CupidLD links native PE32 and ELF32
callers and producers. Reused support objects are accepted only after their
source and artifact hashes match retained receipts. Four newly checked object
pairs match byte for byte, including the 12,904-byte codec and 13,616-byte
caller. Each host receipt retains 43 source and test inputs.

The persistent full-capacity probe now uses a 139,280-byte bundle and a
310-unit Windows command. Both checked producers return zero and emit the same
1,224,704-byte image with SHA-256
`6f7fa236f8d2b34b1a9ff7d9048cb93a0a4a8877baa2e3293304db940a2dccea`.
The complete retained capture/image checker and the independent Python renderer
both accept it. `iso-bundle-paired-independent.json` rereads sources, actual
commands, suite logs, image profiles, all 513 fixture inputs per host, framing,
objects, outputs and the unchanged installed seeds.

The initial CLI still required per-entry arguments and rejected bundle mode.
Removing that legacy requirement for bundle mode fixes the command while the
ordinary entry-based command retains it. The first harness also used the wrong
fixture path and treated length-bearing ctypes names as NUL-terminated strings;
the corrected harness reads full views and the fixture directory. One Windows
run failed only during temporary executable cleanup; the unchanged next run
passed. The initial external checked builder read `path_encoding` from the
wrong retained source tree and failed before compilation; its corrected lookup
uses the observer support receipt. WSL's older command tests also selected an
unusable `python` entry. A private oracle path selects the running interpreter
without changing producer code. All failed logs and closed receipts remain.

## Consequences

The full inventory can reach CupidObj through a bounded command on both hosts.
This adds source capability and current build carriage. It does not install a
seed, remove Python from normal ISO publication or establish release authority.
The native publisher still needs to bind the retained observer through final
replacement, freeze and compare every input, protect seed/output aliases and
carry the complete request within the transaction's retained-input capacity.
The 158,252-byte manual passes its own installed-seed kernel/image build and
strict private four-CPU boot with completed `ls` and SMP checks. Independent
verification rereads all 1,589 source controls, 429 objects and sixteen artifacts;
only its wrapper changes and FAT data remains intact. The raw kernel measures
9,591,040 bytes. The final and pass-one ELF sizes remain 9,818,556 and 9,687,484.
The 200 MiB image has SHA-256
`e2a684819f1699d5ada9da129b1b1a7f46fe736be63b772d28967d8e8b9f1502`.
The three kernel policy rows come from this independent measurement; all other
rows retain the accepted baseline. This manual acceptance uses the installed
tool cohort and does not qualify a new staged producer.
All new C sources use `.cc`; no source is pruned or weakened. `TempleOS/` remains
read-only reference material and is excluded from these counts.
