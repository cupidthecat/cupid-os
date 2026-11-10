# Remaining formatting fixture repairs

Two Python checks still expected the source layout from before formatting.
Their repairs preserve the user compiler profile and complete bootstrap checks.
No production C source or emitted object changes in this step.

## User compilation tables

The user-profile check searched for a closing brace at the start of a line.
Clang-format keeps the short initializer on one line, so the reader continued
into later tables and collected 76 extra strings.

The check now reads balanced C blocks and token spelling. It still requires
exactly three source rows, each source's exact header closure, the declared
two-input count and the complete ordered compiler profile. Duplicate rows
cannot disappear into a dictionary comparison.

Both hosts accept whitespace and comments between tokens. Seven controls
reject a wrong source, header, input count or option, a missing or duplicate
row, and an extra option.

## Current producer snapshot

The full fixed-point test retained the preceding source digest after the
114-input producer changed. Independent readback reconstructs the old inventory
from Git and compares it with the qualified current input set. Their paths and
counts agree; their exact source bytes have different digests.

The current lock is
`8f570ec78b0e81c2bb31b6a0774d3726b0f972d88cdb0556f83437050adbd36e`.
A fast check verifies all 114 inputs before an expensive bootstrap. A negative
check changes one source byte stream and requires rejection.

The original full method keeps its 6,000-second bound and every other AST node.
The older complete Make replay successfully ran the bootstrap, then failed on
the stale digest assertion. Its later assertions were not reached. A corrected
full replay is required and remains in progress.

## Verification and retained evidence

All 28 selected seed-validation methods and the complete 16-test user compilation
class pass on each host, without skips. These runs use the qualified private
seed pair and execute native and Cupid-built coordinators. Linux takes 441.753
seconds; Windows takes 971.738 seconds. Input guards remain unchanged.

The older frozen Make run ends with 3,705 tests, 28 failures, four errors and
75 skips. Its complete log remains retained; these focused checks do not turn
that failed command into a passing result or establish full repository acceptance.

The [archive](evidence/remaining-format-fixtures-20261110.json.gz) retains the
before/after fixtures, complete focused logs, seven mutation controls, Git
inventory comparison, AST review and older full-run report. All 22 payloads
pass byte readback. Its 579,748 bytes have SHA-256
`615f25a5ee4fe16927730a1e191df0accfbd53ccfe4854a435975db26722a05a`.
