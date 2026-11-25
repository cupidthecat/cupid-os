# ADR 0468: Group temporary source selection under its existing custody rules

Capture present source paths together through a temporary selection observer.
Require that observer profile explicitly and validate its complete selection
before and after the group. Source-root identity follows the optional selection
contract, which permits uncaptured sibling changes; descendant and leaf checks
retain their existing rules. The ordinary grouped path API continues to require
full root metadata.

Temporary selectors permanently reject publication borrowing. A complete input
owner must compare the selected parent and file with retained observations and
keep complete root, original-request and payload custody at both boundaries.
Callback kinds remain provisional until the group succeeds. This separates
shared selection work from the authority required to publish an image.

The [component record](../bootstrap/NATIVE-SOURCE-SELECTION-GROUP.md) carries the
held patch, complete host replays and preserved failures. The private complete
input owner remains outside component acceptance and normal image publication
remains Python-owned.
