# ADR 0381: Let guarded callers own output publication

Date: 2026-08-31

Status: Accepted

## Context

CupidBuild creates and retains private candidates before it launches a checked
tool. That retained identity is the authority used for validation, rollback,
and final publication. Standalone CupidASM also has a recoverable publisher,
but its rename replaces the candidate identity. Nesting both publishers made a
successful assembly look like outside interference to CupidBuild.

The native Windows transition exposed a second generation boundary. Frozen
inputs must remain readable by the promoted tools, while candidates and
captured outputs need the broader sharing used by the source-current runtime.
Treating both roles as one handle profile either blocked the older reader or
weakened the frozen-input boundary.

The source-current Windows image also has a new native plan digest because
CupidBuild's runtime and startup closure grew. During the paired promotion,
CupidBuild and the artifact-size verifier must accept the active plan and its
immediate replacement.

## Decision

Hosted CupidASM accepts `--caller-owned-output` once in the normal option
order. In this mode it writes the selected object, raw image, and optional map
directly to the paths supplied by its caller. It does not create
`.cupid-as-*` publication state. CupidBuild uses this mode for guarded
assembly because it already owns the candidate identity. Standalone CupidASM
keeps its recoverable publisher when the flag is absent. A duplicate flag is
a usage error and does not change an existing output. Direct raw output and
its map must also name distinct file identities. Lexically equivalent paths
and Windows case aliases fail before truncation even when neither path exists.
Existing hard-linked paths fail the same check by file identity.
Matching leaf names beneath aliased existing parent directories also fail
before either output is created.

Windows keeps frozen inputs under a separate handle profile from mutable
candidates. The retained frozen handle allows read sharing, so the promoted
tools can open the input, while write and delete opens remain blocked. During
cleanup, CupidBuild verifies the retained handle and exact named snapshot,
closes the read-retained handle, reopens the same identity with delete access,
and verifies the full snapshot again before disposition. A replacement
identity is preserved, and cleanup fails closed.

The promoted and source-current native Windows plan digests form a temporary,
exact transition window:

| Plan | SHA-256 |
| --- | --- |
| Active promoted plan | `f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14` |
| Source-current plan | `c27481d2c532486648a1170a8a44b3b0020cea1460408f5606f340fb86976ed3` |

CupidBuild and the Cupid-built artifact-size verifier reject every other
value. The parent-generation window from ADR 0380 is unchanged.

## Evidence

The hosted CupidASM module completes 46 cases on Windows, with four procfs
cases skipped. The same 46-case module completes on Linux with four
Windows-only cases skipped. Positive cases cover direct ELF32 and raw-plus-map
output, and a native handle test proves that the caller-retained identity
survives. The duplicate-option, absent lexical-alias, case-alias, and hard-link
cases fail before mutation. The dedicated Windows CupidBuild process module
passes the exact handle allowlist, directory-record race, frozen-input sharing,
retained cleanup authority, and discovery-handle cases. Four native race cases
also prove that retained output, parent, and lock handles block post-open
mutation.

POSIX cleanup now propagates directory and stream descriptor close failures
instead of discarding them. An indexed native fault seam closes each selected
descriptor, injects the failed result, and proves that retained-input, stdout,
and stderr close failures all report incomplete cleanup without leaving a
private runner root.

A paired v4 reconstruction from the current working tree converged on both
hosts. Linux matched 22 C objects, one startup object, and all six stage-three
and stage-four tool images, then passed 31 failure, seven help, and 37 success
groups. Native Windows matched 23 C objects, three assembly objects, and all
six tool images, then passed 19 failure, seven help, and 24 success groups.
Both reports bind source snapshot
`179037ef27947e26406df8dc9f693bf77673a0e5c527e985210c917b82905469`.

The Linux report is 51,575 bytes with SHA-256
`64f60e47d96310bd94108cbf2c0a554febb117d62ee0c9b586174dfe6bb53cb7`.
The Windows report is 66,572 bytes with SHA-256
`451c9aba1152583a95db1d4a8fe67e95bff2f286af9d3cd91062b8d91b62f2ab`.
This is implementation evidence, not a promotion record: the source was not
commit-pinned, the checked seed directories did not change, and a clean
rebuild from the source commit remains required before manifest generation.

## Alternatives considered

Closing CupidBuild's candidate handle while standalone CupidASM publishes was
rejected. The caller would lose the identity it is supposed to validate, and
a same-user replacement could occupy the name before the caller reopened it.

Removing standalone CupidASM recovery was rejected. Direct output is an
explicit enclosing-transaction mode; ordinary command use still needs
recoverable publication.

Giving frozen inputs the mutable-output share profile was rejected. Frozen
bytes are evidence, not an output channel, and must reject writes for the
duration of the transaction.

Accepting any well-formed Windows plan digest was rejected. Both accepted
plans are reviewed bootstrap boundaries, not descriptive metadata.

## Consequences

One layer owns publication for every guarded assembly. Current CupidBuild and
CupidASM can preserve one candidate identity from creation through inspection
and commit on Linux and Windows. The active promoted Windows tools still
predate this protocol, so mixed-generation execution remains a bounded seed
transition until the clean paired promotion lands.

No build owner changes in this decision, so no `.c` file is renamed.
`TempleOS/` remains read-only reference material.
