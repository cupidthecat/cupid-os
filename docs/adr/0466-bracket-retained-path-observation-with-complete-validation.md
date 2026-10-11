# ADR 0466: Bracket retained path observation with complete validation

Complete input construction repeatedly walks the same retained ancestor graph
while capturing ordinary path observations. A serialized group brackets those
observations with complete validation, retaining each path's root metadata,
immediate parent and fresh no-follow leaf checks. This preserves custody while
allowing capture to share the full ancestor checks across the group.

Callback kinds remain provisional until final validation. Failure clears results
and poisons the observer; callbacks cannot reenter or close it, and transaction
borrowing and borrowed validation reject during capture. Temporary selection
observers retain their publication restriction. The ordinary single-path call
keeps its full ancestor checks.

The [component record](../bootstrap/NATIVE-PATH-OBSERVATION-GROUP.md) carries the
paired contracts and reproducible held patch. Complete input discovery remains
outside acceptance: both Windows UNC owner probes still exceed sixty seconds.
Normal source, installed seeds and the three Python coordinators retain their
ownership.
