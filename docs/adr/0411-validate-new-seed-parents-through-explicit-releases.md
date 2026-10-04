# ADR 0411: Validate new seed parents through explicit releases

Date: 2026-10-01

Status: Private implementation qualified; final image and runtime checks passed

## Context

The proposed directory-alias seed pair passes its four committed producer
proofs. Consuming that pair through the Windows default profile fails in the
checked CupidObj runner: `fixed-point provenance differs`. The behavior
manifest correctly names the supplied execution and plan seeds as its parents.
The native reader recognizes only older parent tuples embedded in its source.
Adding the finished new manifest hashes to that source would change the source
that produces those same manifests.

The external release record already contains the complete source, parent, plan
and artifact identities. Its authority belongs to the caller, as established
in ADR 0400. It can supply new parent identities without a source hash cycle.

## Decision

Keep `cupid_seed_manifest_validate` and its historical parent rules. Add
`cupid_seed_manifest_validate_release` for callers that explicitly authorize
an immutable release record. Parse the record, match every applicable manifest
claim, then run the complete structural, target, producer and supported-plan
checks with that record's parent tuple. Failure clears the owned result.
The record does not authenticate itself or establish a file's lifetime.

The existing pair API already receives a release. It uses the release-aware
reader for each manifest and still hashes the actual Linux manifest bytes to
verify the Windows plan reference. A matching declared digest is insufficient.

Add a separate checked-runner entry point taking an explicit release path.
The original request layout and entry point remain unchanged. The CLI accepts
`run --seed-release RELEASE`; duplicate, missing and empty values are usage
errors. The execution transaction captures the release with the manifest and
tool images, reads at most 65,536 release bytes, and checks its observations
again after execution. Ordinary calls still use the historical reader.

Extend the same separate release argument to every typed seeded operation:
object and raw assembly, JPEG embedding, symbol generation, kernel flattening,
profile generation, the four compiler profiles, and physical and resolved user
links. Each original function forwards a null release to its new variant. The
request structures keep their existing layouts. Every seeded CLI command accepts
the optional release path and rejects duplicate, empty or missing values.

Reserve the release input together with each captured closure before retaining
paths into the transaction input table. The table can move when it grows. The
initial native profile and Doom tests crashed after the additional release
forced such growth; a debugger showed a freed tool-path pointer in execution
profile validation. Counting the release in the profile, compiler and flatten
reservations fixes that failure without changing closure requirements.

Open Linux regular-file candidates with nonblocking and close-on-exec flags
before inspecting their kind. This applies to both the native and raw i386
adapters. A FIFO must be rejected before waiting for a writer.

## Evidence and remaining work

The private bootstrap coordinator now separates preparation from behavior
qualification. Preparation builds all three stages and compares stages three
and four, then writes `stage-preparation.json` with status `unqualified`. It
retains the source closure, every object and all six tools. It supplies neither
behavior evidence nor `bootstrap-report.json`, so the complete bootstrap
publisher rejects this bundle.

The separate author requires both preparations, independently selected parent
manifests, a caller source revision and a caller source snapshot digest. It
checks actual source and artifact bytes and creates a twelve-artifact release
candidate exclusively. Qualification rebuilds the stages and matches every
object and tool against the preparations before running behavior. Materialized
fixtures use the captured source, real parent tuple and actual staged tools.
Their commands receive the explicitly selected release before the child
argument separator. The original bootstrap commands retain historical rules.

The initial Windows boundary group passes twelve selected methods, with the
one POSIX FIFO replacement method skipped. The existing staged compiler and
publication group passes 27 methods. These are coordinator and metadata checks,
not completed producer or behavior proofs. Review found and corrected an
unbounded pathname read and missing rebuilt-object comparisons. The reader
opens one handle, checks regular kind and identity, reads at most the bound plus
one byte, and checks handle and pathname observations again. Windows reports
different creation-time and executable-suffix metadata through `lstat` and
`fstat`; full comparisons stay within each observation family.

