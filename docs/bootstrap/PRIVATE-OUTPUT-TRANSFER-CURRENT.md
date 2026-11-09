# Current checked-output transfer

The rebuilt private adapter can transfer an accepted checked-tool output into
its frozen-input table without copying the payload. The final selection passes
92 native and Cupid-built case executions across Linux and Windows. The same
implementation also passes all 68 unchanged expanded launch regressions.
Active host sources and installed seeds remain unchanged.

## Ownership boundary

`cupidbuild_host_freeze_private_output` keeps the accepted identity, extent,
digest and precise timestamps. It checks the saved record before and after
sealing, then moves the retained handle or descriptor into input custody.
It discards only a verified unused empty checked-tool candidate. The old output
slot no longer owns cleanup, so closing cannot dispose the input twice.

Every new producer attempt invalidates earlier eligibility before request
validation. Untouched initial output, failed requests, nonempty or captured
candidates, repeated transfer and started stores reject the operation. Later
launches and private writes reject a transferred output while registered reads
and native candidate work remain available.

`cupidbuild_host_frozen_input_snapshot` returns the original accepted record
after revalidating the frozen inputs. Both new APIs clear supplied results on
failure and prevent publication. Returned paths borrow the input table; callers
copy the spelling before growing that table. Ordinary output limits remain
unchanged, including the 64 MiB bound.

## Complete payload checks

The final callers publish the complete transferred explicit output, empty file,
actual CupidObj ELF object and help output through the native store. A 3 MiB
payload is also checked in full. Independent rereading compares every final
byte, validates the ELF object, and checks source, SDK, program and build guards.
Input-table growth, rejected producer attempts and real metadata drift retain
separate controls.

Each transfer invocation keeps 180 seconds; each launch regression keeps 60.
Linux retains its 32 MiB address-space and 10,240-descriptor limits. Earlier
76-case and 88-case development selections pass independent review on the same
implementation. The complete reviewed set has 324 executions; the final transfer
and unchanged launch selections account for 160 of them.

## Retained fixture failures

An untouched Windows transaction need not have the empty entries assumed by
the first fixture. Corrected controls prepare their required candidate and still
reject unproduced output. The checked Windows fixture also needs time-query
declarations and bridges already supplied by the native SDK.

Immediate Linux permission changes can retain the same anonymous-file ctime.
A focused immediate/delayed probe confirms that a 50 ms pause produces actual
drift and rejection. Windows changes the modification time by 100 ns.

A suspected inherited sealing flag proves harmless: the existing close helper
already clears it. That sequence fixture first has a DWORD format error, then
expects help bytes after an explicit rewrite and stops before its writer probe.
The corrected control permits a writer before transfer and receives Windows
sharing error 32 afterward. It writes no foreign byte after transfer, and this
investigation makes no adapter repair beyond the transfer implementation.

## Evidence and next work

The [evidence archive](evidence/private-output-transfer-current-20261109.json.gz)
retains 1,442 file records, 153 embedded payloads and fourteen references to the
qualified producer archive. It includes complete private sources, callers,
actual programs, public payloads, failed fixtures, probes and independent reviews.
Its SHA-256 is
`a06c6537b57cadc7c40c74a946ace6a6afb1ea46c17c043a6e67b5e09a796b1b`.

Fresh full-file checks above four GiB are running against this adapter version.
The accepted earlier large-file results concern v26. Registered-input range
streaming, checked template/publisher reconstruction, active ABI/source-plan
integration and qualification remain open. Native image-command ownership and
installed seed adoption remain separate gates. The
[historical transfer record](PRIVATE-OUTPUT-INPUTS.md) describes the earlier
prototype and its distinct evidence.

## Guest manual and image

The updated guest guide passes a guarded relink using every independently
matched preceding normal object. Only the exact raw-kernel size changes, from
9,415,688 to 9,416,096 bytes; both ELF size rows remain unchanged. All sixteen
artifact gates, seventeen policy tests and the ownership audit pass.

A strict private four-CPU max/e1000 boot passes all 62 TLS checks and ordered
JIT/terminal completion. The fifteen installed seed files remain unchanged.
The [manual/image evidence](evidence/private-output-transfer-manual-image-20261109.json.gz)
retains complete linked products, guards and serial output. Its SHA-256 is
`a544887edd5bb1f8ecf6bf734e12a8d46cdcd247730f9dbd9bb89a1e873ff20d`.
The preceding complete image and kernel remain preserved locally. This manual
relink is separate from the earlier cold replacement-tool acceptance.
