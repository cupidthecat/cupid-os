# Static rotate-call prototype

The emitter recognizes calls to local pure unsigned rotate helpers after
ordinary call validation. It evaluates both arguments, consumes them in their
source order and emits one shared-encoder ROR or ROL. External and indirect
calls, `noinline` functions and qualified helper bodies keep ordinary emission.
See [ADR 0442](../adr/0442-fold-calls-to-proven-static-rotate-helpers.md).

The source fixture is `toolchain/tests/cupidc_rotate_call_runtime.cc`; Python
controls are `tests/test_cupidc_rotate_calls.py`. They share tool setup with
the frame-load controls without importing a concrete test class. Both hosts
pass all eight retained native frame and static-call tests. The fixture checks
six values across every defined count, surrounding live stack values, both
argument orders, actual i386 execution and two argument-side-effect counters.

Retained evidence uses `rotate-call-emitter1-*` under the external proof root.
Current Cupid compilation, the two-generation emitter fixed point, complete
large-publication controls and independent rereading still require their own
results. The previous frame prototype passes the Windows extent suite but
times out on Linux. Its broader object controls retain 35 failures out of 58
per host, including changed inventories, fingerprints and offsets. Those
controls must pass with reviewed expectations before a complete new compiler
cohort can qualify. The accepted disk-foundation OS uses the earlier compiler.