Fresh native suites pass 42 selected methods per host: 39 execute on Windows
and 38 on Linux, with only their named platform skips. All 16 typed-operation
methods execute on both hosts. They compare release-authorized results with
historical results, check strict rejection afterward, and preserve output bytes
and timestamps for mismatched parents. The compiler checks use active kernel,
Doom and user sources; real CupidObj generates the installation source. Direct
physical-link API tests exercise both the original function and a null release.
A native assembly launch hook checks release drift and prior-output preservation.

Installed Cupid tools freshly compile and certify the canonical CupidBuild and
CupidLD object union plus the physical API caller: 23 objects on Windows and
16 on Linux. Each host links three programs: the normal dispatcher, physical
caller and current linker. The unchanged checked suites pass 19 methods per
host, with one Windows POSIX skip. They execute all 14 typed APIs through the
actual Cupid-built programs. The paired independent verifier rereads 114
executed method results and eight declared skips, exact command arguments,
objects, executable formats and imports, and all 1,562 captured source files
and copies. It checks the complete retained evidence again before acceptance.
These checks qualified the operation path before the staged proofs below.

The tight pair regression fails before the change with the same provenance
diagnostic and passes afterward. The complete manifest and pair modules pass
44 native methods on each host. Cupid-built callers on both hosts match 1,464
historical manifest cases, 338 pair cases and 83 release-aware cases. Their
release-aware checks exercise capacities 0, 1, 2, 8 and 128, guarded storage,
null diagnostics, unchanged inputs and cleared failure results. An independent
verifier rereads all 3,770 checked results, both executable formats, seven
objects per host, all source and support copies, closed logs and the complete
1,558-input capture.

The private native execution suite passes five methods on each host, with one
Windows platform skip. It executes the real CupidObj, checks strict rejection
before and after an explicit release call, and preserves prior output for
altered release claims, malformed records and usage failures. The first Linux
FIFO fixture could not run on DrvFs. Moving it to a native temporary directory
exposed a 30-second blocking open. The nonblocking change makes that case pass.

The broader native runner group passes 26 selected methods on each host:
23 execute on Windows and 22 on Linux, with only their declared platform
skips. The first broader run found an outdated help assertion. Its replacement
requires the documented release option. A Linux rerun also exposed shared
temporary-file roots between the concurrent host tests. Fresh source copies
for each host remove that interference; the failed runs remain recorded.

Installed CupidC, CupidASM, CupidLD and CupidDis build and validate the normal
modified dispatcher on both hosts. It passes the four unchanged release-path
methods: three execute on Windows with its POSIX skip, and all four execute
on Linux. This checks real tool execution, strict rejection before and after
explicit authorization, malformed and mismatched records, usage failures,
prior-output preservation, and raw Linux FIFO rejection.

Checked deterministic drift coverage remains pending. The Windows race build
fails because the hosted headers do not declare `getenv`; the native test
hook also uses it. The raw Linux adapter has no equivalent deterministic
launch hook. Native drift tests observe a real child launch, edit the release,
and require failure with suppressed success output and complete cleanup.
Those native tests do not replace checked drift coverage.

The new Cupid-built Windows caller also passes the exact default fixture
that originally failed. Preparation copies its manifest and six tools without
changing any bytes or parent identities, then writes a private release from
the actual manifest claims and paired default tool cohort. An explicit call
produces the same 476-byte ELF as the earlier baseline. Strict calls reject
before and after; a mismatched release preserves the 12-byte prior output.
Both source roots, the supplied parent seed pair and retained proof inputs
are reread around every call.

Original native entry points still use the historical reader. The original
Windows default consumer failure remains frozen. New complete default
release-authorized bootstraps pass on both actual hosts with host C/ASM tools
forbidden. Independent verification checks all 228 rebuilt stage artifacts,
76 final-stage pairs and 7,300 behavior files. Linux passes 62 success, 55
failure and seven help cases; Windows passes 49 success, 43 failure and seven
help cases. Source, selected parents, plans, reviewed release bytes, commands
and closed logs are reread before acceptance.

