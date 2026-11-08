# Windows observer worker profile

The qualified source owner needs CreateThread and the callback adapter in
CupidBuild. Its producer profile must carry those inputs without invalidating
the installed historical seeds. ADR 0478 establishes an opt-in profile before
changing the current producer entry points.

`_windows_build_plan(..., observer_workers=True)` adds the production
`toolchain/hosted/i386-windows/observer_thread.asm` row, one CupidBuild link
entry and one KERNEL32 CreateThread import. The other tools retain their
existing imports and link order. All five ANSI/UTF-8, long-path and user-link
combinations have separate worker and legacy classifications. The adapter
bytes match the already qualified private candidate.

`_windows_plan_configuration` returns all four selections. Stage construction
uses it for link arguments and exact PE import validation. The three-field
`_windows_plan_profile` wrapper preserves existing callers. Source capture and
freeze accept `windows_observer_workers`, save it in `SourceInputs`, and apply
it during both live and frozen closure checks. Prepared-stage verification and
behavior-seed retargeting retain the complete selection and parent identity.

The profile tests reject missing or repeated adapter rows, a different adapter
path, missing or repeated link entries, foreign tool linkage/imports, a changed
import slot, unpaired bridge/import selections, reserved Linux source names and
non-Boolean selections. Live and frozen adapter mutation, missing input before
namespace creation and selection loss during revalidation are covered.

The profile and focused seed runs found eight stale live-inventory assertions
from the earlier addition of `cupid_runtime.h`. Those assertions now include
that header; historical installed digest/count pins remain unchanged. All 53
profile methods pass after the correction. The focused release and seed suite
passes fifty methods with two existing platform skips. Four ownership methods
pass. The original callback and concurrent-heap contracts assemble the
production adapter: all six selected ordinary/UTF-8 Windows methods pass in
90.385 seconds. The old fixture is moved to the production path; there is one
adapter source.

A direct comparison with `acd5f73f` checks ten legacy plans, all sixty tool
link roles, profile classification and complete source inventories. Every
default result is identical. The adapter SHA-256 remains
`b5af6dd3f69cf986d94bb28448eba2cbfa7b49d21e9044432eb1640ed48c1a97`.
The regenerated graph and its check pass, retaining 787 active inputs,
83 unreachable sources and 452 transforms. The
[evidence archive](evidence/observer-worker-profile-20261108.json.gz) retains
the source, reference comparison, audit, final test logs and earlier failures.

An inadvertently selected full seed test module started a complete compiler
fixed point. That run was stopped while compiling stage two and is not counted
as acceptance. Its three temporary directories remain after automatic cleanup
review reported that deletion was blocked by policy. The full producer rebuild
belongs after source-owner adoption. Earlier audit attempts exposed stale
validator fragment expectations; the guards now require the typed profile in
linking, PE validation and behavior capture.

This profile is available but is not selected by the current producer drivers.
Source-owner module adoption, complete named producer and replacement consumer
qualification, wide publication stores and the normal image handoff remain
open. The normal graph remains 449 CupidBuild transforms and three Python
coordinators. Installed seed files remain unchanged.
