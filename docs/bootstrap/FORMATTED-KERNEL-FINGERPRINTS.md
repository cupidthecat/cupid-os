# Kernel fingerprints after formatting

The original complete kernel frontier method passes on both hosts with its
unchanged 2,340-second deadline: 2,019.699 seconds on Windows and 2,042.319 on
native Linux. Each run compiles all 156 sources twice and compares 4,253,532
object bytes per pass. Source and installed-seed guards remain unchanged.

The test keeps 60 pinned object rows and every preceding size assertion.
Formatting changes 36 complete fingerprints; the other 24 whole objects stay
byte-identical. Independent before/after ELF comparison matches function sets,
relocations by target and data sections. After relocation normalization, the
only remaining function-byte differences are 65 immediate values passed to
`kmalloc_debug`, matching the old/new source lines of real `kmalloc` calls.
The allocator macro intentionally passes `__FILE__` and `__LINE__`.

The exact input snapshot grows from 486 to 487 through
`toolchain/tests/cupidc_source_spelling_fixture.h`. The test now explicitly
requires that header and the measured snapshot SHA-256
`9c154131801414956302df6e8b0e330e2a3116b97f9c5ebbd261403f8b58fd8a`.
AST readback confirms that restoring the changed constants and removing this
new presence check reproduces the preceding complete test module. No source
cohort, object size, aggregate, timeout or existing assertion is removed.

The first full rechecks retain their failures: the initial calibration covers
only the first 23 pinned rows and later reaches the stale source-driven group.
The passing rechecks cover all 60 rows and the complete snapshot. The missing
old keyboard object is recovered by directly compiling the original source
with the same checked compiler and header closure before comparing it with
the formatted source.

The [fingerprint archive](evidence/formatted-kernel-fingerprints-20261110.json.gz)
retains 706 file records and 647 payloads, including all pinned before/after
objects, the 487 current inputs, comparison scripts, failed and passing logs,
controllers and corrected test. Every payload passes independent size and
SHA-256 readback. The archive has 3,886,901 bytes and SHA-256
`b007380720ff846e2f51a2981187d60b2fe792a74251add04c2303e69fa4f6f2`.

This calibrates tests against the formatted source; it adds no compiler
behavior or production-source change. Complete current suites, long-path SDK
profiles, cold replacement OS/user builds and guest acceptance remain separate.
