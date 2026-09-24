# ADR 0196: Transfer the Toolchain contracts to CupidC

## Status

Accepted on 2026-07-30.

## Context

The normal OS build already ran CupidC, CupidASM, CupidDis, CupidLD, and
CupidObj from the checked i386 Linux seed. The Toolchain test build was a
different story: fourteen contract programs still used `.c` names and were
compiled and linked as native executables by GCC or Clang. That left 52 host
C transforms in the supported build graph even though CupidC could emit most
of the same source.

Compiling the full contract sources exposed three real gaps. A block-static
pointer could not be initialized with the address of another block-static
object. Local `long double` values could not move through Linear IR or the
i386 x87 emitter. A static initializer could not reuse the direct integer
initializer of an earlier non-atomic `const` object. The hosted runtime also
lacked the small standard-library surface used by the unchanged contracts.

Changing the contracts to avoid those forms would have hidden the compiler
work that self-hosting is meant to uncover.

## Decision

Rename all fourteen Toolchain contract sources from `.c` to `.cc`. Their
native builds remain available as the explicit `native-oracles` target, with
the compiler forced into C mode for the new suffix. They are no longer
reachable from `toolchain:all`.

Make the normal Toolchain target build one checked cohort. The new harness
starts from the manifest-bound i386 Linux seed, completes the existing
stage-two and stage-three tool bootstrap, and then uses each stage's CupidC
and CupidLD to build the fourteen contracts. It also rebuilds the hosted
runtime contract. All fifteen contract executables must be valid static i386
ELF files. The sixteen newly compiled contract, runtime, and adapter objects
and all fifteen executables must match across the two stages byte for byte.

The harness accepts only a dedicated `cupidc-contracts` output directory
inside the source tree. It validates that target before any build work and
again immediately before promotion. An existing destination must already
verify as a complete cohort. An arbitrary directory, source directory, file,
or symbolic link is rejected without modification.

The harness takes an exact 45-input snapshot, copies it into a private source
root, and reconstructs the complete frozen inventory there. The inventory
includes the Toolchain Makefile, the fixed-point bootstrap module, and the
cohort builder itself. Membership and hashes must match the initial snapshot.
Live checks discover the input set again, so an added or removed header fails
instead of escaping an older key list. A transient edit copied into the
private root also fails even if the live file is restored later.

The public manifest also binds the checked seed manifest, build-plan digest,
and complete 41-file fixed-point source inventory recorded by the staged
bootstrap. Before publication and before every later verify or run, the
harness validates the seed and reconstructs that inventory from its checked
build plan. Size and SHA-256 differences, added or removed headers, and
backdated changes to an implementation source, `start.asm`, or `link.ld` all
fail. Manifest hashing, JSON decoding, schema validation, and build-plan use
share one captured byte sequence. Replacing the manifest during validation
cannot pair one digest with a later read's plan. `link.ld` is also an explicit
Make prerequisite.

The harness publishes the fifteen contracts, five refreshed tools, and one
manifest as a complete directory. A failed compile, link, comparison, runtime
check, or input check leaves the previous cohort untouched. If directory
promotion fails after the old cohort moves aside, the publisher restores it.
If restoration also fails, the error reports both failures and the exact
recoverable backup path. Failure to remove an obsolete backup cannot turn a
completed publication into a false failure. The runtime contract runs before
publication and checks its exact standard output, file output, exit status,
and error paths.

Every normal Toolchain entry point verifies the published schema, target,
fixed-point record, input count, exact artifact inventory, sizes, hashes, and
stage-comparison hashes. It rehashes the live 45-input contract set and the
41-file fixed-point source inventory, then validates the recorded seed. This
rejects a publication built from different source even when timestamps would
otherwise leave the manifest target up to date. A contract run derives its
cohort from the requested executable, requires a named manifest artifact,
verifies the whole cohort and its hashes, and compares both recorded
inventories with the live source before execution. The build gate does not
claim to execute every contract mode before publication. The complete
behavior gate is `make -C toolchain test`; it runs after publication passes
verification.

