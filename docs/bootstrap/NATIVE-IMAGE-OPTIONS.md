# Native image command options

The private source at `C:/Users/admin/cp7/image-options-source1` interprets the
existing image command's options in Cupid C. Linux holds the same 113 source
and support files at `/var/tmp/image-options-source1`. The normal image recipe
continues to use Python; path discovery, retained observer lifetimes, complete
publisher integration and recipe adoption remain open.

## Source and ownership

The new implementation is `toolchain/cupidbuild_disk_image_options.cc`, its
header and `cupidbuild_disk_image_decimal.inc`. It builds on the separately
verified [stage argument functions](DISK-STAGE-ARGUMENTS.md). The other two new
paths are `toolchain/tests/cupidbuild_disk_image_options_contract.cc` and
`tests/test_cupidbuild_disk_image_options.py`.

The owner parses the six required manifest, image, bootloader, kernel and
geometry options. Repeated single values keep the last value, including a
valid geometry that replaces an earlier out-of-range value. Explicit stages
accumulate; the last WAD group replaces earlier groups. Explicit stages always
precede that final group. Unique long-option abbreviations and `--name=value`
forms retain their represented behavior. Both host basename profiles preserve
the existing Doom/Freedoom aliases, indexed fallback names and dot components.

The argument owner borrows path and explicit-stage views from argv, owns the
stage vector and WAD destinations, and releases its storage on close. Up to
4,096 stages retain the existing capture capacity. Failure clears the output
owner and queried view. Parsing opens no files and grants no source authority.
Filesystem resolution, FAT destination checks, checked release authority and
publication remain with the future command caller.

Geometry keeps unsigned 32-bit sector values without truncation. The four
previously accepted geometry requests, including the 4,096 MiB image, preserve
their complete values. These tests interpret parameters; they do not produce
the large images. Decimal signs, separators, Unicode digits and integer
whitespace are checked against each host's actual existing parser. The retained
table covers Unicode 16.0.0; Windows uses that oracle, while Linux's oracle is
15.0.0. Runtime tests discover each host's complete digit/space set independently
of the table-generation record.

The source stays outside the normal 99-input producer closure. That snapshot
collector includes every top-level Toolchain header, so introducing these
headers during the current producer-consumer queue would change its inputs.
All 99 current files still match committed `acbbd834`; fifteen installed root
seed files retain the accepted `a1cc8f3a` identities.

## Executed checks

Native and qualified normal-source Cupid compilers build all four callers.
Every caller passes all 25 methods, for 100 method executions and 780 actual
program invocations. Windows runs 199 calls per caller and Linux 191. Cases
compare the existing `hostbuild.main` parser with only image publication
intercepted, exercise both basename profiles, preserve ordered collisions,
cover the entire 4,096-stage vector and check useful syntax, encoding, numeric,
missing-value and capacity failures. Each invocation retains twenty seconds.

The final labels are
`image-options-{native,checked}-{build,contracts}-{windows,linux}3`.
Build receipts archive all 113 source/support files, actual programs, compiler
identities and borrowed runtime objects. Cupid compilation keeps two workers,
360-second compile and 180-second link bounds. Every new checked object passes
strict disassembly; complete PE32/ELF32 programs retain their execution profiles.

`image-options-independent3/closed.json` passes in 10.521 seconds. It rereads
all retained sources, commands, programs and complete output sequences, matches
the checked tools to the qualified release, checks all 99 raw committed inputs,
and verifies unchanged installed parents. All four complete checked object
pairs agree between hosts. Native and Cupid output bytes agree for every
invocation on each host; the two common cross-host input sequences also agree.

The options object is 17,828 bytes with SHA-256
`20cbd71072e27d2ca140848ad7e1387aac4c54af7fd10837859c949c629a8e1d`.
The final source-control record has SHA-256
`00c90ac042746790fae871366f0357efd009ead5d7c71a59cb176015aecf8a06`.
All evidence remains under `C:/Users/admin/cp7/`.

## Retained failures and remaining work

The first checked fixture build calls `strtoul`, which the represented hosted
header does not declare. The fixture's capacity argument now uses an explicit
full-width unsigned value and the already represented `strtoull`. The options
implementation, hosted header and runtime are unchanged by that repair.
This does not establish hosted `strtoul` support.

The first independent check detects native Windows CRT CRLF output where the
hosted runtime emits LF. Its parsed results already agree. The native Windows
fixture now selects binary stdout; the independent checker keeps complete byte
equality. All original source copies, programs and failed receipts remain held.

Source-plan integration still needs the complete native command's compatible
diagnostics, absolute path discovery, physical-root deduplication, argv and
observer lifetimes, seed/output authority and publisher handoff. Whole publisher
execution with the UNC host copy, accepted large-image publication and normal
recipe adoption remain separate gates. No ownership count changes here.
TempleOS remains read-only and excluded.
