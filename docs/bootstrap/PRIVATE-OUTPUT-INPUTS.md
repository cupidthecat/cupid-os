# Retain a checked private output as an input

The separate prototype adds an ownership transfer between a checked producer
and a native candidate writer. `cupidbuild_host_freeze_private_output` moves the
retained private output into the transaction's frozen-input table. It allocates
no payload and copies no file bytes. The accepted identity, complete extent,
digest and timestamp record must match after sealing. The registered input then
retains precise range-read metadata and owns cleanup through transaction close.

This boundary is needed before the disk publisher can run its authorized
CupidObj template producer and compose the image through the owned store.
The normal disk recipe remains Python-owned. The separate
[checked template capture](CHECKED-DISK-TEMPLATES.md) now uses this transfer.
The [combined required-file publisher](CHECKED-DISK-PUBLISH.md) also passes its
four-caller small-image selection and complete current-adapter regressions.
Full normal-image acceptance, complete source qualification and replacement
seed adoption remain separate work.

## Ownership and rejection rules

Require a successful checked-tool operation or an explicit private-output
write. Opening a transaction creates an empty private file on Linux; that file
alone is not an accepted producer result. Every new launch or private-output
write invalidates earlier eligibility before validating its request. A rejected
launch or write cannot leave the previous output eligible for transfer.

Transfer saves the previously accepted snapshot before either capture helper
runs. Sealing cannot replace an accepted digest or extent with a fresh baseline.
The producer's authorization still belongs to the caller. Existing ordinary
private-output bounds, including the 64 MiB limit, remain intact.

A checked launch also retains its candidate. Transfer verifies and discards
only an unused empty owned candidate, using the existing retained cleanup
protocol. A captured, nonempty or published candidate rejects transfer. A
candidate store or publication may not have started. The input table receives
the original handle or descriptor; the transaction's former output slot is
cleared, so close cannot dispose it twice.

Later checked launches, private-output writes and private-output capture fail.
Registered reads, candidate-store work, complete input revalidation and guarded
publication remain available. The returned path is borrowed from the input
table; callers copy its spelling before growing that table.

`cupidbuild_host_frozen_input_snapshot` returns the original registered capture
after checking its current retained metadata. It does not introduce another
digest or recapture authority. An unknown path or invalid result rejects the
operation. Both APIs clear supplied results on failure and prevent publication;
the caller discards and closes the complete transaction.

## Runtime controls and retained failures

The expanded fixture has twenty methods. Positive cases compare the complete
pre-transfer private-output snapshot with the returned frozen snapshot, then
read and publish every payload byte. The actual CupidObj `wrap` case also
compares the complete object with an ordinary invocation of the same selected
seed. Empty and full 64 KiB payloads, input-table growth and later rejected
operations retain their separate cases. Rejections preserve prior public bytes,
their exact timestamp and the expected namespace.

Native Windows selection 10 and native Linux selection 10 pass all twenty
methods. Each runtime retains its original 180-second bound; Linux also retains
its 32 MiB address-space limit. The actual child producer retains 60 seconds.
Matched Cupid-built selection 2 passes the same twenty methods on each host.
Both callers use fresh complete adapter builds through the prepared mixed-wide
compiler; their parent compiler is still a separately retained, unqualified
prototype.

| Caller | Complete selection seconds |
| --- | ---: |
| Native Windows | 3.841 |
| Cupid-built Windows | 4.300 |
| Native Linux | 2.791 |
| Cupid-built Linux | 2.099 |

The final matched caller builds complete in 47.559 seconds on Windows and
49.957 seconds on Linux. Both retain two compile workers and the original
360-second compile, 120-second assembly and 180-second link bounds.
Independent rereading passes in 10.174 seconds. It checks all eighty executions,
complete source and header bytes, actual caller programs and build objects,
original source controls, exact output bytes, timestamps and namespace cleanup.
All four actual producer objects are the same complete 412-byte ELF with
SHA-256 `1269b9be0a85ab247f6bdecf224a6adbd51a4ca2710fe702f9fc70b06e185a65`.

Evidence is `private-output-input-independent2-products.json` under
`C:/Users/admin/cp7`. Final source/build archives are
`C:/Users/admin/cp7/private-output-input-accepted2-windows` and
`/var/tmp/private-output-input-accepted2-linux`. Source copies are made after
the successful closed executions and before the next source change; every
copied byte matches the original before/after controls. Both build receipts
observe 103 producer inputs. Complete source qualification and adoption remain
open.

Earlier executions remain distinct:

- Linux selection 1 incorrectly accepts the transaction's initial empty output.
  The explicit successful-operation record fixes that case.
- Selection 3 reaches the actual producer but Windows keeps its unused empty
  candidate sealed. Linux's positive fixture tries to write a sealed output
  from an earlier explicit write. The positive producer now starts from the
  transaction's fresh output, and transfer disposes only the unused candidate.
- Linux selection 4 closes the candidate descriptor without removing its named
  file. Candidates remain named in the flat Linux layout; the corrected path
  uses the same retained cleanup protocol as ordinary transaction close.
- The first Windows checked-build receipt records a source size in the compiler
  size field. Its original artifacts and receipt remain recorded. Later builds
  record the actual execution compiler size. The first Linux build also fails
  before linking because the harness requests a Windows-only object. Its
  corrected link list keeps the original command bounds.
- The original fifteen-method selection passes all sixty executions and
  independent rereading, but review finds missing controls. Windows selection
  7 reproduces accepted content drift, both early failed-launch paths and a
  rejected private write. The transfer now compares its saved accepted record,
  and the producer entry points invalidate readiness before validation.
  An ordinary Windows stdio open cannot reproduce drift because it omits delete
  sharing; the retained negative uses the actual Windows file API with all
  required sharing flags. Linux's sealed anonymous output prevents the write.
- Selection 9 reproduces a rejected runner-only launch leaving earlier output
  eligible. That entry point now follows the same invalidation rule. Its new
  negative is the sole failure on each host before the repair. The Windows
  checked selection also rejects a pointer `NULL` used as the optional template
  handle: the hosted declaration represents `HANDLE` as an integer. The fixture
  uses the ordinary null constant `0`, valid under both host declarations.

The earlier sixty-case records and complete source/build archives are
`private-output-input-independent1-products.json` and
`private-output-input-accepted1-windows` under `C:/Users/admin/cp7`, with the
Linux archive at `/var/tmp/private-output-input-accepted1-linux`. They document
that narrower selection and do not establish the later fixes.

The accepted parent seed-capture, mixed-wide compiler and public-compile sources
were archived before these host changes at
`C:/Users/admin/cp7/checked-template-parent-accepted1-windows` and
`/var/tmp/checked-template-parent-accepted1-linux`. Their complete artifacts and
original source bytes remain available. None of these private records promotes
or installs a replacement seed cohort.
