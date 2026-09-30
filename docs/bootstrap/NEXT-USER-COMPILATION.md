# Next native user compilation boundary

The file-path fixtures also distinguish launch paths from process working
directories. Both accepted compiler fixtures run from a long executable name
with an explicit application and a shorter working directory. A separate direct
Windows probe rejects a 361-character process working directory with error 267,
including extended spellings. Passing a long `--root` to CupidC from a shorter
working directory succeeds. Preserve the wrapper's existing launch contract;
extended file handling alone does not prove a long working directory works.
Evidence is `build/bootstrap/windows-long-current-directory-probe-v1.json`.

This plan covers the three user compiler transactions for `cat.cc`, `hello.cc`,
and `ls.cc` under `user/examples`. Their three links remain separate work.
The generated-install `compile-production` draft covers only the bin, demos,
and docs installation tables. It can advance independently; it does not
establish support for configurable user output directories.

## Existing path contract

`tools/cupidc_production_compile.py` accepts `<source-stem>.o` beneath a
normalized `user/<directory>/` path, excluding `user/examples/`. It validates
the binding before creating missing parents. The output may use nested or
hidden directories, lexical `.` and `..` aliases that normalize into the
approved subtree, or an absolute path inside that subtree. An external
absolute output, an escaped repository path, `user/hello.o`, a wrong stem,
and a wrong suffix are rejected. Unicode names and components longer than
127 characters work within the host filesystem's limits.

`user/Makefile` defaults to `BUILD=build` and prefixes the compiler argument
with `user/`. Relative custom directories and aliases that remain in the
approved subtree must continue to work. An external absolute `BUILD` is not
an existing external-output feature: the Make target and prefixed compiler
argument name different locations. Direct wrapper calls do accept absolute
outputs inside the permitted subtree. Make quoting limits are separate from
filesystem pathname support; not every name accepted by the API is accepted
by an unquoted Make rule.

Normalization must match the wrapper's lexical absolute-path rule and its
resolved repository root. Do not follow an output symlink to make an invalid
binding appear valid. The current `cupidbuild_path_safe` rejects dot components,
so normalization must precede that validation.

## Retained parent API

Introduce an opaque `cupidbuild_host_output_parent_t` with prepare,
require-current, bind-to-transaction, error, and close operations. Preparation
receives a validated normalized repository-relative output and canonical root.
It opens the root and retains every directory component through transaction
cleanup. Allocate records for actual components with checked arithmetic and
the existing overall path bound, rather than adding a small nesting limit.

Preparation creates directories only. It creates no input, object, temporary
file, or lock, and close never removes a directory. This preserves the
wrapper's persistent-directory behavior. Do not reuse the existing profile
parent object unchanged: it is tied to `build/bootstrap` and has directory
rollback rules that do not suit caller-selected output parents.

On POSIX, use parent-relative `mkdirat` and nofollow, directory-only `openat`.
Accept `EEXIST` only when the subsequent safe open returns a directory. Native
and hosted Linux paths can share the existing profile component-open helper
and syscall definitions. On Windows, use parent-relative `NtCreateFile` with
`FILE_OPEN_IF`, `OBJ_DONT_REPARSE`, directory-only flags, and retained handles
that prevent replacement. Reject ordinary-file collisions, links, and reparse
points. Existing directory identity snapshots and profile-parent binding
provide the comparison primitives.

Bind the prepared chain to the transaction's retained root and output parent
before compiler input capture. Recheck all ancestors before descent, after
preparation, before launch, before publication, after installation, and before
accepting an unchanged object. Compare directory identities and link status,
not directory timestamps; parallel sibling object writes must remain valid.
Source and header snapshots still require their full byte and metadata checks.
Keep existing output-leaf validation, locking, rollback, and recovery behavior.

## Race and pathname limits

