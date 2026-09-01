# ADR 0308: Bind the SMP trampoline to CupidASM raw layout metadata

## Status

Accepted on 2026-08-21.

## Context

The guarded SMP trampoline transaction assembled a private image with
CupidASM, then repeated its mixed code and data boundaries as manual CupidDis
arguments. CupidASM already knew those boundaries and could publish them as a
`cupid.raw-map.v1` file. Keeping a second offset list at the production handoff
left the assembler's source-derived layout unused.

The generated map cannot approve itself. The trampoline layout is part of the
kernel's AP startup contract, so a source edit that moves a boundary needs an
intentional policy update even when CupidASM reports the new layout correctly.

## Decision

The SMP transaction asks the checked CupidASM seed for a private range map with
`--map`. Hostbuild requires this exact canonical policy:

```text
cupid.raw-map.v1
size 4096
base 0x00008000
range 0x00000000 code16
range 0x0000001f data
range 0x00000210 code32
range 0x00000254 data
```

A missing, empty, malformed, or different map stops the transaction before
inspection. Hostbuild pins the accepted map, runs CupidDis with `--raw
--range-map`, `--require-known`, and `--require-local-targets`, then checks that
the map and image still match their captured hashes. Only the 4,096-byte binary
may replace the public trampoline. The map stays inside the private transaction
root and is removed with that root.

## Evidence

The first transaction test failed because hostbuild still invoked CupidASM
without `--map` and sent manual mode, base, and range arguments to CupidDis.
After the handoff changed, the test passed twice through the public command and
found no leaked map or private root.

Negative coverage rejects a missing map, an empty map, an unsupported schema,
a moved range boundary, map drift during CupidDis, strict disassembly failure,
candidate and input drift, output replacement, and unexpected tool output.
Each case preserves the existing trampoline. The active-source test assembles
the real source twice, obtains identical canonical maps, checks all four local
targets, and still rejects the known mid-instruction mutation.

The focused hostbuild, active-source, and CupidDis group passed 40 tests in
10.215 seconds with one platform skip. A forced production trampoline build
passed through the checked native Windows seed. It produced 4,096 bytes with
SHA-256
`b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90`
and left no private root or map beside the output. The broader CupidASM and
guarded object group passed 24 tests in 3.004 seconds. Deterministic bootstrap
audit check mode also passed against the three supported build roots.

## Rejected alternatives

Keeping manual CupidDis ranges would preserve two independent descriptions of
one source layout.

Accepting any valid source-derived map would let an accidental boundary move
become production policy without review.

Publishing the map beside the trampoline would turn transaction evidence into
an OS artifact without a runtime consumer.

## Consequences

CupidASM now supplies the production SMP layout metadata that CupidDis
consumes. The caller still owns the exact layout policy. The trampoline bytes,
build owner counts, ABI, checked seeds, and host dependency inventory do not
change. Host Python still owns the transaction, locking, drift checks, and
atomic publication. No source qualifies for a `.c` to `.cc` rename, and the
TempleOS reference tree remains untouched.
