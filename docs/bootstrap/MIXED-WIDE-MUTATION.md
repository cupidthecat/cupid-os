# Mixed width integer compound assignment

The ordinary external publisher needs a four-byte i386 `size_t` destination
combined with an eight-byte captured extent. The compiler now completes that
wide integer calculation before converting and storing the narrow result.
The publisher source retains its original expression.

## Implementation

`cir_require_integer_mutation_computation` accepts the existing wide integer
computation type for a represented narrow destination. The frontend still owns
promotions, usual arithmetic conversion and assignment conversion. Existing
conversion, binary and store instructions carry the value; no IR kind or emitter
operation changes. The address and right operand are evaluated once, and the
compound expression returns its stored value.

The new normal-source stage-four compilers also build all four private
publication callers on both hosts. Independent comparison verifies every
object and eight complete same-host program pairs against the preceding
accepted builds. This removes their dependency on the older private compiler
extension while preserving the caller source. The
[publisher record](EXTERNAL-DISK-PUBLISH.md) distinguishes reused execution
evidence from fresh runs and records six strict boots of the earlier accepted
publisher images. Those images contain the preceding manual and kernel.

Byte, halfword, word, compatible enum and supported integer bit-field
destinations retain their declared store width. A nonvolatile bit field preserves
adjacent bits. Volatile ordinary integers and full-unit bit fields retain their
existing one-load/one-store rules. Atomic and boolean mutation, eight-byte shift
counts and partial volatile bit-field mutation remain unsupported.

Exactly two producer paths change: `toolchain/cupidc_ir.cc` and its header.
The four test paths are the IR and object contracts, the new execution fixture
and `tests/test_cupidc_mixed_wide_mutation.py`. ADR 0453 records the decision.

## Separate source and producer checks

`mixed-hosted-integration-source4` retains the normal 99 producer inputs and
422 source/support controls. Its snapshot is
`5a2d30853cc1e064c93a2795428c7511822f8833e03085e5e044efc895f0fe9b`.
The other 97 producer paths match qualified library source `84ae3852`.
No private CW9 host extension, publisher or alternate bootstrap coordinator
enters this cohort.

Both complete preparations pass under `mixed-hosted-prepare-{windows,linux}1`,
in 1,511.270 seconds on Windows and 1,626.093 on Linux. Conventional producers
are forbidden, with two workers and the original producer bounds.
`mixed-hosted-preparations-independent1/closed.json` checks all 291 complete
products and 97 fixed-point pairs, static profiles, actual inputs and unchanged
qualified parents.

Native and new stage-four Cupid compilers pass all ten methods per host.
`mixed-hosted-four-producer-independent1-products.json` independently checks
forty caller/method combinations, twelve native IR/object contracts, 44 rejected
compiler invocations and twelve fixture executions. Corresponding complete
compiler objects agree across all four producers; same-host linked products
agree. Tests retain their original 60-second invocation and 180-second native
contract bounds.

The execution body covers all ten compound operators, narrow widths and enums,
signedness, wide divisors/remainders, side effects, chained results, volatile
stores and adjacent bit fields. The negative matrix rejects atomic/boolean
mutation, wide counts, partial volatile fields and invalid floating bitwise
operations while retaining previous output bytes and timestamps. Existing
pointer and frame-load controls keep their full selections.

## Worktree application and remaining gates

Both complete library qualifications and their independent checks close before
application. `mixed-hosted-applied1/closed.json` records the exact two producer
and four test paths copied to the bootstrap worktree. It retains every previous
file, verifies the unchanged 97 inputs and fifteen installed seed files, and
excludes the private host and publisher extensions.

Fresh native and Cupid worktree selections pass all forty methods and twelve
native IR/object contracts. Their products use
`mixed-hosted-root-{native,checked}-{windows,linux}1-products`. The independent
worktree check, `mixed-hosted-root-independent2-products.json`, also binds actual
source, retained native compiler inputs and new stage-four execution compilers
to the preparations. Its first checker compared digest-only supplemental-source
records with size/digest objects. The corrected checker reads the existing
schema while retaining every source, output and runtime predicate; the failed
receipt remains retained.

The first isolated copies omit active X25519 and private parser fixtures; later
copies expose omitted AES and VGA inputs in existing low-level contracts. The
fourth copies add the original ATA, serial, timer and VGA sources named by those
contracts. All failed copies and receipts remain retained, and none of the
contract bodies or bounds change to bypass these missing dependencies.

The refreshed 181,453-byte manual is projected into both normal OS consumer
copies with exactly the two IR producer changes. All preceding objects,
kernels, complete images and user products are archived before projection.
Both kernel builds pass through the existing qualified parents, with
conventional producers forbidden and two workers. Windows takes 2,511.158
seconds and Linux 1,784.260. The paired cold measurement checks all 429
complete object pairs and three kernel pairs; only `04CUPIDC.o` differs from
the preceding accepted cohort. The complete manual occurs once in every kernel
product and remains nonexecuting in its wrapper.