POSIX cannot establish ownership atomically across `mkdirat` and the first
safe open. An ordinary directory replacement in that interval becomes the
initial baseline if the safe open and named identity agree. The protocol does
not claim to detect that replacement or own the original directory. It leaves
both directories and foreign contents alone. Once pinned, replacements must
be blocked or rejected by identity checks. No changed ancestor may redirect
nested creation through a link into an unrelated directory. This is not an
atomic filesystem snapshot; the existing Linux/DrvFS publication recovery
boundary still applies.

The installed Windows tools now use the shared UTF-8 codec, wide command-line
startup, and wide file and process adapters. The converged tools pass the
accented, Japanese, and supplementary-character path cases recorded in
[NEXT-WINDOWS-UTF8.md](NEXT-WINDOWS-UTF8.md). Those checks do not establish the
full configurable user-output path contract.

The Windows profile parent helper still uses a 128-wide-character component
buffer. The proposed user-output parent API must handle longer components and
retain the complete chain across preparation, capture and publication. Compiler
private paths need their own long-path checks too. Do not expose a restricted
user command that rejects paths already supported by the wrapper.

A September 21 bootstrap probe also reproduced a path-length limit in the
checked Windows CupidC. The same small kernel source compiled through
`cupidbuild compile-kernel` under a 54-character root but failed under a
224-character root with `cupidbuild: checked CupidC failed`. Direct calls to
the same checked compiler under the long root succeeded with a 237-character
output path and failed with a 276-character output path, reporting
`cannot write ... (io)`. This predates the generated-install promotion: the
probe used the existing checked seed. Native user migration must account for
the compiler's private bundle and output paths as well as CupidBuild's own
directory handling. Moving bootstrap evidence to a shorter checkout permits
that proof to run; it does not add long-path support or satisfy the user-path
acceptance requirement.

## Compiler and validation work

User compilation must retain `--freestanding -I /user`, its 180-second timeout,
and a closed two-record bundle containing the selected source and
`user/cupid.h`. It must preserve the existing syscall ABI gate and source/output
binding. The generated-install command retains its separate kernel profile
and six-record closures; it must not silently supply those flags to user code.

The ignored parent protocol model ran eleven methods on Windows and native
Linux, with one platform-specific skip on each. It checks wrapper path
acceptance, nested creation, Unicode and long components, files and links,
post-pin replacement, the POSIX first-open boundary, failure retention, and
concurrent sibling writes. Windows exercises junction rejection. The model
uses Python wrapper helpers and has no compiler or publisher. These results
are not native CupidBuild or checked-CupidC evidence.

Before a user recipe handoff, implement the retained-chain API and complete
pathname support, then test preparation and binding races, prelaunch drift,
post-installation rollback, and unchanged-output checks. Require no foreign
mutation, handle leaks, or output loss. Run the native code through host and
Cupid-built executables on both platforms; compare all three real user
objects and custom `BUILD` Make invocations. Carry the operation through both
staged proofs, promote the checked pair, and validate the recipe handoff.
Keep the three user links and their publication contract separate.

## Separate user-link boundary

Source inspection of `tools/cupidld_user_link.py` and `user/Makefile` identifies
three links: `hello`, `ls`, and `cat`. The wrapper accepts an existing object
and its matching executable in the same directory beneath `user/`. It fixes
the entry to `_start` and the text address to `0x01C00000`, with a default
60-second deadline for each checked tool. It does not create missing output
parents; compilation prepares those directories first.

The checked path freezes the complete seed, copies the object into a private
directory, validates it as an i386 relocatable object, and invokes CupidLD.
The candidate must pass the loader's ELF32 contract before CupidDis checks
known instructions, local targets, and code anchors. A zero exit status with
unexpected standard output or standard error is still a failure. Candidate
identity and bytes must remain unchanged across inspection. The wrapper
rechecks the live input object before replacing the executable with the
validated private bytes. Its current publication replaces equal output too;
unchanged timestamps are not an existing user-link guarantee.