Keep the hosted include request narrow. Thirty-one ordinary strict roots
search only the Toolchain tree and the angle-only i386 Linux declarations.
`kernel/lang/as_elf.cc` and its Toolchain contract use a separate bridge
profile that also searches `/kernel/lang`. The bridge exists because both
sources include `as_elf.h`; it does not make kernel-private headers visible
to the rest of the hosted cohort.

Extend CupidC instead of narrowing the sources:

- Static initializer metadata can name a block-static symbol, and ELF
  emission resolves it through the existing local-symbol path.
- Non-atomic automatic `long double` values use twelve-byte target objects
  and x87 80-bit memory loads and stores. Conversions among `float`, `double`,
  and `long double`, unary plus and minus, and addition, subtraction,
  multiplication, and division share the floating value path. Direct and
  indirect fixed, variadic, and unprototyped arguments occupy twelve cdecl
  bytes. A function returns `long double` in x87 `ST0`, and direct or indirect
  callers store that result in a twelve-byte snapshot. `va_arg(long double)`
  uses the same width and advances to the following four-byte argument.
  File-scope and block-static non-atomic scalars, fixed arrays, and complete
  records may contain `long double` leaves. Implicit initialization zeros the
  complete object; an explicit scalar leaf accepts an integer constant
  expression equal to zero. Each leaf occupies twelve zero-filled BSS bytes,
  while its enclosing array or record keeps the ordinary i386 layout. Atomic
  leaves are rejected recursively, but a pointer to an atomic long double
  does not initialize its referent. Long-double literals, nonzero or floating
  static initializers, `long double` comparisons, and integer conversions
  involving `long double` remain outside this slice.
- A static initializer may reuse the direct integer initializer of an earlier
  file-scope or block-static non-atomic `const` integer. Mutable, automatic,
  atomic, indirect, and non-integer cases still fail as constant expressions.
  This is an intentional Cupid C extension, not an ISO C integer constant
  expression. It preserves the unchanged address-table idiom in
  `atomic_oracle_execute`, which strict GCC and Clang already fold.
- The hosted declarations and runtime provide `stdint.h`, `NULL`, `EOF`,
  `printf`, `puts`, `snprintf`, `fputc`, `fputs`, `memmove`, and `strstr`.
  The formatter handles signed and unsigned `long long` values, padded
  64-bit hexadecimal output, and fixed or argument-supplied string precision.
  Their checked error paths include bad streams, invalid formats, truncation,
  overlap, and missing substrings.

The checked seed itself is not replaced by this decision. It remains the
reviewed bootstrap root; the cohort records the refreshed stage-two tools
that it produced from the current source.

## Evidence

The regenerated active-source audit contains 717 inputs, 449 transforms,
252 feature groups, and 25 unreachable sources. Its C preprocessing contract
covers 381 tracked translation units and four generated units, with no
deferred hosted unit. The strict hosted set is divided into 31 ordinary roots
and two kernel-bridge roots, followed by two GNU runtime roots for the
implementation and behavior probe. Across the
root image, user build, and normal Toolchain build, 245 transforms involve
CupidC, 449 involve Python orchestration, and none involves a host C
compiler. The Toolchain graph has three Python
orchestration transforms: directory creation, the checked cohort, and fast
manifest-backed artifact verification.

