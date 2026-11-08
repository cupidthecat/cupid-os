# Private derived integer updates

Private CupidC supports prefix and postfix increment/decrement through integer
pointers, array elements and record fields. Byte, halfword and word stores
preserve neighboring bytes and evaluate the destination once. Postfix returns
the old value; prefix returns the normalized stored value. The existing
Feature 23 demo checks widths, neighboring storage and index side effects.

The shared update path keeps an integer destination in EDX and its value in
EAX. Floating and SIMD updates keep their existing calling convention.
[ADR 0475](../adr/0475-update-private-derived-integer-lvalues.md) records the
implementation and diagnostic boundary.

## Passing qualification

- Windows and Linux each pass all 528 compiler/smoke methods, in 84.326 and
  8.407 seconds. Both checked compilers emit the same 491,708-byte parser
  object, SHA-256 `a48592368409a8c4841c7d55c30400f49d0a79c1b69d3f5b83eb48543a0eaac8`.
- Windows passes the complete forty-method source module in 2,274.829 seconds.
  The original Linux source method and its capture/cleanup wrapper pass in
  1,915.835 seconds, below the unchanged 2,340-second compiler bound. All 511
  captured inputs retain their bytes; its temporary workspace is removed.
- Readback checks all 624 retained objects: two emissions of 156 sources on
  each host. Every pair matches the producer manifests and normal production
  objects. Each cohort contains 4,253,360 bytes.
- Ordinary `make -j2 all` passes in 2,733.856 seconds. All 428 complete objects,
  322 headers and five linked/generated outputs match the independently
  measured layout in Make link order. All sixteen exact artifact rows pass;
  all seventeen artifact-policy methods pass.
- The raw kernel measures 9,412,596 bytes, first-pass ELF 9,508,496 bytes and
  final ELF 9,639,568 bytes. Only the raw-kernel and first-pass size rows change.
- A four-CPU max/e1000 guest passes ordinary `ls`, all 62 crypto checks and the
  installed Feature 23 demo through JIT and external AOT in 111.525 seconds.
  Both executions pass integer-update, pointer-difference and overall markers;
  the AOT process exits cleanly and the terminal survives. The source image
  and all three checked source inputs stay unchanged. The 209,715,200-byte
  image has SHA-256 `12b425c897a7e05e6ee0f069acfc781a1d9b6255ad49a5a8e587808fdd06d5b8`.
- Source-audit regeneration and deterministic checking pass.

The [portable evidence](evidence/private-integer-updates-20261108.json.gz)
retains raw logs, receipts, input identities, complete object digests, producer
manifests and failed attempts.

## Review findings and limits

Review caught pointer array slots entering the integer path and a prior const
condition supplying the wrong qualification to a record-field update. Positive
and negative regressions cover both. Two obsolete rejection fixtures now reject
computed integer rvalues. Const destinations, incomplete rows and unsupported
pointer-slot updates retain diagnostics; signed overflow gains no guarantee.

The first integration stopped on an invalid Python encoding name after copying
the parser; its obsolete-fixture test failure remains recorded. Two candidate
comment encoding errors were restored before adoption. The first layout used
capture-dictionary order, which can differ from Make after deferred reads. That
layout is explicitly rejected; only the corrected order supplies size evidence.

Normal ownership remains 449 CupidBuild and three Python actions across 452
transforms. No hosted seed promotion or production owner transfer is claimed.
Native image/SDK coordination, full Doom runtime acceptance and a complete
inside-guest OS build remain separate work. TempleOS stays excluded.

## Test layout follow-up

The execution cases now live in topic modules under `tests/private_cupidc/`;
the original ABI module retains its class and test selectors. Both hosts pass
the same 528-method selection after the split. All test and helper method ASTs
are unchanged. The [separate layout evidence](evidence/private-compiler-test-layout-20261108.json.gz)
records this refactor; the capability evidence above retains its original inputs.
