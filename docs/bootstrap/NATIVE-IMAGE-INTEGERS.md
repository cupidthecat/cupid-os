# Native image integer interpretation

## Current boundary, 2026-10-10

The held implementation parses the decimal values accepted by the existing
image command's `--hdd-mb` and `--fat-start-lba` options. It also supplies a
separate hosted `fgetc` extension for the contract caller. The exact six C
source files are retained in an [unapplied patch](prototypes/native-image-integers.patch).
The [paired evidence](evidence/native-image-integers-20261010.json) binds those
bytes, both actual integer oracles and the complete compiled results.

This work supplies a numeric component for the option owner described in
[ADR 0455](../adr/0455-own-native-image-option-interpretation.md). The normal
image recipe still uses Python. Normal argv handoff, native path
discovery, observer lifetimes, image publication and recipe adoption remain
open. The earlier private `cp7` option and discovery sources are absent from
the current worktree; the retained patch gives this component a current,
reproducible source artifact.

The active producer cohort remains the exact 99 inputs committed at
`1d852023df98c03481c4b3979dccb398288b7115`, with snapshot SHA-256
`4ae98403b0896b24e50b81b11b213b8117207421b9152e95d0500e00e612d07e`.
All fifteen installed seed files retain the `acbbd834` cohort. Cold OS builds,
boots and full compatibility have accepted the qualified heap replacement.
Both Linux SDK profiles pass complete publication and independent audits;
both Windows SDK profiles, original public methods and actual Make bootstraps
remain under their original consumer queue. The normal graph still has
449 CupidBuild actions and three Python coordinators across 452 transforms.

## Integer contract

`cupidbuild_disk_integer_parse` consumes a complete UTF-8 byte span. The caller
selects Unicode 15 or 16 and supplies the decimal digit limit. Zero disables
that limit. The captured Python installations use 4,300 by default and allow
configured limits from 640 upward.

| Actual oracle | Unicode | Decimal digits | Decimal families | Integer whitespace |
| --- | --- | ---: | ---: | ---: |
| Linux Python 3.12.3 | 15.0.0 | 680 | 68 | 25 |
| Windows Python 3.14.3 | 16.0.0 | 760 | 76 | 25 |

The tables come from a sweep of all 1,114,112 code points in each installed
parser. Every decimal digit is also converted by that actual parser. Whitespace
belongs to the table only when the parser accepts it around an integer.
Unicode 16 adds eighty decimal characters; all eighty are rejected under
profile 15 and accepted under profile 16 by each compiled caller.

The parser accepts ASCII signs, decimal digits from the selected profile,
underscores between digits, and leading or trailing integer whitespace.
Negative zero becomes ordinary zero. It rejects internal whitespace,
misplaced separators, nondecimal numeric characters, radix prefixes, NUL,
malformed UTF-8, unsupported profiles and configured digit-limit violations.
UTF-8 validation consumes the whole spelling before a deferred syntax or
digit-limit result. Invalid sequences include overlong encodings, truncated
sequences, surrogates and values above U+10FFFF.

A valid value larger than `UINT32_MAX` returns success with a saturated
magnitude and a clear `magnitude_fits_u32` flag. Sign, zero state and decimal
digit count remain available. The final option owner must select the last
repeated value before checking geometry. This preserves a valid later value
that replaces an earlier oversized value. No oversized value is truncated
into a usable sector count. Geometry bounds and large image production remain
with that future owner.

The function allocates no memory and performs no I/O. Failure clears a supplied
result. Input and result storage must be disjoint; the result is cleared before
the input is consumed. Digit count uses a 32-bit field, and exceeding that
capacity reports an argument error.

## Hosted byte reader

The current hosted `stdio.h` already defines `EOF` but has no `fgetc`
declaration, and the shared runtime has no implementation. The prototype
declares the function in its own header and links it as a separate object.
It reads one `unsigned char` through the existing `fread` and returns its
unsigned value, or `EOF` when the read does not deliver one byte. Native
callers use their own CRT's `fgetc`.

