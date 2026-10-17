# Retained directory discovery

Status, 2026-10-16: focused Windows native and Cupid-built callers pass. The
portable drivers also pass fresh Windows native qualification and both Linux
selections. The saved Linux evidence passes independent review; later Windows
input metadata drift holds continuation under those old admission graphs.
Installed seeds, normal Make recipes and the three Python coordinators keep
their existing ownership. This interface does not adopt the SDK verifier or
publisher and does not qualify the held source-selection observer profile.

The SDK verifier discovers header and include names through Python's pinned
directory reader. Before this interface, the ordinary observer could check a
supplied exact set but could not return discovered names. Its transaction
discovery interface belongs to a writable output transaction. A read-only
verifier needs discovery without creating that transaction or preparing
publication state.

## Interface and lifetime

`cupidbuild_host_observer_directory_snapshot` takes an existing ordinary observer
and a relative directory path. An empty path selects the retained root. Complete
observer validation brackets capture. Enumeration uses the retained directory
handle and records its exact membership for later unchanged checks. A token
becomes visible only after the final complete check succeeds.

The observer owns the opaque token and a separate immutable copy of every name.
`cupidbuild_host_observer_directory_snapshot_names` first checks that the token
belongs to the supplied observer, without dereferencing an unknown token. It
then validates the complete observer before returning the borrowed names and
count. Both calls return one on success and zero on failure. For an empty
directory, access succeeds with a null names pointer and zero count.

The second call supplies a current validation result. Holding a token alone
does not keep a live filesystem unchanged. Names are sorted by byte spelling,
retain literal strict UTF-8, and exclude dot and parent entries. Suffix policy
belongs to the consumer. Names establish neither child kinds nor payload
identity; callers use the existing kind or file observations for those facts.

There is no token-close operation. The observer frees all token storage when it
closes and retains its existing handle-close error reporting. Callers must keep
the observer alive, serialize calls, provide valid disjoint request and result
storage, and leave returned names untouched. Reentry and closing an observer
while it is in use are invalid. Borrowers of the observer keep their existing
lifetime requirements.

Tokens have independent name storage because the older
`cupidbuild_host_observer_directory` operation can replace the root's membership
array after checking its previous value. That operation keeps its public
behavior; replacing its array cannot invalidate an earlier token's view.
Repeated snapshots retain separate tokens and consume their respective budgets.

## Capture and failure rules

The implementation shares retained-handle enumeration with the existing exact
membership check: relative NT directory queries on Windows, `getdents64` in the
custom i386 Linux adapter, and `fdopendir`/`readdir` on native POSIX. It adds no
Windows imports or delete sharing. There are no file-ID calls, child renames,
process launches, output creation or publication operations in this interface.

An existing root membership remains the baseline. A newly enumerated set must
match it directly; capture cannot replace an unchecked baseline. New membership
allocations are completed before binding them to their retained entry. Partial
token allocations are freed on failure; an already bound entry remains owned by
the poisoned observer and is reclaimed at close.

Invalid arguments, foreign or null tokens, unsafe names, malformed UTF-8,
duplicate names, capacity exhaustion, failed allocations or enumeration, drift,
and reported handle-close failures reject the operation. Result outputs clear
before argument and prior-poison checks. A supplied observer stays poisoned
after failure. A fresh observer is required for recovery.

The ordinary observer still checks full root metadata, retained directory and
ancestor identities, fresh parent bindings, prior file metadata and captured
digests, and every observed exact membership. These sequential checks do not
provide an atomic filesystem snapshot after the last check. They grant no
release, output or publication authority.

## Limits and naming

The existing limits remain 4,096 retained handles, 4,096 retained membership
names, 8,191 path bytes and 1,023 bytes per UTF-8 component. Snapshot storage has
separate limits of 4,096 tokens and 4,096 cumulative copied names. Empty snapshots
consume token slots. A token may fail at either name budget even when handles
remain available. Counters increase only within checked remaining capacity.

Each token's pointer vector grows from one by doubling, capped at its remaining
name allowance. Its capacity stays below twice its actual nonempty name count.
An early source draft allocated all remaining pointer slots for each token;
review found that repeated one-name captures would overallocate. The draft was
corrected before execution. No runtime conclusion comes from that source finding.