Focused frontend contracts cover successful block-static address
initialization and reject invalid symbol categories. Long-double contracts
pin the exact 80-bit `FLD` and `FSTP` forms, unary plus and minus, all four
arithmetic operators, local Linear IR, twelve-byte fixed and variadic
arguments, direct and indirect unprototyped arguments, direct and indirect
results, returns, `va_arg(long double)`, object size, instruction inventory,
and deterministic object fingerprints. Four file-scope and block-static
long-double scalars occupy 48 exact BSS bytes. Their six absolute relocations
exercise reads and writes without initialized data. A separate aggregate
object pins 104 BSS bytes for two arrays and two records, a 415-byte access
function with fingerprint `BF01CC71`, eight absolute relocations, and six
symbols. The static i386 runtime checks both result paths, both unprototyped
paths, a four-byte argument after a long-double variadic slot, the four
scalars, 24-byte arrays, and 28-byte records. It moves 1.5 through file and
block aggregate leaves after proving every leaf and marker starts at zero.
Negative cases reject atomic, nonzero, and floating static forms.
Static constant contracts cover chained scalars, arrays, and nested records at
file and block scope while retaining negative checks for mutable, automatic,
and atomic objects. Publication contracts reject unsafe targets, incomplete
existing destinations, frozen or live inventory drift, unlisted run targets,
unknown link-object keys, artifact drift, and a seed-manifest replacement
during validation without changing the destination. The hosted runtime
contract checks the new library calls, the exact wide-integer and
bounded-string forms used by unchanged diagnostics, and their useful
failures. The cohort records and checks sixteen object hashes as well as the
fifteen executable comparison hashes.

Review found that generic semantic zero initialization already admitted a
static long-double object while this record still called every static form
unsupported. The supported boundary now makes that behavior explicit and
tests it at file and block scope. A second pass found that implicit zero for
an array or record could hide an atomic long-double leaf. The initializer now
walks aggregate types and rejects such leaves, with separate file-array and
block-record negative cases. The same walk runs before an explicit static
initializer, because `{0}` can omit a later record member. A partial-record
negative pins that path. A positive case keeps a static pointer to atomic
long double valid because the pointee is not an initialized subobject.

A final specification pass found that the implementation already accepted
non-atomic long-double leaves inside static arrays and records, while this
decision still described scalars only. That behavior is now explicit rather
than removed. Frontend tests cover implicit arrays and explicit record leaves
at file and block scope. The separate ELF proof fixes their complete BSS
layout and every relocation, and the hosted i386 runtime checks both the
initial zero state and value transport through those leaves. The same pass
removed a token-spelling shortcut that mistook the type name in
`sizeof(float) - 4` for floating arithmetic. Static long-double leaves now use
the integer constant-expression parser directly: that expression succeeds,
while a genuine `1.0L` initializer still receives a focused diagnostic.

The first complete checked run compiled all fourteen Toolchain contracts with
stage-two CupidC before finding a missing public `EBADF` declaration in the
separate runtime contract. Adding the target ABI value to the hosted
`errno.h` let the unchanged contract compile. This failure was useful: it
proved that the cohort does not silently borrow the host system headers.

A later format-string audit found `%lld`, `%llu`, `%016llx`, and `%.*s` in
the unchanged contract programs. The first focused Cupid-built runtime probe
stopped at the new wide-format check, as expected. The widened formatter then
passed signed minimum, unsigned maximum, padded hexadecimal, zero and
argument-supplied string precision, negative precision, and invalid `ll`
usage under WSL.

The full audit then found a stale lexical lock: the graph contained 5,333
active `sizeof` expressions instead of 5,330. A word-level comparison found
exactly three additions and no removals, one in each of the frontend, Linear
IR, and object contracts. Five source-control locks were stale for the same
reason. Their changes were accounted for file by file before the expected
totals were updated. The complete audit module then passed all 67 tests in
548.970 seconds. The later runtime `sizeof(long double)` check brought the
final checked count to 5,348 across 168 files.

The first published candidate passed its fixed-point and runtime gates. The
complete behavior suite then found one contract assumption that depended on
the host data model. A 256-byte output limit reached line 2 in the emitted
i386 contract but line 1 in the native 64-bit oracle. The diagnostic code,
rollback state, arena mark, allocator balance, source tape, and recovery were
otherwise identical. A read-only breakpoint probe identified the line check
as the first failed predicate and measured line 2, column 17. A 224-byte limit
keeps the intended line-1 failure on both targets, so the portable fixture now
uses that value. No compiler or runtime behavior changed to mask the
difference.

