# ADR 0469: Capture initial leaf kind and metadata from the same handle

Query an unknown leaf's newly opened handle once for its ordinary kind and
initial metadata. Pass that snapshot to retained observation creation instead
of querying the same handle again. Apply this to ordinary walking and grouped
path-kind capture. Preserve known-type checks and reject every unsupported
kind before retention; failed queries clear both outputs.

The initial snapshot does not replace subsequent validation. Fresh path
bindings, root and parent metadata, complete payload custody and provisional
callback outcomes retain their existing contracts. The public header and ABI
remain unchanged.

Qualify the host refactor through all existing contracts and complete generated
products. Keep the private complete input owner outside the held source cohort
while its Windows UNC capacity calls miss the original sixty-second deadline.
Native query counters describe specific call sites and retain their measurement
scope; elapsed values do not establish complete owner capacity or speed.

The [component record](../bootstrap/NATIVE-KIND-METADATA.md) contains complete
paired host acceptance and the preserved private-owner limits.