This uses the ordinary observer's existing portable-name grammar. In particular,
POSIX colon and backslash names remain rejected. Admitted control characters
retain their literal bytes. The separately held source-selection profile is
absent from this base and has not been adopted or reimplemented here. Future
consumers needing that profile must preserve its separately reviewed semantics.

## Focused qualification

The new C contract and Python test driver exercise the same public interface.
The driver accepts these selections:

- `CUPIDBUILD_OBSERVER_DIRECTORY_SNAPSHOT_PROGRAM`: supplied ordinary caller.
- `CUPIDBUILD_OBSERVER_DIRECTORY_SNAPSHOT_FAULT_PROGRAM`: distinct fault caller.
- `CUPIDBUILD_OBSERVER_DIRECTORY_SNAPSHOT_PRODUCTS`: existing output directory
  outside the repository. A new exclusive child retains fixtures, programs and
  per-command argv, status, raw output and failure receipts.

Without a supplied caller, the native fallback builds the contract, host adapter
and path encoding module. All three observer drivers select the executable
named by `CC`, defaulting to `clang` on Windows and `cc` on POSIX, and resolve it
through `shutil.which`. A missing compiler produces an explicit error. The
resolved executable's basename selects `--no-default-config` for Clang; GCC
receives the same strict C11 flags without that option. The selected executable
spelling and the 180-second build bound remain intact. Products use the private
output directory. With a supplied ordinary caller and no fault caller, fault tests are
explicit skips; they never substitute a native build. The shared caller needs no
platform defines or filesystem calls. Its host and runtime still require their
normal target-specific definitions and bridges.

Fault selection requires `CUPIDBUILD_DIRECTORY_SNAPSHOT_TEST=1` in both caller
and adapter. This otherwise inactive hook covers allocation failures, a partial
enumeration failure, failed enumeration completion, failed post-binding capture,
and a mutation just before the final capture check. Completion injection does
not claim an actual operating-system close failure. Injected paths require
their own qualification; passing an ordinary caller does not cover them.

Ordinary cases cover empty, Unicode and mixed-kind name sets; deterministic
order; root-array replacement and token lifetime; invalid arguments and token
ownership; bounds; poison and fresh lifetime recovery; additions, removals and
renames; retained parent binding; full root metadata; and captured payload drift
with restored time. They also show that membership alone leaves child kind and
unobserved payload changes outside its claim. POSIX cases cover the current
portable-name restriction, malformed bytes, control characters, links and FIFOs.
The existing ordinary observer and kind suites remain regression requirements
because enumeration is shared.

## Failed fixture assumption and correction

Root's R2 checked attempt, `8015` through terminal `1e7df2`, closed with return
code 1, child status 1, null error and no active cleanup children. The ordinary
caller, host adapter, runtime and assembly compiled and linked. Command 12 then
failed while compiling the separate fault contract: `CTB000010` at line 100,
column 28, on `allocation_failure = atoi(mode + 12u)`.

The fixture assumed an interface absent from the hosted `stdlib.h`. That header
already declares `strtoull`; Root's source classification `3d2bcf` identified
this as an incorrect fixture dependency. The correction uses `strtoull` with
a nonempty decimal start, a complete-tail check and a nonnegative result that
fits the target's signed 32-bit index. The eight allocation injection cases
and all other selectors and assertions remain unchanged. No runtime, header
or compiler change is needed or authorized.

The R2 plan, source freeze and failed products remain preserved. Fresh R3
admission and execution qualify the corrected fixture below. The repair changes
no compiler, runtime, adapter behavior or allocation-failure assertion.

## Prior Windows qualification and portability status

The original native selection passes 105 methods with nine declared skips among
114 selected methods, including the complete ordinary-observer and kind suites.
After the fixture correction, both the focused native run and the supplied
Cupid-built run pass 21 methods with three POSIX skips among 24 selected methods.
All eight allocation-failure indices and the other fault cases run in each
selection. The checked caller and fault caller are distinct static i386 PE
programs with the existing 38-import profile; their 15 compile, assemble, link
and test commands all succeed.

