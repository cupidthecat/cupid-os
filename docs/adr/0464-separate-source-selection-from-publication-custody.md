# ADR 0464: Separate source selection from publication custody

Repeated file selection need not rebuild a complete physical ancestor chain
when a complete input owner already retains it. Add a temporary selection
observer that retains the original parent and leaf but permanently rejects
transaction borrowing. Require its captured parent and file identities to
match complete retained observations, with complete root and payload validation
before and after use. This preserves publication custody while allowing shared
input discovery to avoid duplicate ancestor work.

Cached identity comparisons establish historical equality only. Direct
original-request checks reselect the parent twice and check the regular leaf
without adding observations. Indexed lookup keeps every older observation on
the owning list, so stale duplicates still reject complete validation. Checked
Windows paths use existing wide calls while retaining normalized physical names;
the opened-name flag diagnostic changes that contract and remains rejected.

The [implementation and evidence](../bootstrap/NATIVE-SOURCE-SELECTION.md)
record both complete repository replays, 862 new API calls, all 2,054 preceding
outcomes and 83 strict checks. Independent whole-product and runtime review
and twenty verifier rejection controls pass.
The complete private input owner remains outside this held patch: its two final
4,096-input UNC phase processes exceed sixty seconds. Normal source, installed
seeds and the three Python coordinators retain their ownership.
