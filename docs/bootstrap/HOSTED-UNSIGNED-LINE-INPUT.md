# Hosted unsigned conversion and line input

The shared hosted source supplies
`strtoull`, `fgets`, `feof` and `clearerr` in the shared hosted runtime. Both
platform runtimes use it. The conversion handles the full 64-bit unsigned range,
all represented bases, C-locale whitespace, signs, prefixes and end pointers.
The line reader uses bounded unbuffered reads, retains binary bytes and keeps
EOF and error state separate. Successful seeks clear EOF; position queries do
not. [ADR 0451](../adr/0451-convert-hosted-unsigned-integers-and-read-lines.md)
records the contracts and explicit diagnostic policies.

## Runtime evidence, 2026-10-08

Both hosts pass all 32 methods through their native and Cupid-built callers,
for 128 executions without skips. Native Windows takes 2.875 seconds and Linux
1.175. Actual Cupid runs take 1.373 and 0.375 seconds. Each case keeps its
20-second process bound; native builds keep 180 seconds. Checked callers use
two producer workers and the original 360/120/180-second compile, assembly and
link bounds. Corrected Windows and Linux builds take 6.962 and 5.853 seconds.

Conversion cases cover both machine words, exact maximum, unsigned negative
values, absent conversion, all whitespace, valid and incomplete prefixes,
every base, both overflow signs, null end pointers and first-invalid positions.
Long controls consume 8,192 overflow digits and accept 8,192 leading zeroes.
Deterministic generated values cover each base independently of the C arithmetic.
Line cases cover retained newlines, partial and empty files, small and exact
capacities, binary zero/high bytes, unchanged guards and file timestamps, read
errors, indicator clearing, repeated EOF, standard/wide seeking and position
queries. Native line tests extract the actual line-reader body and use CRT
stream state. Cupid callers execute the new runtime's complete stream state.

`hosted-runtime-quad-independent4/closed.json` under `C:/Users/admin/cp7` verifies
all 5,856 represented conversion inputs from retained stdin using separate
integer arithmetic. It checks complete stdout, complete generated native C,
all 110 source controls, the 106 private producer paths, supporting Python
modules, all actual objects and caller images, strict static execution profiles
and six unchanged qualified parent tools per host. Original case and producer
bounds remain in every invocation record. All retained line input files keep
their complete bytes and original recorded timestamps.

The checked build copies are `hosted-runtime-source3` beneath each private root.
Runtime evidence uses `hosted-runtime-{native,checked}-{windows,linux}3`;
retained products have separate `-strtoull` and `-fgets` suffixes. The paired
source projection and earlier failed runs stay under `C:/Users/admin/cp7`.

## Failed attempts and oracle boundaries

The first native Windows fixture used text output and changed LF to CRLF.
The corrected native wrapper selects binary stdin/stdout. The first actual
Cupid caller exposed the missing `fgets` declaration. Its runtime object already
compiled the unsigned conversion; that partial build did not establish caller
acceptance.

Windows CRT gives no-conversion end pointers for five incomplete hexadecimal
strings in each of bases zero and sixteen. The represented function consumes
their valid leading zero, matching the C contract and Linux reference. The test
retains exact CRT expectations separately while preserving the Cupid predicates.
A targeted probe also shows native Windows write-only reads set the stream error
but keep errno unchanged. Native tests require that observed CRT behavior;
Cupid tests continue to require the adapter's changed, nonzero errno.
Diagnostic instrumentation exists only in the retained probe source.

The first independent checker mistook a generated-value test's local `prefix`
expression for the native wrapper constant. The corrected checker selects
literal wrapper definitions and supports the explicit 8,192-digit controls.
All failed receipts and generated source remain retained.

## Bootstrap integration and OS acceptance

The reviewed integration copy under `C:/Users/admin/cp7/hosted-runtime-integration-source1`
retains the normal 99 producer paths. Exactly the shared runtime and its stdio
and stdlib headers differ from the preceding byte-output source. Both newly
built Cupid callers pass the 32 methods, for 64 further executions. Both also
pass the two putchar regressions. `hosted-runtime-integration-independent1/closed.json`
checks 103 complete source controls, all actual products, static profiles,
commands and six qualified parents per host. Its 128-method count combines the
64 new executions with the earlier 64 native executions, whose complete
generated source contains the identical library bodies and fixtures.

`hosted-runtime-applied2/closed.json` records the exact application of three
producer paths, four test paths and these records to the bootstrap worktree.
The source inventory matches the isolated paired preparations. The first
application guard stopped before copying because the qualification wrapper had
reused its checker's result filename. Corrected verification writes result facts
and runner receipts separately; both complete checks pass again.