Both runs close with child status zero, null controller error and no remaining
owned processes. Independent evidence checking joins the 4,263 frozen inputs,
eight source files, 61 distinct source copies, every completed checked command
and all retained products and raw receipts. Native CRLF and Cupid LF output
remain separate byte records; parsed contract records agree. Unknown original
writers remain unknown. The failed R2 attempt remains a failed predecessor.

These results cover the original Windows source and runtime qualification.
The portable-driver selections below cover Linux name, link and FIFO behavior.
SDK verification, publication and the held source-selection profile remain
separate work.

The compiler-selection revision changes only the three Python drivers. Their
test methods, assertions, selectors, skips and deadlines are unchanged, as is
the ordinary observer's embedded C caller. Native fallback still leaves
`CUPIDBUILD_OBSERVER_PROGRAM` unset, so the existing native-only invalid
destination guard continues to skip the libc `fread(NULL)` case. Supplied
programs keep their existing short circuits and checked behavior. A fresh Windows native run of the revised drivers passes 105 methods with nine
declared skips among 114 selected methods. Its child status is zero, controller
error is null and owned process cleanup is complete.

The fresh Linux native selection passes 111 methods with three declared skips
among 114 selected methods: two Windows junction cases and the native libc
`fread(NULL)` guard. The supplied Cupid-built selection passes 112 methods; only
the two Windows junction cases skip. All 24 snapshot methods, allocation-failure
indices zero through seven and recovery checks pass in both selections. The
checked selection completes all 14 commands and retains 13 i386 products. The
raw command bodies and complete saved fixture/model joins pass independent
review. These are saved execution results, not a later Linux filesystem check.

A later Windows check finds 790 historical models differing only in held-handle
`ctime_ns`, including 782 of the 799 outer inputs. Their bodies, digests, named
metadata, parent identities and other held fields match. The original expected
models remain unchanged and their current continuity verdict is HOLD. Successful
saved test results remain recorded separately; they grant no fresh execution
authority after this discontinuity.

## Windows image and stock boot evidence

The OS checks use the existing image coordinator and buffered producer route.
A fresh ordinary Windows copy retains the 451 compiled products from the
initial stock Make run. The reviewed completion invokes only the sixteen-row
size check and image publisher; compilation, linking and regeneration do not
run again. Completion returns zero and publishes a 209,715,200-byte image with
SHA-256 `7707a8cd57ad7997cdd5b5d015708b8cf6a1b113575074d65f673abeb866a3b0`.
The image readback checks its boot sectors, full kernel and staged ISO/FAT chain.

A separate stock private-image boot uses e1000, four CPUs, CPU max and one `ls`
command under the original 300-second harness limit. Child and qualification
statuses are zero, with no controller error or remaining owned processes. All
62 TLS checks, CPU/FPU markers and one ordered compile/JIT/terminal completion
pass. The invocation records 79.865 seconds; its private temporary directory is
empty after cleanup. Image, serial and transient-copy native writers remain
unknown.

The source size policy adopts kernel.bin 9,405,468 bytes, kernel.elf 9,635,472
bytes and kernel.elf.pass1 9,504,400 bytes. The other thirteen rows and all twelve
seed binaries retain their prior values. The original Windows and Linux stock
Make runs remain failed at their stale size checks. This evidence establishes
copied-output completion and one Windows stock boot.

The fresh Linux preparation copies 2,722 payloads and six controls. Its later
completion rejects `/etc/ld.so.conf.d/ld.wsl.conf` during retained resource
validation, before a supervisor or Make process starts. The saved partial
cohort still matches all preparation models; the changed field and cause are
unknown. Linux OS image and guest qualification remain open. The separate
QEMU filesystem inspection also remains failed after rejecting an unrelated
masked service alias to `/dev/null`; it establishes no QEMU availability claim.

The [portable OS record](evidence/bounded-host-api-windows-os-qualification-20261016.json.gz)
retains raw Windows completion, harness and serial bodies, the Linux failure,
full file models and source-body joins. Standalone API qualification on both
hosts remains recorded above. Hosted callback integration, SDK publication,
installed seeds and normal coordinator ownership retain their separate scope.
