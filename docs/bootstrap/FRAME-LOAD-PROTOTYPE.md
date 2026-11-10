# Frame-load emitter prototype

[ADR 0441](../adr/0441-fold-validated-frame-address-load-spans.md) records the
private optimization and its limits. The emitter first validates the ordinary
address and load, then folds one complete frame span. It preserves the memory
read, flags and stack result. Incoming branches to the second instruction
prohibit the fold.

The source fixtures and Python controls are
`toolchain/tests/cupidc_frame_load_runtime.cc` and
`tests/test_cupidc_frame_load.py`. The controls support explicit tool paths for
retained experiments; their default path builds the native development tools.
The original checked compiler fails the selection case, and the native
prototype passes all four cases on each host.

Retained records use `frame-load-emitter1-*` under the external proof root.
Both runtime executions pass and both generated caller objects match. The
compression benchmark improves within its own paired measurement. Full large
publication controls and compilation of the emitter by the current Cupid
seeds are still running.

Before adopting this compiler, qualify its complete paired source cohort,
update contract capture and ownership, reconcile code-shape assertions, run
the complete affected object and ABI controls, remeasure the source frontier
and run the normal OS build and strict boots. The accepted disk-foundation OS
uses the earlier qualified compiler and cannot validate this emitter.
