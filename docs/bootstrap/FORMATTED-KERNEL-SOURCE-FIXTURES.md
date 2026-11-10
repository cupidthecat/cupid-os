# Formatted kernel source fixtures

Nine compiler and checked-seed methods now use the formatted source's exact
byte, line and SHA-256 locks. The old locks match the original Git blobs, so
their replacements come from a before/after source comparison.

The emitted object sizes remain unchanged. Complete objects for libm, string,
per-CPU state and SIMD remain byte-identical. Kernel entry and SMP use the
fingerprints established by the preceding
[ELF comparison and full rechecks](FORMATTED-KERNEL-FINGERPRINTS.md), which
account for relocation ordering and allocator source-line arguments.

The extracted `str_floor` fixture now follows balanced braces and pins its
44-token body, including the exact assembly strings and clobbers. The old and
formatted bodies have the same token digest. This replaces source line-position
and line-count assertions while retaining the exact 420-byte emitted object.
Changing the rounding mode, rounding instruction or memory clobber fails before
compilation. A separate source-byte mutation still rejects raw source drift.

All nine original methods pass on Windows and native Linux under their
unchanged individual bounds. Four actual-source mutation controls fail on
each host. AST review leaves all other methods and helpers unchanged and
removes no test. Production sources, compiler behavior, installed seeds and
normal-build ownership do not change.

The [fixture archive](evidence/formatted-kernel-source-fixtures-20261110.json.gz)
retains 32 payloads: before/after source, measured production objects,
before/after tests, source accounting, controls and passing logs. The original
tests normally retire their temporary outputs; the retained production objects
are bound to the preceding fingerprint archive. Every exported payload passes
independent size and SHA-256 readback. This archive has 574,108 bytes and SHA-256
`d5a79bf6bb057e41de261dc1043072656c708653c87ecde66e27e0cc0d6d0ccf`.

The remaining Doom profile locks and complete current repository replay stay
separate from these nine methods.