Both complete preparations pass. `hosted-runtime-preparations-source-independent1/closed.json`
checks all 291 prepared products and 97 complete fixed-point pairs against the
actual integration source and fifteen unchanged installed parent files. These
preparations remain explicitly unqualified until committed-source behavior
acceptance. Fresh tests from the applied worktree pass all 34 conversion,
line/state and putchar methods through four callers, for 136 executions without
skips. The native products retain their complete generated source and build
records; the Cupid selections execute the identical integration caller bytes.

`hosted-runtime-root-independent1/closed.json` independently checks the 128
fresh conversion and line/state executions. It rereads all 103 source controls,
99 producer inputs, complete generated native C and actual Cupid products,
then recomputes all 5,856 conversion inputs with separate integer arithmetic.
The eight fresh putchar executions have their separate passing runtime records.

The 179,862-byte manual passes both fresh normal kernel and image builds with
nine conventional producers disabled. Windows kernel/image commands take
2,474.703 and 2,471.504 seconds; Linux takes 1,688.329 and 1,784.043. Independent
measurement compares all 429 objects and 431 inspection inputs. Only the manual
wrapper differs from the preceding byte-output image. The raw kernel measures
9,297,992 bytes, the final ELF 9,527,740 and pass-one ELF 9,396,668. Exactly those
three kernel policy rows move to the measured sizes; all sixteen artifacts pass.

Fresh user checks and all four strict private four-CPU boots pass with the
original 150-second bound, maximum CPU profile and E1000. The paired independent
check passes in 5.956 seconds and compares complete objects, artifacts, six user
products, all 103 ABI fields and 101 providers, the complete manual and both
200 MiB images. Both images have SHA-256
`beb2df2cd69eb2290486564a598fe8bfb6f4acfb52f0f0c5882ca14a25736b79`.
The complete 199,229,440-byte FAT suffix retains its baseline digest. All fifteen
installed seed files and the 99 producer inputs remain unchanged throughout.
Evidence uses `hosted-runtime-manual-kernels1-products.json`,
`hosted-runtime-manual-policy3/closed.json`, the `hosted-runtime-manual-*`
command receipts and `hosted-runtime-manual-os-paired-independent1-products.json`
under `C:/Users/admin/cp7`.

## Committed preparation binding

Commit `84ae3852923a18dd6dab3f2a659f1f1d8ef684c0` contains this library source.
Its 99-input producer snapshot is
`7953f8cc9c3843590ef11dae6dd7d44a96e056e40c17c470b5cc38389c04e433`.
`hosted-runtime-preparations-committed-independent2/closed.json` compares every
input with its actual Git blob and both retained preparation source copies,
without line-ending normalization. It rereads all 291 products and compares
all 97 fixed-point pairs byte for byte. All fifteen installed seed files retain
their accepted identities.

The Linux preparation also has an exact retained copy on the shared Windows
filesystem, so native Windows can read both prepared cohorts. The check compares
every copied file with the original and rechecks both originals. The first
checker could not open a Windows-linked worktree through Linux Git; the corrected
checker uses Windows Git to read those same committed blobs. Both receipts remain
retained, and none of the source or product predicates change.

`hosted-runtime-release-author1` writes the paired
`hosted-runtime-reviewed-release1.json` in the bootstrap worktree. Both complete
qualifiers pass under `hosted-runtime-source-qualify-{windows,linux}2`, with
their original producer bounds and two workers. Windows takes 2,413.305 seconds
and Linux 2,580.462. Their output directories are
`hosted-runtime-seed-qualification-{windows,linux}1` beneath that worktree.
The first launches rejected output paths outside the source root before
qualification; the corrected launches preserve that containment requirement.

`hosted-runtime-qualification-{windows,linux}-independent1/closed.json` binds
both complete qualifications to the actual release, preparations and all 99
raw committed Git blobs. Windows contributes 159 staged products, 53 complete
fixed-point pairs and 3,423 published files; Linux contributes 132, 44 and 4,338.
The paired totals are 291 products, 97 pairs and 7,761 files. The checks verify
both behavior generations, exact source and plan digests, all twelve described
release artifacts and every published file. All fifteen installed parent files
retain their accepted identities. This qualifies the committed library source;
it does not install its seeds.

The installed mixed width cohort below completes replacement seed carriage
and normal OS/SDK/public acceptance for this library.
The external frozen-input and publisher prototypes keep their separate gates.
The normal disk recipe and both SDK coordinators still use Python.

## Installed mixed width cohort, 2026-10-09

The qualified `acbbd834` pair now carries this capability in both installed
host tool sets. All four ordinary/long-path SDK profiles, complete public
bootstrap methods and both actual Make bootstraps pass independent review.
Current paired OS image and fresh user builds, all four strict boots and
complete product comparison also pass with the revised manual.
[The installed cohort record](QUALIFIED-MIXED-WIDE-SEEDS.md) binds the exact
source, release, original failures and final adoption evidence.
Private host extensions and retirement of the three normal Python
coordinators retain their separate acceptance requirements.