The next publication review found that the contract manifest retained only
the hash of the staged bootstrap report. Its 42-file contract inventory was
complete, but the report's separate 41-file fixed-point inventory was not
available to normal verify or run commands. A backdated change to
`cupidc_emit.cc`, the hosted runtime, startup assembly, or `link.ld` could
therefore pass after Make skipped the build. `link.ld` was also missing from
the manifest's Make prerequisites. That candidate was stopped before
publication.

Manifest schema v2 carries the checked seed path and hash, build-plan digest,
and complete bootstrap input inventory. Verification validates the seed and
uses its checked build plan to rediscover the current closure. One
`SeedInputs` value carries the decoded manifest, digest, and verified tool set
derived from that single byte capture. Focused tests change `cupidc_emit.cc`,
`start.asm`, `link.ld`, and the seed manifest while restoring each original
mtime. All four fail before a contract runner is created. Another test
replaces the seed manifest immediately after its only read and proves that
the recorded digest and build plan still come from the captured bytes. The
earlier focused run covered 29 harness and seed tests. Those tests also inject
promotion, restoration, and backup-cleanup failures, so the last-good and
recoverable-backup behavior is executable evidence.

A final control-plane review found three timestamp-only inputs outside both
inventories: `toolchain/Makefile`, `tools/bootstrap_toolchain.py`, and
`tools/cupidc_toolchain_contracts.py`. They now belong to the contract
inventory, which grows from 42 to 45 while the compiled fixed-point source
closure stays at 41. Focused tests require all three and backdate a byte
change in each one. Verification rejects every change before constructing a
contract runner.

The final replacement cohort built and published in 2,655.7 seconds. Its
`cupid.toolchain-contracts.v2` manifest reports 20 artifacts, 45 contract
inputs, and 41 fixed-point source inputs. The manifest has SHA-256
`0bc87715cf03d291e084974d088183c910835cf383f922ec0aa4d0e124f416ea`.
Its fixed-point source inventory has SHA-256
`079ed8c0688ddf683ed112e0bee5c895da47e7143096dcdcc98b2ec425127999`;
the checked seed manifest is
`98dd40674aa42f0fc52689dfe22d459d78c9b2374f7110f83727e5da12321939`,
and the build plan is
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
Both CupidC generations produced byte-identical objects and executables, the
hosted runtime passed, and the publisher rechecked the live inputs before
installing the cohort.

The repository-wide test replay exposed stale evidence around that
publication, not a generated-code mismatch. Absolute `R_386_32` addends had
become valid static-subobject selectors, while an older frontier test still
expected every such addend to be zero. The frontier now accepts that useful
case and separately rejects a non-`-4` `R_386_PC32` addend. The same replay
refreshed the 445-input kernel snapshot and 49 `boot.asm` witness locations in
the x86 manifest. All 187 mode-specific witnesses retain their signatures and
encoded bytes.

The first review of the automatic long-double slice found that its accepted
surface was wider than its proof. The frontend admitted long-double function
results and unprototyped arguments, but the contracts described only fixed and
variadic transport. The reviewed slice now emits returns through `ST0`, stores
direct and indirect call results in twelve-byte snapshots, and tests direct
and indirect unprototyped calls. The focused frontend, Linear IR, object, and
static i386 runtime cases all pass.

The focused frontend and Linear IR modules pass all 175 tests. The object and
fixed-point module passes all 106 tests in 859.704 seconds, including the
five-tool static fixed point and the exact scalar and aggregate long-double
objects. The arithmetic
fixture has 753 text bytes and fingerprint `82A5F459`; fixed calls have 220
bytes and fingerprint `EDB702DD`; the variadic caller has 99 bytes and
fingerprint `1FED7CA7`; and the variadic reader has 176 bytes and fingerprint
`6296CE84`. Returns add 39 bytes with fingerprint `46BAFE97`.
Unprototyped calls add 176 bytes with fingerprint `A3C2BF25`, and returned
values add 278 bytes with fingerprint `69385492`. The complete fixture has
1,955 text bytes, fingerprint `282CA98B`, eleven relocations, and sixteen
symbols. Static-zero access contributes a 214-byte function with fingerprint
`5B39C697`, four twelve-byte BSS symbols, and six absolute relocations.
The separate static-aggregate object adds a 415-byte function with fingerprint
`BF01CC71`, 104 BSS bytes, eight absolute relocations, and six symbols.

