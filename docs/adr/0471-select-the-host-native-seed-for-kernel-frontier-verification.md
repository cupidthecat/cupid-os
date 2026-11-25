# ADR 0471: Select the host-native seed for kernel frontier verification

Date: 2026-10-12

Status: Accepted

The normal Windows build already uses the checked PE32 compiler. The kernel
frontier verifier still selected the Linux compiler, copied it into WSL and
started a WSL process for each compilation. Its complete Windows replay failed
at `kernel/audio/memio.cc` because the staged compiler path no longer existed.
The saved diagnostic establishes the missing path, but not its cause.

Select the checked Windows manifest by default on Windows and the checked Linux
manifest on POSIX. Freeze and verify the selected cohort before execution. A
Windows compiler executes directly from that private capture on Windows; a
POSIX host rejects it. Invalid native seed verification fails immediately.
There is no fallback to another seed or execution environment.

The optional `--seed-manifest` selects an explicit checked cohort. An explicit
Linux manifest on Windows keeps the existing WSL staging, exact private-directory
validation and cleanup. The selector cannot be combined with `--compiler`,
`--compiler-host-path`, `--runner` or `--runner-arg`, which belong to the separate
unchecked execution route.

Keep the complete 156-source frontier, two object emissions per source, frozen
input closure, strict ELF validation, publication checks and original
2,340-second frontier deadline. Host selection changes execution ownership; it
does not change the approved source set or its expected products. This follows
[ADR 0359](0359-remove-the-windows-fixed-point-cupidc-bridge.md).

Three new controls fail against the preceding implementation before the change.
The adapter, selector, rejection and private-directory selection passes all ten
focused controls on each host. Four new methods cover native Windows capture,
foreign-host rejection, invalid-seed failure and incompatible selector options.
The complete forty-method Windows replay compiles all 156 sources and passes
the expected objects and total, then fails a stale complete input-map digest.
Independent capture measures the current 478-input map and a candidate changes
only that expected digest. The Linux run reaches the unchanged 2,340-second
deadline before completion. Both failed runs are retained. After both terminate,
the measured digest repair is adopted. Fresh complete runs use native Windows
execution and a byte-identical Linux copy on native Linux storage; all 2,237
copied files are checked. Both complete forty-method modules pass: 1,706.170
runner seconds on Windows and 1,531.569 on Linux. Each runs all 156 sources,
duplicate emissions, exact object and input-map checks, strict ELF validation
and compiler provenance. The source set, verification boundaries and deadline
stay unchanged. The [complete frontier evidence index](../bootstrap/evidence/verification-complete-native-frontier-20261012.json.gz)
records the source captures, recipes, original logs and terminal receipts.