The ELF check admits at most sixteen program headers and only `PT_NULL`,
`PT_LOAD`, and `PT_GNU_STACK`. It checks file bounds, 32-bit range overflow,
known permission flags, power-of-two alignment and load congruence, file size
against memory size, nonoverlapping nonempty load ranges inside
`[0x01C00000, 0x01E00000)`, and an entry in executable file-backed bytes.
Non-load headers may not contain a payload. Preserve these checks as an
independent candidate validator; a successful linker exit alone cannot
authorize publication.

Path normalization differs from the compiler wrapper: the link wrapper
resolves existing input and output parents, then validates their canonical
locations and name pairing. It rejects linked leaves, but an internal parent
alias may resolve to an approved directory. The native design must account
for that existing behavior as well as Unicode and long components. Retain
the resolved chain throughout capture, launch, inspection, and publication;
do not silently replace configurable build directories with a fixed path.

Before adoption, compare all three real executable bytes and run both default
and custom-directory Make builds. Negative cases must include malformed ELF
headers and segments, linker/inspector failures and timeouts, unexpected
inspector output, changed private candidates, seed/input drift, replaced
parents, and preservation of a previous executable. Existing wrapper tests
cover much of the candidate and drift behavior; this source audit does not
claim a fresh test run or a native link command.

## Implemented user executable validation

Source head exports `cupidbuild_validate_user_executable_bytes` from
`cupidbuild.h`. The caller supplies immutable candidate bytes and a nonempty
error buffer for one synchronous call. The validator allocates no memory,
retains no pointers or mutable global state, opens no files, and writes only
the error buffer. Success clears that buffer. Failure returns zero with a
terminated diagnostic; a missing or zero-capacity buffer returns zero.
The caller must keep input and output storage distinct.

The parser reads little-endian fields without aligned structure casts. It
bounds the program table before reading entries and checks unsigned i386
range addition before computing ends. At most sixteen nonempty load ranges
are retained on the stack. Non-load entries still need known flags,
power-of-two alignment, and zero file and memory sizes. Empty loads,
`PT_NULL`, and `PT_GNU_STACK` retain the existing wrapper rules. Sections,
physical addresses, and unused ELF identification fields are not newly
constrained. Known writable/executable permissions remain accepted, as they
are by the external loader and wrapper.

The independent Python validator remains the test oracle. The new contract
builds both with a host compiler and with checked CupidC, CupidASM, and
CupidLD. Its batch adapter tests byte preservation, repeated calls, failure
recovery, absent error buffers, and one-byte diagnostic buffers with adjacent
sentinels. The Python suite covers valid boundary layouts, explicit malformed
cases, deterministic mutations, and freshly compiled and linked `cat`,
`hello`, and `ls` executables.

This API implements only candidate-format validation. The native user-link
command must still capture the input object and complete seed, retain the
resolved output parent chain, run and check CupidLD and CupidDis, recheck
candidate and source identities, and publish through the guarded transaction.
The existing user-link recipes, seed images, and ownership counts are unchanged.

Run the native and checked-CupidC comparison suite with
`make -C toolchain test-cupidbuild-user-elf`, or invoke
`python -m unittest -v tests.test_cupidbuild_user_elf` from the repository root.
The bootstrap log records executed host and OS checks separately from the
remaining user-link transaction and future seed promotion.

## 2026-09-29: lexical user-path resolution in progress

`cupidbuild_resolve_user_compile_paths` resolves lexical source and output aliases
against an absolute repository root, then checks the existing approved pair.
The output must keep the source basename with an `.o` suffix, lie below a
non-`examples` child of `user`, and stay within the repository. The API accepts
nested output directories and preserves spaces and UTF-8 bytes. It models POSIX
and Windows path separators, drive roots, same-drive relative paths, rooted
Windows paths and UNC shares without consulting the current working directory.
A different drive-relative path cannot be resolved without a drive working
directory and is rejected. Windows root-prefix comparison folds ASCII case;
Unicode case aliases still need filesystem-level handling before full migration.

