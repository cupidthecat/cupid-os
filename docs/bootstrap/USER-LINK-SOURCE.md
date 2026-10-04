# Native user-link prerequisites

## Current-control integration, 2026-10-01

The physical `cupidbuild_link_user_object` operation now passes integration
against the adopted `1440d33f` branch state and installed `78e71bd6` seeds.
It keeps an existing physical parent chain through object capture, checked
CupidLD execution, loader validation, checked CupidDis inspection and guarded
publication. The object and output must be the approved `cat`, `hello` or `ls`
pair. Equal executable bytes preserve the output timestamp.

Both hosts pass 172 native methods covering the link operation, retained
observer, ordinary/direct CupidLD output, shared manifest reader and artifact
policy parser. Each Cupid-built caller passes fourteen link methods and all
57 observer methods. The native callers cover the three mutation-hook subcases
that the checked link callers skip. The adopted user compiler also passes all
sixteen methods on each host.

The complete 129-method audit suite first retains 128 passing methods and one
failure on each host: the new direct-output Windows branch increases the active
`_WIN32` occurrence count. The three fixture files now record 419 `#if`
occurrences, 438 total expressions and 189 occurrences of `defined(_WIN32)`.
The 61 unique expressions, nineteen `#elif` occurrences and 64 directive/token
pairs remain unchanged. Four affected audit methods, all 39 native preprocessor
methods and the same 39 methods in Cupid-built callers pass on both hosts.
Both final generated audit checks pass. The audit also pins `CREATE_ALWAYS`,
ordinary/direct dispatch, duplicate-option rejection and direct-writer sharing.

Evidence is retained under
`build/bootstrap/native-profile-validation-258bb5f3/` in the bootstrap checkout.
The `user-link-integration-{windows,linux}-v1.json`, `v2.json` and `v3.json`
records retain the original results; the failed v2 audit is not relabeled.
`user-link-integration-independent-verification-v2.json` independently rereads
the six completed records, their logs, checked preprocessor artifacts and all
1,549 current inputs. Its first verifier invocation mistakenly treats the v3
CupidLD link as a unittest command; the separate rejected record retains that
harness error. The corrected verifier checks command roles explicitly.
These integration checks precede the settled documentation and manual.

The final source checkpoint passes both normal OS builds with host
code-producing tools forbidden. Independent verification rereads 1,549 source
inputs per host, sixteen exact artifacts and 431 link inputs. Both hosts retain
the preceding user executable bytes and pass the platform ABI, private
four-CPU max/e1000 kernel smoke and three separate external-program boots.
All eight boots preserve their base or staged images and check SMP runtime.
The settled 65,853-byte manual is embedded in both matching kernels. Their raw
size is 9,578,888 bytes; pass-one and final ELFs remain 9,675,196 and 9,806,268
bytes. Both 200 MiB images have SHA-256
`784d906d6a122e7a600a0a8a1fe0022e7cfa7a291f959605c0898f37d25a80db`.
Only the measured raw-kernel policy row changes, after both initial builds
reject the preceding value.

Linux also passes complete default and long-profile publications: 89 control
inputs, 76 or 77 producer inputs, 22 artifacts and 67 matching contract pairs.
The first default command succeeds before its evidence wrapper reads the wrong
report key. The first Linux acceptance driver still names an obsolete
publication record and fails before any user, ABI or boot child starts. Its
corrected driver reads the completed long-profile record and reruns acceptance.
Both failed wrappers remain separate from passing acceptance and publication
records. Independent recovery checks
the completed publication, and a separate long-profile run passes.
`paired-user-link-source-independent-verification-v2.json` binds the final
builds, users, publications, manual and all eight boot records.

This source checkpoint does not expose `link-user` in the CLI or change the
three Python user-link recipes. Installed seeds and ownership remain at the
accepted 444 CupidBuild/eight Python actions. Alias resolution, exact Windows
profile carriage, staged behavior, a new paired fixed point and production
handoff remain required. The following sections retain earlier development
checkpoints and failed approaches.

The separate user linker will use an existing output directory. Source head
provides `cupidbuild_host_output_parent_open_existing` for that requirement.
It validates the whole normalized relative output before opening the root,
then retains every directory without creating directories, files or locks.
Missing parents, file collisions, linked components and unsafe names fail.
The caller must close a partial result after failure.

The opener shares the retained-chain validation and transaction binding used
by compilation. Attach it through `cupidbuild_host_output_transaction_open`
before lock acquisition and input capture. Windows prevents retained directory
replacement; Linux rechecks each ancestor identity and parent/name binding.
Sibling writes remain valid. Closing the transaction precedes closing the
borrowed chain. Equal validated publication can preserve an output timestamp.

