# Next native user compilation boundary

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

The current Windows profile helper converts ASCII into a 128-wide-character
component buffer. Other native paths use ANSI Win32 APIs. Full user support
needs consistent Unicode conversion and length handling across preparation,
command-line decoding, transaction opening, publication, and diagnostics.
The hosted Windows startup currently reads arguments through `GetCommandLineA`.
A wide directory opener
alone is insufficient. Do not expose a restricted user command that rejects
paths already supported by the wrapper.

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
