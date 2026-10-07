# Retained previous-output prototype

Date: 2026-10-07

The separate prototype gives a guarded transaction one retained previous-output
input. It copies the complete file through fixed 64 KiB blocks and records its
whole SHA-256, within the transaction's explicitly selected capacity. The API
and its further emitter work remain outside the private integration source.
The installed `2d04ff25` cohort has its own qualification and acceptance record.

## Request and lifetime

`cupidbuild_host_freeze_previous_output` accepts the transaction, a safe private
name, and frozen-path and snapshot result pointers. An absent previous output
is valid and clears both results. A second capture, unsafe private name or
runner-only transaction rejects the request and poisons publication. The
transaction retains the original Windows handle or POSIX descriptor through
capture and cleanup. Before accepting the copy, it checks metadata, complete
content and the public path binding.

After a successful replacement, the frozen previous file remains an owned
input. Its source and frozen content checks still apply. Ordinary input
freezing continues to reject the public output and its hardlink aliases. The
candidate and previous-output capacity accepts 1 through `INT_MAX`; requests
outside that range reject before publication. Payload-returning reads keep
their existing 64 MiB limit.

## Verification

The sealed compiler/API source has 1,697 inputs. Four actual i386 caller builds
pass all twenty compile, object-certification and link phases through native
and prepared CupidC producers on both hosts. Corresponding objects and programs
agree within each host. Independent rereading checks every build component and
the executable profiles in
`result-previous-output-checked3-four-producer-independent.json`.

The four complete runtime selections pass 114 cases. Independent rereading
checks 98 complete previous-file copies, every final output, source preservation,
namespace cleanup, original 600-second case limits and both Linux 32 MiB
controls. Cases cover empty files, SHA padding and copy-block boundaries,
absence, changed publication, equal reuse, repeated capture, unsafe names,
runner transactions, output aliases, source and frozen drift, capacity excess,
the ordinary payload limit and complete 200 MiB files. Evidence is
`result-previous-output-runtime4-four-producer-independent.json`.

The normal regression module adds one file to that source, producing a sealed
1,698-input copy without changing any C/API byte. Its complete native Clang
runs pass 35 executed methods with one declared Windows memory-limit skip.
Independent rereading checks 65 invocations, 49 complete copies and the Linux
fixed-memory case in `previous-output-regression4-native-paired-independent.json`.

The twelve normal small-case methods also pass through all four retained i386
callers: 48 methods, 108 invocations and 76 complete copies. Each caller checks
all five invalid or exceeded capacities, including zero and `UINT64_MAX`.
The original assertions and case limits remain unchanged. Six large-case
methods are explicitly excluded from this selection; the separate complete
runtime record above owns their i386 evidence. The small-case receipt is
`previous-output-regression5-checked-four-producer-independent.json`.

Fresh ordinary publication regressions use the actual new API objects. Across
all four producers, 382 methods pass with fourteen expected host skips among
396 selections. Independent rereading checks 88 snapshot invocations and sixty
complete copies in `result-publication-regressions1-four-producer-independent.json`.
Both strict audits pass all twenty contracts, and 26 existing positive and
negative ownership controls pass. The CPP inventory stays at 422 tracked roots,
four generated roots and 53 strict hosted roots, with matching profiles between
hosts. Evidence is `result-previous-output-audit-ownership4-paired-independent.json`.

## Failed attempts and remaining work

The 1,698-input source compiles every one of the 156 kernel sources twice
through all four producers. All 1,248 complete objects agree. Compared with
the accepted scalar emitter, 153 objects contain 14,260 same-address register
transfers and three objects are unchanged. Every other byte, symbol, relocation,
addend and branch destination remains checked. Evidence is
`result-kernel-frontier4-four-producer-independent.json` and
`result-kernel-frontier4-instruction-review.json`.

The complete ordinary corpus has 307 matching object pairs: 160 objects contain
1,214 reviewed register transfers and 147 are unchanged. A separate direct
capture reaches 244 of those objects and agrees with the complete diagnostic
capture. That diagnostic keeps the accepted emitter's output for the original
assertions and emits a separate further-emitter object from the same unit.
It supplies object review, while actual further-emitter controls retain their
own failures. Evidence is
`result-paired-object-dump5-complete-instruction-review.json`.