This is lexical validation only. It neither proves filesystem containment nor
creates directories. A source may still name a link, and a parent may still be
replaced after normalization. Production must not use this API as its sole path
check. Failure clears the result, diagnostics respect their supplied capacity,
and a subsequent call can succeed without resetting global state.

The first Windows strict build rejected two `strcpy` calls. They were replaced
with copies whose lengths are checked before writing. The new tests exercise
both path syntaxes in host-built and checked-Cupid-built callers. Both final host and checked-Cupid caller suites pass all seven methods,
including malformed-UNC rejection. The paired record is
`build/bootstrap/user-paths-paired-v1.json`. The first Linux run found that joining a path to `/`
introduced a second leading slash; the join now preserves the original anchor.
The Windows caller also used the retired ANSI link plan, which omitted the
current host adapter's path codec. Its fixture now uses the complete UTF-8 plan.

## 2026-09-29: retained output-parent API in progress

`cupidbuild_host_output_parent_prepare` validates the complete normalized output
path before creating directories, then retains each directory from the repository
root to the output parent. Creation and descent use the retained parent handle:
`mkdirat` followed by a no-follow open on Linux, and relative `NtCreateFile` with
`FILE_OPEN_IF` on Windows. Links, file collisions and ancestor identity aliases
fail. The API creates no lock or output file. Created directories remain after
failure and close; another build may already be using them. Linux creation and
the first open are separate operations, so this does not establish exclusive
ownership of a newly created directory.

The preparation checks identities and each parent/name binding. Directory
metadata may change as sibling jobs run. The existing read-only observer keeps
its stricter root metadata check. Failed preparations retain a diagnostic and
must be closed, including partial preparations. The transaction borrows the
preparation, which must outlive it.

`cupidbuild_host_output_transaction_open` attaches the chain before acquiring an
output lock or capturing source bytes. Publication-boundary checks recheck the
complete chain, including equal-output publication. The separate bind API can
attach to an already opened transaction after checking the root, parent and leaf
name. It cannot retroactively guard that transaction's opening. Windows retained
handles prevent directory replacement; Linux rechecks reject an ancestor
replacement even when the original leaf directory was moved back below it.

This remains a source API prerequisite. Windows repository roots still require
a drive-rooted path in the underlying observer; lexical UNC support does not
establish filesystem support. User compilation, user links, source closure and
production adoption remain separate work. No restricted user CLI has been added.

The installed Windows CupidC still cannot write the reproduced 310-character
output path. `build/bootstrap/user-longpath-probe-v1/result.json` records exit 1
and the missing output. Its UTF-8 runtime converts paths to UTF-16 but does not
provide extended absolute paths to the file API. Fixing that boundary must also
preserve relative paths, drive/UNC handling and text used for process arguments.
Long directory preparation alone does not prove compiler or transaction support.

Final focused runs pass all 49 methods in each configuration. Linux host-built:
2.245 seconds, two skips; Linux checked-Cupid: 41.181 seconds, one skip. Windows
host-built: 11.135 seconds, five skips; Windows checked-Cupid: 55.076 seconds,
four skips. The cases cover nested and hidden directories, UTF-8 names,
200-character components, total paths beyond 260 characters, complete lexical
validation before creation, collisions, sibling writes and concurrent sibling
preparation, links/junctions, replaced parents and transplanted ancestors,
transaction binding, publication and equal-output timestamp preservation.
The existing observer regressions pass in the same runs. Logs are
`build/bootstrap/output-parent-{native,checked}-{linux,windows}-v7.log`;
`build/bootstrap/output-parent-paired-v1.json` records source and log hashes.
This is focused caller evidence; full integration, source-audit refresh and
production acceptance are still pending. The source changes remain uncommitted.

## 2026-09-29: source Windows long-path support

Added a shared, allocation-free UTF-16 helper for extended drive and UNC paths.
It requires an absolute normalized path, valid scalar sequences and backslashes;
it rejects dot/parent components, malformed shares, device namespaces and results
that exceed 32,767 units including the terminator. Failed outputs are cleared and
capacity checks preserve adjacent sentinels. Adapters retain explicit device
paths without reinterpreting them.

