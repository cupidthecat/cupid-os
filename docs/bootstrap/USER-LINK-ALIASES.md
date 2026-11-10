# Native user-link alias checkpoint

The qualified alias/CLI source starts from accepted physical source
`77a114a0` and retains its calibrated policy and source documentation. Installed seeds remain the
`78e71bd6` release. The normal three user links still use Python.

`cupidbuild_host_output_parent_resolve_existing` opens the existing repository
and both existing parent directories while allowing directory aliases. It
derives physical names from those handles, checks repository containment at a
component boundary, requires the two parents to share one physical identity,
and rejects missing input files, linked leaves and nonregular leaves. An absent
output is valid. Resolution creates no directories, files or locks.

Before releasing the resolution handles, it reopens the physical directory
chain without following links and compares its root and final-parent identities
with the retained resolution handles. It returns the physical root and relative
source/output names with that chain. Windows prevents replacement of retained
directories. Linux rechecks ancestor identities and parent/name bindings.
Retargeting an original alias does not retarget the held physical chain.

`cupidbuild_link_user` uses those names to approve the `cat`, `hello` or `ls`
object/output pair. It borrows the resolved chain through the existing guarded
link transaction and closes it after transaction cleanup. The strict
`cupidbuild_link_user_object` entry point keeps its absolute physical-root
contract. Both operations share object capture, six-tool freezing, checked
CupidLD execution, user-loader validation, checked CupidDis inspection,
publication and rollback. Equal validated output preserves its timestamp.

## Host and checked callers

Windows derives the normalized DOS path through
[`GetFinalPathNameByHandleW`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew).
The resolver currently accepts its drive-rooted result. The shared codec
converts the returned UTF-16 name to bounded UTF-8. The new
`final_path_start.asm` bridge preserves the cdecl/Win32 calling conventions.
The checked test caller adds that object and one explicit KERNEL32 import.
The selected bootstrap profiles bind the same bridge and API to CupidBuild.

Linux obtains the physical name from `/proc/self/fd` for each held directory
descriptor. The checked i386 path uses the existing syscall adapter and
`readlink` syscall 85. Both paths compare retained identities before returning
the preparation. Paths longer than 600 characters with accented and emoji
components pass in all four caller configurations.

All twelve resolver methods pass in native and Cupid-built callers on both
hosts. They cover relative, absolute and lexical spellings, independent
internal parent aliases, repository and ancestor aliases, prefix siblings,
escaped and missing parents, distinct physical parents, linked/nonregular
leaves, absent output, alias retargeting and physical-parent replacement.

The combined operation suites pass 32 methods in each configuration: fourteen
strict physical methods and eighteen alias-aware methods. Native Windows takes
163.836 seconds and native Linux 145.219. The Cupid-built Windows caller takes
874.444 seconds and the Linux caller 650.866. Each checked selection skips six
mutation-hook subcases; both native selections cover them. The suites retain
all three real-program byte comparisons, timestamp preservation, malformed
objects, loader limits, seed drift, foreign locks, hard links, instruction and
target inspection, pre-launch mutation and post-install rollback.

Evidence is retained under
`build/bootstrap/native-profile-validation-258bb5f3/` in the bootstrap checkout.
`user-link-alias-independent-verification-v1.json` binds the four twelve-method
resolver results at their original checkpoint.
`user-link-alias-operation-independent-verification-v1.json` independently
rereads all four completed 32-method records, their logs and the same twelve
current source/test inputs. `user-link-alias-inputs-v1.json` captures the full
1,552-input operation checkpoint before this documentation update.

The first Windows resolver gate fails to link the missing implementation.
Later harness failures expose system-code-page decoding of UTF-8 output,
an unconditional native `wchar.h` include, an unavailable `getchar` declaration,
and Windows text-pipe newline conversion. The harness now decodes UTF-8
explicitly, confines the native wide-argv include to that caller, and resumes
with one ASCII byte through `fread`. Failed records remain separate from the
completed passing runs.

## Exact bootstrap profiles

Current-source bootstrap and publication commands include the alias bridge by
default. `--no-windows-user-link-aliases` selects the historical profile for older
source captures. The `--windows-user-link-aliases` selection reaches both public
bootstrap commands, the manifest-author bootstrap, publication build/reuse,
source freezing, behavior retargeting and the independent publication verifier.
Combining it with `--windows-long-paths` selects the larger profile. Historical
plans keep their recorded identities.

