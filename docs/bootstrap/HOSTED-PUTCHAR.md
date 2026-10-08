# Hosted putchar

The shared hosted runtime provides standard `putchar` through `fputc` and the
current `stdout` stream. It performs the standard unsigned-byte conversion,
returns the written byte, and carries the existing write-error behavior.
The Windows runtime includes this shared source.

`tests/test_hosted_putchar.py` exercises the actual implementation through a
native oracle or an explicitly supplied Cupid-built caller. The accompanying
`toolchain/tests/hosted_putchar_contract.cc` covers every byte value and signed
argument conversion, stream selection, a failed read-only write, recovery after
restoring stdout, complete file preservation and timestamps.

## Integration evidence, 2026-10-08

Both methods pass through all four callers, for eight executions. Native runs
take 0.424 seconds on Windows and 0.255 on Linux. Cupid-built runs take 0.161
and 0.100 seconds. The Linux caller build takes 8.486 seconds; the corrected
Windows build takes 5.870 seconds. Actual source controls include the 99 producer
inputs and the two new caller/test paths. Six qualified parent tools remain
byte-identical to the installed `a1cc8f3a` cohort. No new seeds are installed.

Evidence under `C:/Users/admin/cp7` uses labels
`putchar-integration-source-build-linux1`,
`putchar-integration-source-build-windows3`, and
`putchar-integration-{native,checked}-{windows,linux}1`. Build products retain
source bytes, execution tools, objects, caller images, original commands and
bounds in the integration root's `putchar-source-*` directories. Runtime products
retain all 768 output bytes and the unchanged read-only file.

`putchar-integration-quad-independent1` independently checks all eight cases,
all 101 source controls, the complete generated native source, every actual
Cupid object and caller, strict static ELF/PE profiles and six unchanged parent
tools per host. Every case retains its original 20-second bound. Complete
stdout and file bytes match the contract. Timestamp preservation is checked by
the original tests; their initial timestamps are not separately archived.

The native Windows harness initially encountered the SDK stdout macro and CRT
newline translation in the earlier private prototype. It saves the original
stdout through a helper before replacing the macro and selects binary output
only for that native oracle. The integration's first Windows link selected the
final-path bridge while requesting the compiler import profile. Adding unrelated
objects did not repair that mismatch. The corrected recipe selects the existing
long-path bridge. Those failures remain distinct from implementation evidence.

The new source has not completed paired toolchain qualification or seed adoption.
The independent optional publisher and external-observer records describe
separate private capabilities; this runtime change does not transfer a normal
build recipe from Python.

## Installed-seed consumer acceptance, 2026-10-08

Fresh normal kernel builds pass on Windows and Linux with host producers
blocked. Complete comparison covers all 429 objects and 431 inspection inputs;
only `cupidos-txt/04CUPIDC.o` changes from the retained prior object cohort.
The 178,330-byte manual is a complete nonexecuting payload in its object
and appears once in each of the three matching kernel products. The measured
raw kernel is 9,296,460 bytes. Only its exact policy row changes, and all
sixteen current artifact sizes match.

Both normal image builds and fresh user builds pass. Independent checks compare
every byte of the paired 200 MiB images, sixteen artifacts, 429 objects and six
user products. The complete syscall ABI remains at 103 fields and 101 providers.
The baseline FAT suffix remains byte-identical. The accepted image has SHA-256
`301da60d5ec6166a3740dbd57d4bf8ee0e0957d9120e5d47be3172917b92ea2f`.

All four strict private boots pass with CPU `max`, four processors, e1000 and
the original 150-second bound. Both hosts complete `ls` with SMP verification
and `feature17_iso` with its original PASS marker. Private boots preserve the
accepted image bytes.

Evidence uses `putchar-manual-kernels1-products.json`,
`putchar-manual-policy1/closed.json`, the `putchar-manual-*` command receipts
and `putchar-manual-os-paired-independent1-products.json` under
`C:/Users/admin/cp7`. All 99 current producer inputs remain unchanged through
these checks. Their only differences from the installed qualified source are
the shared runtime and hosted stdio declaration. All fifteen installed seed
files retain their original bytes. Full qualification and seed carriage of
the new runtime remain open.
