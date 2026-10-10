# Reproducible native image argument owner

## Current result, 2026-10-10

A held C owner interprets the existing image command's required options,
repeated values, explicit stages and final WAD group. Native Clang and
Cupid-built callers on Linux and Windows each pass all 11,954 captured argument
sequences. The complete four-caller replay contains 47,816 calls: 37,160 accepted
requests, 3,696 help results and 6,960 rejections. Seven complete shared object
pairs and all four complete result streams agree. Twenty strict CupidDis
certificates pass across the two checked builds.

The source is retained in an [unapplied patch](prototypes/native-image-options.patch),
with a [frozen oracle fixture](prototypes/native-image-options-oracles.json.gz)
and [repository replay](prototypes/replay-native-image-options.py). Both hosts
also rebuild and pass the complete replay with the currently installed seeds.
The [initial paired evidence](evidence/native-image-options-20261010.json) and
[installed-seed replay evidence](evidence/native-image-options-replay-20261010.json)
record complete source, tool, command, object and result identities.

This owner remains outside the normal producer closure. All 99 inputs still
match committed `1d852023` and snapshot
`4ae98403b0896b24e50b81b11b213b8117207421b9152e95d0500e00e612d07e`.
The fifteen installed seed files retain the `acbbd834` cohort. The shared heap's
remaining replacement-tool consumer checks continue separately. Three normal
Python coordinators remain; this prototype transfers no normal recipe.

## Interface and storage

`cupidbuild_disk_options_open` receives the arguments following `image`.
The caller selects Unicode 15 or 16, Python 3.12 or 3.14 option grammar,
POSIX or Windows basename rules, the decimal digit limit and a stage capacity.
The required options are `--seed-manifest`, `--image`, `--bootloader`,
`--kernel`, `--hdd-mb` and `--fat-start-lba`. Optional `--stage` values append;
`--force-format` sets a boolean; the final `--wads` group replaces preceding
groups and follows every explicit stage. Repeated scalar values keep the last
value. Repeated stage destinations retain their original order.

The owner borrows path and explicit-stage strings from argv until close. It
owns the stage vector and each generated WAD destination. The view and indexed
stage query expose those retained views. Close releases owned storage and
accepts a null owner. Failure and help clear the output owner; invalid queries
clear their supplied result. Input and output storage must be disjoint. The
operations open no files and grant no seed or source authority.

| Result | Meaning |
| --- | --- |
| `OK` | Complete argument request is available. |
| `HELP` | Help action was reached. |
| `SYNTAX` | Required values or represented option syntax were rejected. |
| `ARGUMENT` | Invalid API arguments or profile selectors were supplied. |
| `MEMORY` | Owned storage could not be allocated. |
| `CAPACITY` | The final selected stage count exceeds the caller's capacity. |
| `ENCODING` | An argv string fails the existing strict UTF-8 path codec. |

The capacity check follows argument actions and final WAD selection. The full
4,095/4,096/4,097-stage boundary is exercised, along with zero/one capacities,
help after an oversized request and 5,000 discarded WADs replaced by one final
WAD. The original Python parser accepts the 4,097-stage request; its oracle
record explicitly adds the native API's capacity result. This test does not
claim that the original parser imposes that limit.

Allocation probes compile the same owner body again with renamed allocation
and API symbols. They fail each successive owner allocation until a complete
success, reject unknown or duplicate frees and require no retained allocation
after every attempt. A successful probe mutates a borrowed WAD pathname and
checks that its generated destination remains owned and unchanged. Direct
contracts also cover null arguments, failure clearing and out-of-range queries.
The current fixture does not inject malformed argv encodings or a hosted file
read error; those cases require further command integration coverage.

## Actual option and pathname behavior

The owner retains unique long-option prefixes, `--name=value`, repeated scalar
selection, short help clusters and attached-value rejection. Unknown arguments
are reported after represented actions, allowing an earlier help action to
finish. Recognized options cannot supply another option's value. Unknown
option-shaped pathnames containing an ASCII space retain the original parser's
value treatment. The `--` marker ends option interpretation; with this command's
lack of positional arguments, the marker itself is rejected as an extra argument
unless an earlier help action finishes.

The actual installed parsers differ in two ways. Linux Python 3.12.3 checks
ambiguity across the initial token scan before actions. Windows Python 3.14.3
can reach help before a later ambiguity. Their negative-number patterns also
differ: 3.12 uses complete integer or fractional matches, while 3.14 accepts a
`-` followed by an optional dot and a decimal digit as a prefix. Consequently
`-1_000` and `-1x` can be value-shaped tokens on Windows and option-shaped tokens
on Linux. Both grammar profiles are independent of both pathname profiles and
are tested in every caller.

The [numeric component](NATIVE-IMAGE-INTEGERS.md) supplies the existing signs,
underscores, decimal digit families, integer whitespace and digit-limit rules.
An accepted value that exceeds unsigned 32-bit magnitude retains its digit
count, sign, zero flag and range flag. It can be replaced by a later valid
option before geometry validation. This owner does not validate sector layout
or produce the accepted large images.