The contracts read all 256 byte values, including `0xff`, then check repeated
EOF, `clearerr`, seek recovery, cursor position and stream closure. They also
check the packet's final EOF and null parser arguments, cleared failure results
and successful reuse after rejection. They do not inject a separate `fgetc`
read error. Integrating the declaration and body into the normal runtime
requires a subsequent producer cohort and its runtime/consumer checks.

## Complete checks and reproduction

The original Linux and Windows builds use their exact qualified native
CupidC, CupidASM, CupidLD and CupidDis images. Each checked program links the
actual shared heap runtime. Native Clang callers use C11, optimization and
`-Wall -Wextra -Werror`. All five checked objects and each final image pass
`--require-known --require-local-targets --require-code-anchors` certification.

Each of the four compiled callers passes both complete oracle packets and the
eighty new Unicode 16 digits under both profiles. That is 16,704 cases per
caller: 66,816 parser calls, including 30,928 positive and 35,888 negative
results. Complete output bytes agree with the actual integer-oracle results.
The numeric, contract and byte-reader objects are equal in full between hosts;
their sizes are 5,540, 6,260 and 556 bytes. Independent review reconstructs
every request packet, rereads every output and source file, checks qualified
tool identities and all 99 raw Git inputs, and verifies unchanged installed
seeds.

The frozen [oracle fixture](prototypes/native-image-integer-oracles.json.gz)
contains all case inputs and expected rows, the two captured Unicode profiles
and the six source identities. Its deterministic gzip encoding decompresses
to JSON. The repository replay extracts the patch into a fresh build directory,
rechecks the current host's actual integer parser, verifies and freezes the
installed native seed manifest, builds both callers and compares all result
bytes. It never applies the patch to the normal toolchain source.

From the repository root on Linux or Windows:

```text
python docs/bootstrap/prototypes/replay-native-image-integers.py --output build/image-integer-replay
```

The replay requires the recorded host Unicode profile and Clang for its
optional native oracle build. Checked production uses only the frozen Cupid
tools. Its compiler has two workers, with 360-second compile, 120-second
assembly/certification, 180-second link and 60-second runtime bounds. Linux
runtime calls also have a 32 MiB address-space bound. All nine conventional
producer variables are forbidden during checked builds and runtime calls.
Existing output directories are rejected; failed products remain available
for review.

Both repository replays pass using the installed cohort: Windows in 15.914
seconds and Linux in 34.604 seconds. Each finishes 33,408 parser calls,
including 15,464 positive and 17,944 negative results, plus the byte/EOF and
direct argument checks. The [replay evidence](evidence/native-image-integer-replay-20261010.json)
records actual tools, sources, whole outputs and the preserved preflight failures.

## Retained failures and next integration

The first native Windows build fails strict compilation on the CRT's `fopen`
deprecation warning. The native fixture now uses its ordinary CRT compatibility
definition while preserving C11 and all warning checks. The first checked
Linux fixture fails because `fgetc` is undeclared; the separate extension
supplies its declaration and implementation without changing the 99-input
cohort.

The next builds compile and link successfully, then fail their first
certification calls because the wrapper supplied unrecognized CupidDis option
names. The correction certifies and runs those same retained products with the
installed strict flags. The original failed receipts are preserved.

The first repository replay attempts use candidate heap manifests. The unchanged
root reader rejects both with `source revision differs`, before any producer
command starts, because its pins still name the installed cohort. The completed
replays use the recognized installed manifests. The candidate consumer queue
continues in its separately projected reader roots.

The [held argument owner](NATIVE-IMAGE-ARGUMENT-OWNER.md) now uses this numeric
component and separate `fgetc` extension. All four callers pass its complete
paired argv, stage and WAD replay. After heap-cohort adoption, the owner must
enter the actual native image command, and `fgetc` must enter the hosted
header/runtime. Complete diagnostics, native discovery and publication need command
comparisons, a new qualified producer cohort, normal Make handoff and paired
OS builds and boots. This prototype does not remove a Python coordinator.
