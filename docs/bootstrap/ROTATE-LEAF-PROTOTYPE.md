# Unsigned rotate leaf prototype

The private emitter change is recorded in [ADR 0440](../adr/0440-fold-pure-unsigned-rotate-leaves.md).
It leaves the SHA source and transaction guards intact. The existing checked
compilers build the emitter on both hosts and produce the same 656,136-byte
object, SHA-256 `cb847cfb1ec28eef853882a5bacce4497565a0332a1b81462c33c5d2cf56caca`. A native emitter prototype produces the candidate objects used
for the first runtime and compression experiments.

Retained evidence lives outside the repository in
`cupid-native-iso-proof-20261005`. The compression control is
`rotate-emitter1-hash-benchmark2-linux`. The final native object controls are
version four. Earlier versions retain failed harness expectations. The full
publication experiments are `rotate-emitter1-candidate-extent-checked-*` and
remain in progress. These records do not authorize seed installation.

Before committing this capability, complete both native object controls and
runtime fixtures, run the emitter through Cupid compilation, check the new
compiler's output and fixed point, update source ownership and contract
capture, and run the relevant frontier and OS controls. Preserve the first
large publication failures and compare later results under the same bounds.