Stage splitting keeps the original ASCII drive-colon rule and requires a
destination beginning with `/`. Basename selection follows POSIX or Windows
lexical rules, including trailing separators, dot components, drive-relative
names, ordinary UNC names and device spellings. Doom/Freedoom aliases keep their
original case-insensitive precedence; other names use `/wads/wadINDEX.wad`.

The owner returns borrowed raw path strings. The result checker applies the
selected `PurePath` profile to compare them with the actual Python command's
Path values. Full native pathname normalization, filesystem resolution, retained
observers and publication remain with the future command caller. C help/error
results are structured statuses; the C owner does not render the original help
text or diagnostic wording.

## Oracle and execution evidence

The frozen fixture preserves all 6,017 Windows and 5,937 Linux original records
in order, including complete original stdout/stderr and the installed argparse
source. Case names and argument sequences can repeat; no deduplication is used.
Capturing a record invokes unchanged `tools.hostbuild.main(['image', ...])`.
Only its final image publication is intercepted. Both pathname profiles use
the standard library's actual `PurePosixPath` and `PureWindowsPath`. A wrapper
around `_get_value` calls the original conversion before recording the selected
integer spelling. It does not replace integer conversion or parser predicates.

Coverage includes every long-option prefix, required values, last-value
selection, errors before/after help, unknown arguments, negative-number token
classification, short help tails, both basename profiles, alias collisions,
Unicode decimal families and digit limits of zero, 640 and 4,300. Five hundred
deterministic mixed sequences supplement the explicit cases. All four callers
execute both complete host datasets. Complete raw results must match byte for
byte, in addition to every decoded status and returned field.

Initial builds use the exact qualified stage-four tools from `1d852023`.
The repository replay uses a verified installed native seed manifest. Checked
commands forbid all nine conventional host producers. There are two compile
workers, 360-second compile, 120-second assembly/certification, 180-second link
and 60-second runtime bounds. Linux runtime calls retain a 32 MiB address-space
bound. Both native comparisons use strict C11, optimization and warning checks.
The Windows CRT compatibility definition applies to both native compilations.

The installed-seed replays pass in 15.949 seconds on Windows and 19.819 seconds
on Linux, excluding original-parser preflight. Each runs 23,908 native/checked
calls, verifies its host's complete original parser outputs again, and retains
all nine checked objects and ten strict certificates. Independent review checks
the complete files, every field, all seven shared object pairs, all 99 committed
inputs and the unchanged fifteen installed seed files.

## Reproduction

From the bootstrap branch, with native Clang available for comparison, run:

```text
python docs/bootstrap/prototypes/replay-native-image-options.py --output build/image-options-replay
```

On Linux use `python3`. An optional `--manifest` selects a recognized native
seed manifest. The output directory must be fresh and below the repository's
`build` directory. The replay extracts the held patch into that directory,
checks its exact identities, invokes the actual current host parser for every
host record, builds both callers and runs both complete frozen datasets.
It preserves failed products and rejects existing output directories.

## Retained failures and integration work

The first Linux checked run reaches its 60-second runtime deadline after
writing 965,136 bytes. That file is an exact prefix of the complete 969,856-byte
native result, with 4,720 bytes missing. The same retained binary and complete
request finish in 0.806 seconds on Linux's native filesystem under the same
deadline and memory bound, producing the complete equal result. This diagnostic
isolates the cost of many small mounted-filesystem operations and does not
replace the original mounted-filesystem acceptance.

The correction adds 64 KiB read/write buffers to the contract fixture. It
preserves every case, packet format, result field and execution bound; the
owner body remains unchanged. Both full original mounted-filesystem reruns
pass, including allocation probes. The first Windows fault compilation fails
on the missing CRT compatibility definition; applying the same definition as
the main native compilation repairs that wrapper. All original failed receipts
remain held and are included in the paired evidence.

The first independent replay checker mistakes `compile-runtime` for a runtime
execution because it selects labels by suffix. Selecting the two actual runtime
labels restores the original 60-second and 32 MiB checks without rerunning or
changing any program or receipt. That checker failure is retained separately.

After the heap cohort's full acceptance and adoption, this owner must enter a
complete native image command. Native path normalization/discovery, observer
lifetimes, checked release authority, complete diagnostics, the publisher and
large-image behavior still require integrated comparisons. A new producer
cohort, normal Make handoff and paired OS builds and boots precede removal of
the Python image coordinator. TempleOS remains excluded.

The [reproduced observer prerequisites](NATIVE-RETAINED-OBSERVER.md) now retain
explicit quotas, streaming through existing records and captured identity
comparisons. All four callers complete the full 4,096 distinct flat and nested
inputs, and both full legacy selections pass. The
[observer-path extension](NATIVE-OBSERVER-PATHS.md) now supplies first absences,
ordinary UNC roots and Unicode/long directory resolution. The
[source spelling component](NATIVE-SOURCE-PATHS.md) passes 354 calls covering
actual Python lexical rules and useful C failures, with complete paired replays.
Physical pathname selection and the complete discovery owner remain open.
