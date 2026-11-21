# Check each frozen set once per live-input call

The current private wide adapter applies ADR 0459's validation rule: one call
to `cupidbuild_host_require_inputs` checks every ordinary live input, then
checks the complete frozen set once if a previous-output or private-output row
requires it. Each later call performs its own checks. Publication, candidate,
output and cleanup boundaries retain their separate validation.

This changes only that function. It removes repeated complete-set reads when
both qualifying rows are present. It introduces no cross-call cache and removes
no registered input or payload digest check.

## Counted checks

Separate instrumented native and Cupid-built callers run 64 cases across both
hosts. They use real previous-output capture, private-output writing and
transfer, and ordinary input freezing. Removing the counter reproduces each
clean source exactly.

The original code produces twelve expected diagnostic failures. With both
qualifying rows, one call checks the same set twice and two calls check it four
times. The corrected code performs one and two checks respectively. Calls with
neither row perform no frozen-set walk through this entry point; calls with
either single row still perform one.

Live-input drift still rejects before frozen validation. A mismatch injected
into the accepted frozen digest and a failed previous-capture state also reject.
The digest test changes the expected snapshot, not the retained payload.
Every case preserves the previous public bytes, identity and modification time
and closes its publication namespace.

The first runner expected only one original frozen pass before the live-drift
rejection. Both qualifying rows precede that ordinary input, so the original
actually performs two. Correcting that observer expectation leaves the caller
and implementation unchanged. The failed attempt remains retained.

## Clean caller checks and limits

Eighty clean constructor cases and twenty complete 200 MiB staged-image cases
pass across native/Cupid-built Windows and Linux. Complete image hashes and all
3,200 blocks per case match the unchanged v57 oracles. The qualified disk and
FAT core objects, normal two-addition Windows bridge and existing store modules
remain unchanged. No timing ratio is claimed; the exact reduction in counted
checks establishes the work removed.

The current image contract retains its existing 18,000-second caller and
60-second producer limits. Small cases retain 120 seconds; Linux retains
32 MiB and 10,240 descriptors. The older ADR 0459 handoff's failed 600-second
case remains a separate failed result.

The image oracles retain the earlier 9,416,096-byte kernel. Current OS bytes,
all 114 producer inputs, installed seeds and normal ownership counts are
unchanged. External observer capture, active integration, producer qualification
and the three Python coordinator replacements remain open.

[Evidence archive](evidence/private-coalesced-wide-input-validation-20261109.json.gz) SHA-256:
`d1ad738b2d9caaa9044e3eb71305500e651e48c3a2449241c9079dc82184ceff`.