The actual Cupid-built ordinary callers each execute all 58 unchanged modes,
with 35 passes and the same 23 failures. Most first failures concern encoding
expectations. The block-enum control instead reproduces a real object-equivalence
regression: returning one enum parameter as `int` differs from the ordinary int
form. A targeted probe shows that a validated enum-to-int conversion emits no
bytes but interrupts the optimizer's IR adjacency test. A separate correction
looks past only validated word conversions with no emitted bytes and no branch
entry. All 23 native frame, rotate and transfer methods pass on each host,
including enum equivalence, incomplete enum rejection and conversion-entry
runtime checks. Fresh actual CupidC derivatives also pass all 23 methods on
each host. Their next two emitter generations agree: 629,828 bytes, SHA-256
`273cd8eda39d87c5f1ebbab836573c2a457bfd33ad0f11f8385fa501e69e957f`.
Removing only the intervening conversion-entry guard changes the retained
branch target from POP EAX to the middle of a MOV instruction. Both strict
disassemblers reject the mutant. Evidence is
`result-enum-transfer-controls5-four-producer-independent.json`.

The corrected complete ordinary review checks 307 pairs. Of these, 162 objects
contain 1,325 same-address register transfers and 145 are unchanged. The direct
caller reaches 244 matching objects and its unchanged block-enum mode passes.
Evidence is `result-paired-object-dump6-complete-instruction-review.json`.
The final expectation proposal binds individual arrays to their actual fixture
symbols and whole-text assertions to complete reviewed sections. Quotient and
remainder keep separate strict mnemonic lists. All 10,268 original source and
diagnostic strings, semantic predicates and original bounds remain. Both native
callers pass all 58 ordinary modes, and all 307 complete outputs match the
reviewed objects. Evidence is
`result-object-proposal9-native-paired-independent.json`.

Both fresh actual Cupid-built ordinary callers now pass all 58 modes under the
original bounds. Independent rereading checks complete source, producer,
component, executable and stream bytes in
`result-ordinary-controls5-paired-independent.json`.

The corrected kernel frontier passes 1,248 complete compilations through all
four producers. Every complete object agrees. Compared with the accepted scalar
emitter, 153 objects contain 15,303 same-address register transfers and three
are unchanged. Every other byte, branch and symbol anchor, relocation and addend
remains checked. Evidence is
`result-kernel-frontier5-four-producer-independent.json` and
`result-kernel-frontier5-instruction-review.json`.

The four additional native self-host modes initially retain two failed encoding
inventories per host. Paired capture reviews all 115 complete self-host objects:
114 contain 32,357 same-address register transfers and one is unchanged. Every
output reached by the direct corrected caller matches its paired object.
Both emitters consume the same current source and IR units. The emitter source
itself has changed, so its current scalar output differs from the historical
compiler inventory. Evidence is
`result-paired-selfhost-dump6-complete-instruction-review.json`.

Only sixteen measured numeric locks change: thirteen active-source text
fingerprints, two compiler-source lengths and one synthetic text fingerprint.
All thirteen function counts, synthetic size, symbol and relocation counts,
10,268 original strings, semantic predicates and useful negative controls stay
exact. Both native callers then pass all 62 ordinary and self-host modes. Their
422 complete objects match the finished reviews, and all eight linked products
agree across hosts. Evidence is
`result-object-proposal10-native-paired-independent.json`. The actual i386
adapter and complete hosted runtime contract also pass on each host under their
original ten- and sixty-second bounds. The sealed 1,698-input source-seven copy now passes both complete preparations.
Independent rereading checks all 198 retained producer source copies, 291 staged
artifacts and 97 fixed-point pairs. Every stage-three/four product agrees within
its host profile. The complete 99-input producer snapshot is
`36fabda2192edb3ac0548834c93205f6bdde19562efaa7740fae8e8039a50bb5`.
Evidence is `result-full-preparations1-paired-independent.json`. These preparations
retain their actual parent lineage and remain unqualified.