Long preparation, separate authoring and fresh complete qualification also
pass on both actual hosts. Independent proof checks the 78-input closure,
231 stage artifacts, 77 final-stage pairs and 7,300 behavior files. Windows
has 32 C and five assembly objects plus six tools per long stage; Linux has
27 C and one startup object plus six tools. The behavior counts match the
default qualification exactly. Plan-derived checks reject the earlier wrong
Windows object split, non-integer counts and false comparison claims.

Native 580-method selections pass independently with 1,129 executions and
31 exact platform skips. Both original collectors reject a known auxiliary
receipt after successful unittest output; their failed states remain intact.
The verifier requires complete method results and accepts only that exact
trailer. Regenerated canonical audits and eight conditional checks pass
independently in a separate integration checkpoint.

Both original-source normal Cupid-only OS builds pass and produce identical
200 MiB images. Independent proof checks 429 relocatable objects, two linked
kernels, 16 artifacts and the original embedded manual. Fresh normal Windows
user builds pass the full native ABI report and reproduce the accepted `cat`,
`hello` and `ls` bytes. Guest execution and the final documented-source manual
require their own runtime acceptance.

The first ordinary Linux publication attempts fail with I/O while copying
`cupidbuild.elf`. The long collector also fails to write its final state, so
its incomplete raw record and actual closed failure are preserved separately.
Four later copy-boundary probes pass without establishing the cause. Fresh
complete default and long retries pass with the ordinary commands and real
parents. Independent verification checks all 90 controls, 23 retained files,
22 static images and six tools matching the qualified stage-four cohort per
profile. Closed producer logs report agreement for 67 author/oracle pairs;
discarded temporary objects are outside the independent retained-file check.

Both final-manual kernel preparations pass with all 431 production inputs and
three kernel outputs identical. Independent verification checks the same
68,681-byte embedded manual and measures 9,581,716-byte raw, 9,679,292-byte
pass-one and 9,810,364-byte final kernels. Each host rejects three isolated
stale limits, accepts all 16 candidate artifacts, then applies and accepts only
the three measured policy rows. The initial Windows calibration harness
rejects a WSL-root spelling before any command or mutation. The reviewed native
conversion fixes that harness boundary; both receipts remain preserved.

Both final normal Make builds finish successfully. Their original collectors
fail on kernel-file metadata retained before legitimate regeneration; those
failed receipts remain unchanged. A separate verifier requires that exact
failure and a closed successful Make command, then checks all 1,564 source
inputs per host, 431 production inputs, 16 artifacts, the selected manual and
complete disk layouts. Both 200 MiB images have SHA-256
`f7fbd52ee59cb1b92914a55e745d22baface5dc723ab28d5974edffff88f8b91`.
Both canonical audits and all four conditional checks per host pass again.

Fresh normal users pass on both hosts. Independent verification checks the
complete canonical ABI, three objects and three executables per host. Objects
match across hosts; `cat`, `hello` and `ls` retain accepted executable bytes.
The ABI still has 103 fields, 101 providers and a 412-byte table. Linux consumes
all 23 files of the verified long publication without rebuilding it, with all
90 controls matching current source. Native Windows also accepts both ordinary
publications through the unchanged supported manifest-contract CLI.

All eight serial private-image four-CPU max/e1000 boots pass: kernel disassembly
and shell commands, then separate hello, ls and cat runs on each host.
Independent verification applies the complete SMP/crypto/network contract and
Makefile dynamic PID and payload hashes to retained serial logs. It checks exact
staged FAT16 users and the 62-byte fixture plus boot/kernel placement. Source,
users and base or staged images stay unchanged. The first Windows setup runs
out of space before staging or boot; its failure remains recorded. A fresh run
checks available space and forces completed-image compression before boot-time
observations, with image bytes unchanged.

All release identities currently name the c05 working-source base, not a commit
containing these changes. New committed producer proofs remain required before
seed installation or ownership changes. Checked deterministic drift remains pending for the hosted
runtime limitations above.