The normalized API rejects directory links. The Python user-link wrapper also
accepts internal aliases after resolving their existing parents. A future
native link coordinator must resolve those aliases before calling this opener,
check the canonical source/output pair, and retain that physical chain through
publication. This prerequisite does not implement alias resolution, CupidLD
execution, CupidDis inspection or the complete `link-user` operation.

## Executed checks

All 57 observer methods pass in four caller configurations. Native Windows
takes 12.011 seconds with five expected platform/runtime skips. Native Linux
passes with two expected skips. The CupidC-built Windows caller takes 67.662
seconds with four expected skips; the CupidC-built Linux caller takes 53.314
seconds with one expected skip. Both checked callers are built by the installed
CupidC, CupidASM and CupidLD tools.

Eight new methods check opening without namespace changes, missing chains
without creation, unsafe names and foreign file collisions, Unicode paths
beyond 260 characters, binding before transaction opening, transplanted
ancestors, linked parents, and publication with equal-output timestamps.
The whole suite also retains the existing creation, observation, concurrency,
input mutation and cleanup checks. The checked and Linux native records bind
five source/test inputs before and after each run.

Evidence is under `build/user-link-existing-parent-v1/` in the isolated source
checkout: `native-linux-v1.json`, `checked-windows-v1.json`,
`checked-linux-v1.json` and their logs. Native Windows retains its terminal
test result. The source step has not changed installed seeds, Make recipes or
production ownership. Integration and OS acceptance remain pending.

## Alias-resolution implementation boundary

The next adapter must resolve an existing repository and existing input/output
parents while rejecting linked leaves. The core will approve the resulting
physical source/output pair, then attach the retained physical directory chain
before creating a transaction. Compare the opened root and parent identities
with the resolution handles so a replacement between resolution and retention
cannot become a different publication baseline.

On Windows, `GetFinalPathNameByHandleW` supplies a resolved name for an opened
directory. Microsoft documents its normalized DOS result, extended prefix,
capacity rules and SMB permission failures in the
[API reference](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew).
The current CupidBuild checked profile does not import this function.
Adding its checked declaration and calling-convention bridge therefore also
requires an exact new import profile, historical profile compatibility and
paired bootstrap evidence. Broadening the existing import allowance would
hide a mixed cohort. The existing NTDLL imports include `NtQueryDirectoryFile`;
they do not include `NtQueryInformationFile`.

Linux can obtain the resolved names of opened directory descriptors through
`/proc/self/fd`. That approach still needs checked i386 syscall handling,
bounded name checks, retained identity comparison and live filesystem tests.
Neither platform resolver is implemented by the existing-directory opener.
UNC-root handling and aliases in repository ancestors need explicit coverage
before a complete user-link recipe transfer.

## Physical user link operation under validation

The isolated `cupidbuild_link_user_object` C API accepts a normalized physical
root and object/output pair. The two leaves share an existing directory below
`user/`; the object is the executable name plus `.o`, and the executable name
is `cat`, `hello` or `ls`. It opens the retained physical parent chain before
the lock and input capture. It creates no output directories.

The operation validates the captured relocatable object, freezes all six tools
and their manifest, links at `0x01C00000` with `_start`, checks the resulting
ELF against the existing user loader's segment and arena rules, and runs
CupidDis with known-instruction, local-target and code-anchor requirements.
The tool runner already rejects unexpected stdout and stderr. Candidate
identity and bytes, live and private inputs, seed membership, directory chain,
destination and lock are rechecked before atomic publication. Equal validated
bytes retain the previous output timestamp.

The first ten-method runs fail six cases on each host. The standalone CupidLD
publisher tries to replace the caller-retained candidate identity; its temporary
file naming also cannot publish beside a Linux descriptor path. A minimized
single-example reproduction fails on both hosts in about seven seconds.
The fix follows ADR 0381's assembler protocol: CupidLD accepts a single
`--caller-owned-output` flag and writes directly through the retained candidate.
Its ordinary invocation keeps the existing atomic publisher. Duplicate flags
fail before mutation. Windows direct output uses the mutable read/write/delete
sharing profile and preserves the enclosing caller's handle identity.

Further failed runs expose a native Windows test caller that passes ANSI text
to the UTF-8 adapter, a missing checked `CREATE_ALWAYS` declaration, and an
oracle attempt to use a private development linker with independently pinned
release constants. The caller now converts its wide arguments and uses the
native UTF-8 adapters; the shared checked header declares the existing Win32
constant. Parity uses the unchanged installed linker as the oracle. Its release
pins are preserved. The development cohort replaces only CupidLD in its private
manifest and validates that tool's actual bytes and unchanged exact imports.