The native Windows adapter resolves ordinary file paths once with
`GetFullPathNameW`. Names whose input or resolved length reaches 248 units use an
extended absolute path. Short spellings retain their original Win32 form. File
reads, writes, attributes, deletion, moves, application paths and working
directories use this conversion; command text, file modes and environment names
keep text conversion. Cleanup preserves the file API error.

The checked adapter enables the same behavior under `CUPID_WINDOWS_LONG_PATHS`.
The internal Windows plan selects that definition for every adapter role and an
exact import profile. Ordinary tools gain `GetFullPathNameW` through a separate
startup shim. Publication and CupidBuild roles already import it. Each tool gets
one resolver. Stage linking and PE validation select the same profile. Existing
ANSI and UTF-8 plan identities and installed import validation are preserved.
The long-path plan is not yet selected by the full bootstrap CLI: source capture,
manifest readers, paired proofs and promotion still need to carry it.

The first real compiler fixture passed long reads/writes and lexical aliases,
but its missing-input assertion expected `cannot read`; CupidC reports
`cannot load`. The corrected fixture includes long and relative repository roots.
Both native and checked CupidC pass all four methods in 205.346 seconds, with
byte-for-byte agreement against short-path reference objects. The log is
`build/bootstrap/windows-long-path-real-v2.log`; its sibling directory retains
both tools, their hashes, the selected plan and captured fixture input hashes.
This is focused compiler evidence, not a staged self-bootstrap proof.

The first checked failure fixture called `abort`, which its small runtime does
not expose. The fixture now records invariant failures and returns a nonzero
status. It does not require a new runtime API to test path conversion. Final
checked Windows adapter runs pass seven methods in 30.610 seconds. Linux passes
32 adapter, codec and plan methods in 21.099 seconds, with two Windows-only
skips. Native Windows allocation, codec and plan cases pass 31 methods in
5.781 seconds. Evidence is `checked-long-path-adapter-windows-v3.log`,
`checked-long-path-adapter-linux-v3.log` and `long-path-adapter-native-v3.log`
under `build/bootstrap/`. Older failed logs are retained.

Microsoft documents that extended paths require an absolute spelling and do not
normalize slashes or dot components. The adapters therefore resolve before
prefixing: https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
and https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew.

## 2026-09-29: final path-support regressions and OS replay

A direct Windows probe found that `GetFullPathNameW` can resolve a long ordinary
spelling ending in `NUL` or `CON` to a device namespace. Both adapters now retain
that resolved name instead of passing it to the ordinary-path prefix helper.
The native allocation fixture, checked API fixture and real compiler fixture
cover this case. Final real compiler replay passes five methods in 238.783
seconds, including the discarded device output. Native and checked objects still
match their short-path references. Evidence and both fixture executables are in
`build/bootstrap/windows-long-path-real-v3/`, with its sibling `.log` file.

The final Windows adapter/codec/plan suite passes 38 methods in 53.024 seconds.
The Linux suite passes 32 methods in 33.337 seconds, with two Windows-only skips.
Logs are `long-path-adapters-windows-final-v1.log` and
`long-path-adapters-linux-final-v1.log`. The six-tool native help, entry selection,
Unicode publication and rejection suite passes three methods in 437.265 seconds
(`long-path-native-tool-regressions-v1.log`). The fixed-point audit rejection
corpus passes its mutation method in 345.252 seconds. Its exact link-selector
check and corresponding mutation were updated for the explicit long-path flag.

The direct worktree OS build failed before compiling the TLS CA bundle object.
Windows denies reading the existing `kernel/tls/tls_ca_bundle_data.o` and its ACL;
the source is readable. A single-target replay fails the same way and preserves
its size and timestamp. The object has not been moved or had its permissions
changed. `tls-ca-transaction-replay-v2.json` records the replay. A separate OS
copy contains the captured current sources and previously independently verified
outputs. The same installed coordinator compiles the TLS target there, preserves
its verified SHA-256 and timestamp, and exits zero; evidence is
`tls-ca-isolated-replay-v1.json`. This distinguishes the existing inaccessible
output from a compiler or source regression.

