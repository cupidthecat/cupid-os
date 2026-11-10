# ADR 0467: Group original file requests without weakening retained custody

Repeated original-request validation resolves the same parent twice for every
file. Group requests only when their original parent spellings match, retaining
fresh no-follow file checks, captured parent identities and both parent
resolutions. Different aliases keep separate groups even when they currently
resolve to one physical directory, so each original binding remains visible.

The group checks metadata. Its caller retains complete root and payload
validation before and after the operation; callback results stay provisional.
Same-size restored-time payload edits demonstrate this boundary: the bare group
can succeed while the complete caller frame rejects. Failure poisons the
observer, and serialization preserves closure, reentry and borrower lifetimes.
Reuse only directory information captured from the same resolution handle.

The [component record](../bootstrap/NATIVE-FILE-REQUEST-GROUP.md) carries the held
patch and complete evidence. The separate complete input owner still exceeds
sixty seconds on the Cupid-built Windows UNC caller. Normal source, installed
seeds and the three Python coordinators retain their ownership.