| Selected profile | Producer inputs | C / assembly objects | Fixed-point pairs | Native plan SHA-256 |
| --- | ---: | ---: | ---: | --- |
| Directory aliases | 77 | 32 / 4 | 42 | `79241fcdd8784952cf9e1e74907ac817dc83e24429c5625d3424a889c2753d70` |
| Directory aliases and long files | 78 | 32 / 5 | 43 | `2dc92702e1e6e823b0c43fd48427d66bd021563925fe2b8b418451206768f8ff` |

Each selected plan requires one `final_path_start` object in the CupidBuild
link and one `GetFinalPathNameByHandleW` import in its KERNEL32 table. Other
roles retain their exact tables. Missing or duplicate bridge bindings, unknown
imports, role mismatches and mixed profiles fail validation. The historical
77-input long-file profile and the new 77-input alias profile share a count;
their exact plan digests distinguish them.

The shared C manifest reader reports PE profiles 4 and 5 for these plans.
Both require the complete `78e71bd6` parent release tuple. The independent C
artifact policy checks the count, Windows plan and both parent roles, together
with the paired Linux manifest. Linux count 77 still describes the historical
long-file inventory as well; the paired Windows plan identifies the selection.
Linux count 78 requires the current parent. The C PE validator enforces the
new exact imports without weakening earlier profiles.

The paired profile gate passes 156 methods on each host. It includes the eight
new plan tests, eight publication-boundary tests, shared manifest and independent
artifact-policy tests, and C validation of all installed seed images and the new
PE import profiles. `user-link-alias-profile-independent-verification-v2.json`
rereads both completed records, logs and their fourteen source/test inputs.

Earlier gates retain their failures: two stale tuple/installed-profile test
expectations, a negative policy case that actually selected the complete
historical 77-input long-file profile, default alias behavior retargeting that
kept the older parent, and installed PE tests that assumed the default UTF-8
profile. The corrected cases preserve historical acceptance and require the
selected new profile's exact lineage and imports.

The build audit now binds the new Windows declaration and selected profile
through native linking, behavior capture and publication recapture. Its first
three refresh attempts fail on old source fragments and AST expectations; the
fourth refresh passes all contracts. The wider Python gate and refreshed
preprocessor occurrence fixtures remain under validation.

## Earlier release boundary

Installed seeds remain the `78e71bd6` release. New staged fixed-point and behavior
evidence is still required before seed carriage. UNC roots remain unsupported
by the resolver and need explicit implementation and coverage. CLI exposure of
the user-link operation, seed promotion and Make adoption also remain open.
This checkpoint changes no installed seed, production recipe or ownership
count.

The first two review passes found that an optional bridge left current-source
Windows defaults unresolved. Public and private build drivers now select the
complete alias profile by default, including commands that select only long
paths. Historical plan and snapshot helpers retain their exact profiles. The
paired 444-method runs also exposed an unsorted membership mock, stale relink
keyword expectations, and the older seed linker help oracle. Those failed runs
remain recorded; at that checkpoint, the repaired defaults still needed checked-link and fixed-point
proofs before promotion.

## Integrated source evidence, 2026-10-01

The complete default profile selection passes 445 methods on each host at its
pre-CLI checkpoint. All 39 native and 39 checked preprocessor methods pass;
both canonical audits pass. Independent development fixed-point verification
checks the pre-CLI 34/42/43 artifacts and every retained stage. The subsequent
CLI proofs also pass independent verification with the same selected plans
and matching stage-three/four output. These proofs precede a source commit.

The integrated audit now attributes the resolver bridge to CupidASM. A review
of the real Make graph found its missing publication dependency. The bridge
is now a declared publication/bootstrap prerequisite and part of the 90-input
publication observation. Two new regressions cover both Make host branches
and same-size byte drift with restored modification time. The corrected full
477-method regressions, canonical audits and four conditional-contract methods
pass on each host and pass independent verification. Both complete 90-control
publications and the fresh paired OS/runtime checks also pass. Committed paired
producer proof, seed carriage and recipe adoption remain required.

The native manifest contract binds the exact 90-control inventory. Its producer
inventory requires every base path and permits only the known alias and long
shims. That preserves both historical profiles and accepts the current 77/78
profiles. Native and checked author/verifier tests reject same-count source
substitution and duplicate facts, then recover on a valid request.


The final source acceptance binds 1,558 inputs per host, sixteen exact artifacts
and 431 matching link inputs. The raw kernels are 9,579,628 bytes and embed the
same 66,594-byte manual. Both 200 MiB images have SHA-256
`a9c44748cff1c04339c4d2c6f62b117e404fb98874d4bfddd61a374e1f346ed4`. Both hosts retain the preceding user executable bytes and
pass their platform ABI and four private four-CPU max/e1000 boots. Independent
verification also rereads both complete default77 and long78 Linux publications,
including their 90 controls, 22 artifacts and 67 matching pairs.