The separate OS build was resumed after its original tool handles disappeared
and process inspection found no live build. All 1,538 captured source files in
that copy still matched the preparation record. Evidence is under
`build/bootstrap/user-path-os-v1/`. Image completion and boot smoke remain pending
at this checkpoint. The goal remains active; user transactions and long-profile
seed carriage are still required.

Reproduce the file-boundary suites from the repository with
`python -m unittest -v tests.test_windows_long_paths` on Windows and
`python -m unittest -v tests.test_windows_utf8_checked tests.test_path_encoding tests.test_windows_utf8_plan`
on either host.

## 2026-09-29: complete long-profile source capture

The explicit `windows_long_paths` source selection requires a Boolean UTF-8
selection and includes `utf8_long_path_start.asm`. Frozen source records retain
both selections; live and private revalidation use them again. The current
canonical long profile captures 77 inputs. Historical plans keep their defaults
and exact installed import validation. Missing shims and invalid selections are
rejected before creating a frozen directory; live and frozen shim changes fail,
and restoring the bytes permits a fresh recheck.

Both hosts pass 29 plan/capture methods: 5.568 seconds on Windows and 12.648
seconds on Linux. Six default source-freeze and plan regression methods also
pass: 6.645 seconds on Windows and 1.145 seconds on Linux. Evidence is
`long-path-source-capture-{windows,linux}-v2.log` and
`long-path-source-default-regressions-{windows,linux}-v1.log` under
`build/bootstrap/`. The first new count assertion used the historical manifest's
old closure count; the current source tree also contains the later policy header.
The current historical fixture captures 75 long-profile inputs, while the
canonical plan captures 77. The failed v1 log is retained.

The full bootstrap CLI, manifest/publication readers, paired staged proofs and
promotion still need to carry the selected profile. Production user compilation
and user linking remain open; installed seed payloads and ownership are unchanged.

## 2026-09-29: accepted incremental Windows image and boot

All 83 fresh Doom transactions passed and reproduced the earlier object bytes.
The incremental Make replay retained those outputs and the 156 kernel objects
whose exact closed inputs and producer identities were unchanged. It regenerated
the manual asset, installation tables, symbols, both kernel links and typed flat
kernel. The artifact verifier then rejected only the three expected old kernel
sizes. The policy now records 9,572,436 bytes for `kernel.bin`, 9,802,172 for the
final ELF and 9,671,100 for the pass-one ELF. The raw kernel grew by 1,168 bytes;
the embedded manual is the only changed object in the checked code cohort.
The first failed size check and `policy-transition-v2.json` retain that calibration.

The final Make image replay passes all sixteen exact artifact checks and image
publication. Both hosts pass seventeen policy methods: 9.656 seconds on Windows
and 23.616 seconds on Linux. Logs are
`user-path-policy-regressions-{windows,linux}-v1.log`. The final graph audit passes
with 764 active sources, 452 transforms and unchanged 441/11 ownership. Its first
new invocation used a backslash Python path that the audit's shell parser treated
as `C:Python314python.exe`; the slash-form replay passes. The failed v5 and passing
v6 logs are retained under `build/bootstrap/`.

Windows acceptance checks all 1,538 captured inputs, sixteen artifacts and the
431-input code cohort. It independently compares the disk's boot code and raw
kernel region with the current artifacts. The three user executables match the
previous acceptance record. The private four-CPU max/e1000 smoke passes raw
disassembly, shell completion and SMP runtime checks; the serial log reports all
four discovered CPUs online. No panic or corruption marker appears. The image
hash remains `3abecb9d57efded4b91e9b89a06603cd851731605bfaf5b0fa1dc91dce38fefd`
before and after the private smoke.