The raw kernel measures 9,299,584 bytes. Final and pass-one ELF sizes remain
9,527,740 and 9,396,668 bytes. The measured policy projection checks all sixteen
artifact sizes and changes only the raw-kernel row in the integration and both
consumers. All 99 compiler inputs and fifteen installed seed files remain
unchanged. Evidence is `mixed-hosted-manual-kernels1-products.json` and
`mixed-hosted-manual-policy1/closed.json`. Both normal `make -j2` image builds
pass, in 2,399.016 seconds on Windows and 1,679.188 on Linux. Fresh normal user
checks and all four strict private boots also pass. Each host completes ls/SMP
and feature 17 ISO checks with four max-model CPUs, e1000, command completion
and the original 150-second bound. The accepted image remains unchanged.

`mixed-hosted-manual-os-paired-independent1-products.json` compares every byte
of all 429 objects, sixteen artifacts, six user products, fifteen installed
seed files and both complete 200 MiB images. It verifies the 103-field,
101-provider user ABI, the complete manual in every kernel and the unchanged
baseline FAT suffix. The images share SHA-256
`8869c891beafa901eeb9b9adae791831659de0b16e22683deb812a2fdce9d158`.
All 99 compiler inputs remain held. Both complete behavior qualifications now
pass as recorded below. Seed carriage and replacement-cohort OS/SDK/public
acceptance remain open. The normal disk recipe and both SDK
coordinators still use Python. TempleOS remains read-only and excluded.

## Canonical ownership audit repair

The first canonical check fails because three hosted library fixtures from
the preceding commits lack explicit source ownership entries. The policy now
lists those exact paths and the new mixed-width execution fixture as not
reached by the supported Make roots, matching the existing execution fixtures.
Their separate native/Cupid behavior proofs remain recorded; no source is
removed or renamed.

Canonical generation passes in 105.075 seconds and the subsequent check passes
in 93.306. Three focused ownership contracts also pass, retaining the missing
entry and classification-drift rejections. The independent receipt
`mixed-hosted-source-audit-repair1/closed.json` checks exactly four added policy
entries, no removed unreachable source, and unchanged compiler, seed and
generated preprocessor-case bytes. The current inventory contains 786 active
inputs, 255 features, 452 transforms and 73 accounted unreachable sources.
The first failed command remains retained.

## Committed source binding and completed qualification

Commit `acbbd8342d8901169c5484742b168277ce9bbdd1` contains the two IR producer
changes, four test paths, accepted manual, measured policy and documentation.
It is pushed to `bootstrap/cupid-self-hosting` without merging main.
`mixed-hosted-preparations-committed-independent2/closed.json` passes in
20.370 seconds. It compares all 99 source files with their exact committed Git
blobs and both retained preparation copies, without line-ending conversion.
It rechecks 291 complete products, 97 fixed-point pairs and fifteen unchanged
installed seeds, and retains an exact 232-file Linux preparation for native
Windows release authoring.

The first checker cannot launch Windows `git.exe` from WSL. The corrected checker
uses native Git to read the same committed objects from the common repository;
every source, product, preparation and seed predicate remains in place. The
original failed receipt and helper are retained.

`mixed-hosted-reviewed-release-author1` writes the paired release candidate
for that exact source revision and snapshot. Both complete qualifications pass
under `mixed-hosted-source-qualify-{windows,linux}1`, in 2,158.125 seconds on
Windows and 2,252.197 on Linux. Conventional producers stay blocked, with two
workers and the original command bounds.

`mixed-hosted-qualification-{windows,linux}-independent1/closed.json` passes
in 5.889 and 6.168 seconds respectively. Windows supplies 159 complete staged
products, 53 whole fixed-point pairs and 3,423 published files; Linux supplies
132 products, 44 pairs and 4,338 files. Together these checks cover all 291
products, 97 pairs and 7,761 files, plus the exact 99 committed producer
inputs, actual preparation copies and twelve reviewed final tool identities.
Windows retains 60 success groups, 54 rejection groups and seven help groups;
Linux retains 73, 66 and seven. Both behavior generations complete.

All fifteen installed seed files now belong to qualified `acbbd834`.
The exact source and artifact release completes installed projection and full
OS, SDK and public-bootstrap consumer acceptance as recorded below. Private UNC/discovery source is outside this qualified cohort.

[The seed candidate record](QUALIFIED-MIXED-WIDE-SEEDS.md) records the private
fifteen-file projection, paired manifest binding and fresh zero-object OS/test
consumers. Complete replacement-tool acceptance remains separate from the
completed producer qualification.

## Installed mixed width cohort, 2026-10-09

The qualified `acbbd834` pair now carries this capability in both installed
host tool sets. All four ordinary/long-path SDK profiles, complete public
bootstrap methods and both actual Make bootstraps pass independent review.
Current paired OS image and fresh user builds, all four strict boots and
complete product comparison also pass with the revised manual.
[The installed cohort record](QUALIFIED-MIXED-WIDE-SEEDS.md) binds the exact
source, release, original failures and final adoption evidence.
Private host extensions and retirement of the three normal Python
coordinators retain their separate acceptance requirements.