All thirteen native operation methods pass on both hosts: 119.311 seconds on
Windows and 99.866 on Linux. They compare all three real executable bytes,
check long accented paths, timestamp preservation, malformed/missing objects,
missing entry, excessive arena, seed digest and membership drift, foreign locks,
hard links, linked leaves and parents, pre-launch input mutation, post-install
rollback, unknown instructions, nonlocal branches and duplicate flags.
The CupidC-built caller also passes all thirteen methods on each host: 568.403
seconds on Windows and 408.003 on Linux. Each checked run skips the three
mutation subcases that require native test hooks. The native runs cover those
subcases. All four `build/user-link-operation-v3/` records retain the same eight
source/test inputs before and after execution; an independent reread confirms
the four records and current files agreed at the v3 checkpoint.

The complete native CupidLD suite passes all eighteen methods on Linux and
passes on Windows with the one Linux descriptor case skipped. Three new methods
check ELF/PE byte parity and retained identity, duplicate-option/link-error
preservation, and inherited descriptor output. The existing standalone atomic
publication fault and recovery checks still pass.

## Absolute physical-root enforcement

The physical C API promises an absolute root. A new regression exposes a
Windows gap: both an ordinary relative root and a drive-relative root are
resolved against the current directory, accepted and used to replace the prior
executable. The one-method Windows red run fails both subcases in 68.409 seconds.
Its record is `build/user-link-relative-root-red-windows-v1.log`.

The operation now checks the host's absolute root syntax before opening parents
or creating transaction state. Windows requires an alphabetic drive, colon and
root separator; Linux requires a leading slash. The existing path validator
still rejects unsafe components. The higher-level alias adapter remains
responsible for resolving caller spellings before entering this physical API.

The new negative method checks ordinary relative roots on both hosts and
drive-relative roots on Windows, requiring the previous executable bytes,
timestamp and namespace to remain intact. The first complete Linux run passes
all fourteen methods in 70.018 seconds. The first Windows logging invocation
prints fourteen passing methods but its PowerShell stderr redirection reports
a nonzero shell status; it is not an accepted completed harness record.
Four fresh Python process runners now capture actual child return codes and
freeze the eight source/test inputs around native and CupidC-built selections.
All four v4 selections now pass fourteen methods: native Windows in 87.134
seconds, native Linux in 76.173, CupidC-built Windows in 378.229 and CupidC-built
Linux in 282.303. Each checked selection skips the three native mutation-hook
subcases; both native selections cover them. Independent verification rereads
all four completed records and test logs and verifies the same eight current
source/test inputs. Records and `independent-verification-v4.json` are under
`build/user-link-operation-v4/`. Seed, alias, CLI and Make acceptance remain
separate.

This API does not resolve filesystem aliases and is not exposed by the CLI or
Make. It remains isolated from the active bootstrap branch, installed seeds
and production ownership while the preceding user compiler release consumes
its own frozen producer. Alias resolution, staged behavior coverage, clean
paired proof, seed promotion and real Make/OS acceptance remain required for
the complete user-link handoff.
## Exact user-compiler parent compatibility

The isolated shared manifest reader and independent artifact-policy parser now
admit the exact `78e71bd6137042720c378d2c596aa40b153dad11` parent tuple:
Linux manifest `b6f247af2034d7432333eed74230452fede2198ba744c30a5c410ce19c4b79b4`
and Windows manifest
`1d40ec6e03bdd736e5993f8a204588f0e376541f4019b83bd00650469b9531bd`.
This permits the next source-built generation to describe those tools as its
parents under the existing 76-input default or 77-input long-file profile.
Historical tuples retain their separate checks. Revision/digest mixtures,
partial Windows execution/plan substitutions and mismatched counts or profiles
remain invalid. The change admits one reviewed tuple, without learning parent
identities from an untrusted manifest.

The two new regressions first fail on native Windows: the shared reader reports
`fixed-point provenance differs`, and the policy reader reports
`seed manifest parent provenance differs`. The full native selection then
passes 83 methods on each host. Both CupidC-built manifest callers match their
native oracle on 1,421 structural cases and fifteen pair cases. Both checked
policy callers pass all 56 methods and match the native result, output and
diagnostic on 352 requests. This adds 54 structural and 32 policy cases.

The first native export harness passes its tests but records no manifest corpus
because recording was not enabled. Those empty exports remain separate from
the nonempty v2 replay. The first independent raw-byte comparison also rejects
Windows text-mode CRLF exports. A byte-level probe shows that every case row
matches after CRLF-to-LF conversion. The corrected verifier retains both raw
hashes, rejects remaining carriage returns and compares the canonical rows
exactly. It does not rerun or relabel the rejected check.

Evidence is under `build/user-link-parent-compatibility-v1/` in this isolated
checkout. `independent-verification-v3.json` rereads the four changed source/test
files, both native results, checked source copies, objects, executables, seed
payloads and result streams. Both hosts retain the same four input hashes.
The preceding `v4` link-operation checks remain their original checkpoint.
This compatibility step does not establish a new fixed point, install seeds,
resolve filesystem aliases, expose `link-user`, or change normal Make ownership.
