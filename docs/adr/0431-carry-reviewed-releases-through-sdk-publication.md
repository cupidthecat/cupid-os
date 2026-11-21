# ADR 0431: Carry reviewed releases through SDK publication

The SDK can rebuild the exact tools from an already qualified release while
capturing a different publication inventory. Carry the explicit reviewed seed
behavior reuse from ADR 0418 through SDK build and cache reuse. The native
publication author keeps its own observed sources and all stage comparisons.

Build and ensure accept an optional behavior release. Make explicitly selects
the reviewed release and declares it as a build prerequisite. Capture it before
stage construction, retain the selected Linux seed observations, forward the
request through the private pending bootstrap, and recheck authority after
bootstrap, after behavior and before publication. The six published tools must
match the reviewed cohort. Cache reuse checks those same bytes and observations;
a rebuild receives the selected release. Calls without a release keep the
strict bootstrap path.

The captured SDK inventory includes the tools package initializer, release
coordinator, ABI behavior module and pin validator. The complete ISO producer
has 34 C sources and 35 bootstrap objects. Its SDK captures 101 inputs and the
native author compares all 74 pairs before the coordinator's independent
comparisons. Complete 97- and 101-input requests require captured seed context.
Historical 92- and 96-input producer profiles retain their exact inventories,
and extended classic requests fail. Source counts remain bound to their plans.

Recheck authority after output validation and preparation, immediately before
the first replacement. A check before validation would miss drift introduced
at that boundary. Rejected authority leaves the prior publication intact and
permits recovery after restoration. Standalone entry points put the repository
package first, keeping error types and authority imports consistent. WSL path
translation decodes its actual UTF-8 bytes independently of the Windows host's
default encoding.

Both hosts pass the 61-method native and Cupid-built policy suite, for 244
executions without skips. Paired independent SDK coordinator checks verify 192
executions, including captured-request forwarding, cache rejection, author and
output-validation drift, recovery and Unicode translation. All four actual
default and long SDK publications pass independent rereading on both hosts.
Each captures 101 inputs and retains all 74 raw stage pairs, 22 ELF artifacts
and its native-authored manifest. Stage-pair identities and ELF artifacts match
across all four runs; manifests match between hosts within each profile. The
original compilation deadlines and complete fixture selections remain intact.
The prior implementation and its failed publication receipts remain retained.