A separate integration candidate combines twelve reviewed implementation/test
files with the current private controls and two exact optional-fixture ownership
rows. All 1,704 inputs are sealed. All C, header and assembly bytes agree with
source seven, and all 99 producer inputs exactly match the new preparations.
The current public bootstrap fixes, installed seeds, kernel/public Python
expectations, artifact policy and embedded manual remain preserved. Independent
checks pass twenty strict audit contracts, twenty-six ownership methods and
forty-six native frame, rotate and transfer methods. Both review axes report no
new findings. Evidence is `result-integration-candidate2-paired-independent.json`.

Both fresh retained-copy callers now build through the complete new stage-four
cohort on each host. Their host API objects are exact stage-three/four products.
All sixteen build phases, complete components, commands, streams and executable
formats pass independent rereading in
`result-fullcohort-api-build1-paired-independent.json`. Every caller retains its
original compile, strict-certification and link bounds. Both retained-copy APIs now pass the complete runtime described below. Full
new-cohort object callers remain under verification. These results do not qualify
or install replacement seeds.

Both retained-copy APIs now pass complete runtime through the entire new
stage-four cohort. Independent rereading checks 57 previous-output cases and
31 frozen-input cases, including 68 complete exported copies and twelve expected
capacity or ordinary-input rejection exits. Both Linux complete 200 MiB cases
pass under a 32 MiB address-space limit. Every original 600-second process bound,
semantic predicate, complete file and namespace check remains. Evidence is
`result-fullcohort-api2-previous-runtime-paired-independent.json` and
`result-fullcohort-api1-frozen-runtime-paired-independent.json`.

Windows blocks the attempted live-output write; Linux permits that write and
rejects the changed observation. Both preserve the complete previous copy.
The first new previous-output checker incorrectly adds a universal cross-host
output equality check after all 57 original per-host predicates pass. Its failed
source and observed result remain retained. The corrected checker keeps every
per-host predicate and verifies both exact host-specific outcomes. No runtime
case, executable, source byte or deadline changes. Evidence is
`result-fullcohort-api-previous-checker1-observed-failure.json`.

Complete new-cohort object callers remain under verification. Committed paired
producer qualification, normal OS/runtime acceptance, seed installation and
production recipe adoption remain open.


The derivative caller passes all 62 original modes on Windows. Linux passes
61 modes, then reaches the original 1,800-second limit on `self-host-link-tools`.
The failed mode emits no diagnostics and writes none of its seven requested
tool executables; the earlier linked adapter remains retained. Independent
rereading checks all 138 build/mode phases, exact commands, source and component
bytes, streams and available products. Evidence is
`result-complete-controls6-measured-independent.json`: 123 passes and one failed
mode across the two hosts. This is a measured failure, not paired acceptance.
The separate callers built from the entire new stage-four cohort remain under
verification with all original modes and limits. No deadline or assertion is
weakened, and no replacement seed is installed.

The first encoding-lock proposal incorrectly changes a mnemonic list shared
by quotient and remainder functions. Both native runs retain ten failed modes;
the proposal is not adopted. Exact function bindings must distinguish their
register operands before replacing that shared list. Evidence is
`result-object-proposal6-{windows,linux}/closed.json`. The original 23 failures
remain retained in `result-ordinary-controls4-paired-independent.json`.
Later proposals retain two failed modes and then one failed mode while resolving
ambiguous function and section bindings. Those failures also remain retained;
only the final independently accepted proposal supplies the current locks.

The initial native Linux run times out on the full 200 MiB changed publication
under 32 MiB. Initial prepared-CupidC runs on both hosts time out on the full
200 MiB changed-publication case. All three original 600-second failures remain
retained and rejected. Successful replays use the same complete source,
programs, assertions and bounds from native Linux storage and a shorter Windows
layout. The Windows changed-publication replay takes 565.069 seconds; the two
Linux fixed-memory controls take 542.510 and 543.369 seconds. Those results do
not establish a universal layout or timing guarantee.

The retained runtime driver checks equal reuse's inode and timestamp during
execution. Its receipt does not retain the earlier stat tuple, so independent
rereading cannot repeat that metadata assertion. It does reread the complete
unchanged output and retained previous copy.

The corrected result-transfer emitter still needs complete actual Cupid-built
self-host acceptance, committed paired producer qualification and normal
OS/runtime acceptance. The API then needs production recipe adoption. Complete
disk validation, flushing, SDK publication and removal of the Python disk
publisher remain separate ownership work. No TempleOS source participates in
these counts or builds.