An independent reread verifies the source capture, all artifact/code-input and
user hashes, the image identity and runtime logs. Acceptance records are
`windows-os-acceptance-v1.json` and `independent-acceptance-v1.json` under
`build/bootstrap/user-path-os-v1/`. This is incremental Windows OS acceptance,
not a clean paired self-bootstrap or seed promotion. Installed seeds, user recipe
ownership and TempleOS scope are unchanged. Full user transactions, long-profile
manifest/CLI carriage, paired proofs, promotion and full Doom runtime/performance
acceptance remain open.

## Long-path bootstrap profile carriage, 2026-09-29

The bootstrap and contract-publication drivers accept `--windows-long-paths`.
Both proof drivers capture the same selected 77-file producer inventory. The
Windows plan builds 32 C objects, four assembly objects and six tools; Linux
builds 27 C objects, startup and six tools. Default producer capture remains at
76 files. Publication captures 89 inputs, with a 116-file producer/publication
union for either mode.

The shared reader accepts count 77 only with the exact Windows plan digest and
the complete installed `5ba6ea24` parent tuple. Linux shares the existing plan
digest, so its count and parent distinguish the new profile. Windows image
validation has a distinct exact import table: ordinary tools gain one
`GetFullPathNameW`; publication and build tools retain their existing wide tables.
Historical profiles keep their count, plan, parent and import rules. Behavior
fixtures bind the actual captured execution and plan parents.

Publication authoring and verification admit either complete 76- or 77-file
producer inventory. Only the latter contains the ordinary resolver shim. Live
verification recaptures that inventory, and reuse requires the requested profile.
Invalid selection fails before input or output preparation. Installed seed
identities and production ownership remain unchanged.

Both hosts pass 259 profile/publication methods. Checked Cupid readers agree on
1,267 structural manifest cases and fourteen release/manifest pair cases; five
shared object pairs also match. The native Windows six-tool suite passes three
methods in 396.262 seconds and builds the complete checked long profile. Evidence
is under `build/bootstrap/long-path-profile-v1/`, including
`paired-checked-reader-v1.json` and the v3 profile/publication logs.

The selected driver rerun passes seventeen regressions on each host, with four
expected Linux skips. The audit mutation corpus passes in 309.522 seconds. The
generated graph has 765 active source inputs, 41 unreachable files and 452
transforms; ownership remains 441 CupidBuild actions and eleven Python actions.

Windows OS acceptance uses an isolated incremental build. All 83 fresh Doom
objects match the previous accepted objects; 156 retained kernel objects have
byte-identical source-specific closed inputs and the same installed producers.
The final manual is present in its object, ELF kernel and raw kernel. Sixteen
artifact checks and all three user programs pass. The private four-CPU max/e1000
smoke completes disassembly, `ls` and SMP runtime checks without changing the
source image. Independent rechecks cover 1,540 source inputs and 431 recorded
link inputs. The raw kernel is 9,572,532 bytes. The 209,715,200-byte image has
SHA-256 `5b155c56e4a0461b446d9a71385789a6afd96262b6ef031aa8b8ba46d87199a0`.
Evidence is under `build/bootstrap/long-path-profile-os-v1/`. This acceptance
uses the installed seeds and does not promote the new long-file profile.

Fresh paired stage-three/four proofs, reviewed promotion and closed user
compilation remain open. Long file names do not establish long process
working-directory support. ADR 0410 records the profile boundary.

Reproduce the selected proofs with `tools/bootstrap_toolchain.py bootstrap`
and `bootstrap-windows`, adding `--windows-long-paths` to both. Linux uses its
checked manifest for `--manifest`. Windows uses the checked PE manifest for
`--manifest` and the Linux manifest for `--plan-manifest`. Both take `--root` and
a fresh `--output`. Contract publication uses
`tools/cupidc_toolchain_contracts.py build --windows-long-paths` with its existing
root, manifest, output and worker arguments. Verification derives the selected
inventory from the checked publication facts.
