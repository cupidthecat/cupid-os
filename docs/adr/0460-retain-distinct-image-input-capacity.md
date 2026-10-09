# ADR 0460: Retain distinct image input capacity

## Status

Implemented in separate private source-five through source-seven copies on
2026-10-09. All four final builders and both complete native 64-method selections
pass. Ordinary/UNC observer regressions also pass all 100 executions and 106
calls. Four checked capacity cases fail on each host. Complete paired acceptance,
source qualification and normal command integration remain open.

## Context

ADR 0456 accepts 4,096 repeated stages, which does not prove 4,096 distinct files
or directories. Its observer's 4,096-entry quota includes ancestors, file leaves
and first absences. A direct metadata replay admits 4,088 distinct files and
rejects the next file. Changing only that quota admits all 4,096 under 32 MiB;
extra memory and descriptor allowance do not repair that quota failure.

The expanded native selection then exposes two separate problems. Nested
metadata and later streaming retain different handles. The direct full replay
reaches Linux's 10,240-descriptor limit after 2,038 streams and returns EMFILE.
At a reduced 64-descriptor budget, eighteen nested files pass and nineteen fail.
Changing only the full replay's descriptor budget confirms the cause. That
diagnostic does not widen acceptance limits.

Windows construction also revalidates the entire accumulated observer for each
new file. The 128-input work-count replay performs 12,060 complete-entry checks.
Removing only that prefix scan in an isolated diagnostic leaves 2,366 checks
and exact linear growth across 32, 64 and 128 inputs. The first diagnostic's
linear budget is too small for necessary temporary selections; its failed
receipt remains preserved. No diagnostic bypass is integrated.

## Decision

Add explicit retained-entry quotas to observer and path-selection construction.
Linked records allocate only when observed. Discovery budgets one root entry
plus `(stage_count + 4) * 4096`, covering its accepted 8,191-byte paths and first
absences. Ordinary constructors retain 4,096 entries; directory-membership and
metadata-batch bounds stay separate. Zero and SIZE_MAX reject before resolution
and clear the supplied owner.

Add streaming of an already retained regular-file record. Reopen the leaf through
its no-follow parent, compare full metadata and original ancestor bindings,
stream with before/after checks, preserve any earlier digest, and upgrade that
same record's handle only after validation. Failure poisons the observer and
clears a supplied result. Ordinary streaming retains its distinct-leaf behavior.
This operation grants no authority and supplies no whole-observer verdict.

Add separate comparisons of captured root/file identities. They may describe
historical records; they do not check metadata, payloads, absence or path bindings.
Invalid/unretained inputs poison supplied observers, and existing poison cannot
be bypassed. Ordinary root/file comparisons retain complete revalidation.
Discovery uses captured comparisons only during serialized construction, then
checks every retained observer and every original request before returning a
successful owner. Later checks and publication guards are never cached.

## Evidence and limits

The retained-stream replay makes the nineteen-file and complete 4,096-file
descriptor reproductions pass under their original limits. The complete direct
replay closes in 0.410 seconds. Source six passes all 56 native Linux methods.
Source seven builds through native and Cupid tools on both hosts. Both native
callers pass all 64 methods, including distinct present, missing and nested
capacity, restored-time edits, invalid quotas, old default bounds, retained-stream
failure/identity contracts and historical comparison controls.

An isolated construction hook changes the kernel, working-root alias or selected
parent alias after capture. Source six exposes a view before its later rejection;
source seven rejects all three changes before exposing a result. All six actual
runs retain the original sixty-second and 32 MiB limits. Hooks remain in the
clearly named debug tree, outside all 206 source/support controls.

Checked Windows rejects four full-capacity cases during allocation. The original
unchanged missing-stage replay passes at 3,607 rows and rejects at 3,608; the
complete 4,096-row failure and every minimization attempt remain held in
`input-discovery-checked-capacity-debug9`. This establishes a boundary, not its
allocation cause. Checked Linux
exceeds sixty seconds in the corresponding four cases, including the held final
payload edit. Neither failed full selection qualifies the new owner. Allocation
and remaining work require further diagnosis without reducing the accepted
4,096-input scope, deadlines or Linux memory bound. The native success does not
substitute for Cupid-built acceptance.

Evidence includes `input-discovery-source7`, its paired Linux copy,
`input-discovery-descriptor-{minimized,hypotheses}-linux5.json`,
`input-discovery-retained-stream-green-linux7.json`,
`input-discovery-performance-cause7.json`,
`input-discovery-construction-guards-linux8.json` and the complete original runtime
receipts under `C:/Users/admin/cp7`. All 99 normal producer inputs and fifteen
installed parent files remain unchanged. TempleOS stays read-only and excluded.
