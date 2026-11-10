# ADR 0465: Bracket retained file group capture with complete validation

Repeated retained-file capture spends substantial host work rechecking the same
ancestor graph. Add a serialized group API for already retained regular files.
Complete observer validation brackets the group; each member retains its
immediate parent, no-follow leaf, metadata and payload checks. Existing
single-file streaming keeps its full ancestor checks. Original-request binding
also reuses the final directory metadata already validated by each resolution.

Callback bytes remain provisional until complete group validation succeeds.
Failure poisons the observer and clears all supplied results. Callback
revalidation, closure and recursive capture reject. New transaction borrowing
rejects during capture, and existing borrowers cannot validate then. Owners
must outlive their borrowers. Temporary selection observers retain their
publication restriction; group capture grants no publication authority.

The [implementation and evidence](../bootstrap/NATIVE-RETAINED-STREAM-GROUP.md)
record 144 new cases, all 2,916 preceding API outcomes, both complete repository
replays and 87 strict checks. Independent review checks whole products,
complete streams and twenty whole object pairs. Forty altered-evidence controls
and seven malformed-patch or invalid-output preflights reject.

The complete native input owner remains outside this patch. Its fresh flat
4,096-stage probe passes on both Linux and Windows drive callers, but both UNC
callers exceed sixty seconds. Normal source, installed seeds and the three
Python coordinators retain their ownership.