The expanded emitter and frontend also change their deterministic self-host
objects. The emitter now has 353 functions, 541,569 text bytes, a 608,624-byte
object, and fingerprint `2ABA8014`. The frontend has 422 functions, 846,845
text bytes, a 1,007,060-byte object, and fingerprint `87F6E1B5`. A complete
static frontier run needs 928 seconds on the Windows and WSL test host. Its
Make recipe therefore has a case-local 1,800-second budget. Rebuilding and
linking the five static tools plus the runtime contract needs 1,048.8 seconds
and uses the same scoped budget. Every other contract keeps the 900-second
default.

The strict kernel frontier also compiles all 155 production sources twice
against a 445-input snapshot. Both object sets are byte-identical and total
3,717,856 bytes. The snapshot SHA-256 is
`e28b1024edc5361d99583f79f65ce43690ebc873f04b568837f57f8af5df5db7`.

The final repository-wide replay passes all 1,004 tests in 3,884.554 seconds,
with two optional skips. The complete Make target returns in 3,980.5 seconds
after rebuilding generated installation sources, checking the user ABI and
frontiers, passing the GUI terminal runtime smoke, and rechecking the
canonical audit.

The normal image build completed in 508.2 seconds. It produced an
8,600,676-byte pass-one ELF with SHA-256
`c62830037528b29d470a8266f37bd9131ce18ab20df4896727109e5dd8783caa`,
an 8,711,268-byte final ELF with SHA-256
`893185668ce0282f1e57efed1c3224404c04a2a1a87393c17281610cc141c50a`,
and an 8,510,856-byte raw kernel with SHA-256
`5bd12f137dbbbba30bff4d3fe2b95e1727379b2ece1aaffc6a96cb2dc4416d5a`.
The 209,715,200-byte preboot image has SHA-256
`5589c5cc151c486a85efaffc3551b37ec4f733ebd57f78c863c5bf6b96c7e23d`;
the complete raw kernel matches its bytes from offset 2,560.

## Rejected alternatives

Keeping native contracts on the normal path was rejected because it preserved
the last host C transforms in a supported build root.

Renaming the files without changing their build ownership was rejected
because a `.cc` suffix is an ownership claim, not a cosmetic change.

Compiling only a smaller smoke program with CupidC was rejected because it
would leave the detailed frontend, IR, object, assembler, linker, object-tool,
and disassembler contracts outside the self-hosting pressure.

Publishing each executable as soon as it linked was rejected because a mixed
cohort could combine artifacts from different compiler generations or source
snapshots.

## Consequences

`make -C toolchain all` no longer needs GCC, Clang, or a native linker. On
Windows it still needs WSL to execute the static i386 tools, and Host Python
still coordinates the checked build. A native Windows fixed point and a
Python-free bootstrap remain open.

Developers can still run `make -C toolchain native-oracles` when they want a
native comparison. Those binaries are optional evidence and do not count as
normal ownership.

This decision does not enable the proposed 20 percent output-quality ceiling.
The repository has no approved cohort, metric, oracle producer, or
same-revision host-oracle artifact for that comparison. The older Windows and
Linux host `.text` measurements differ by 22.73 percent for the same
revision, so selecting either one without a separate decision would not make
a trustworthy gate. Existing linker capacity assertions remain independent
safety checks.

All transferred contract sources now use `.cc`. Remaining `.c` files keep
their names until CupidC actually owns their active build path.
