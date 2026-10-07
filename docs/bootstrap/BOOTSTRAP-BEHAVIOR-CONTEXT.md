# Public bootstrap behavior context

The private Linux and native Windows bootstrap entry points accept an explicit
reviewed release through `--seed-release`. This reuses runtime authority only
for byte-identical final tools. It does not qualify changed compiler source.
Both final stages must contain all six exact reviewed tools, with the selected
plans and complete seed observations intact. Native Windows also keeps the
Linux plan seed and Windows execution seed as separate retained roles.

For a source snapshot that reproduces the installed cohort, use:

```text
python tools/bootstrap_toolchain.py bootstrap --root SOURCE \
  --manifest bootstrap/seeds/i386-linux/manifest.json \
  --seed-release bootstrap/seeds/release.json --windows-long-paths --output OUTPUT

python tools/bootstrap_toolchain.py bootstrap-windows --root SOURCE \
  --manifest bootstrap/seeds/i386-windows/manifest.json \
  --plan-manifest bootstrap/seeds/i386-linux/manifest.json \
  --seed-release bootstrap/seeds/release.json --windows-long-paths --output OUTPUT
```

The native Windows command requires a Windows host. A changed source head must
first pass the separate `tools.bootstrap_stage_release` preparation, paired
authoring and qualification flow. The installed release must not authorize
different tools. The current committed cohort has now passed both complete
qualifications and is installed in the private worktree. Complete public
entry points, paired normal OS acceptance and all four SDK publication profiles
pass. Windows external-user runtime remains required before a green replacement
commit.

## Verification and failed approaches

The original actual-worktree compatibility run selects 259 methods on each
host. All 500 executions pass, with eighteen expected platform skips. The
independent receipt is `bootstrap-behavior-context1-paired-independent.json`.
Both actual complete fixed-point and long-alias preparation methods remain
separate acceptance requirements. The five original input files are retained
in `bootstrap-context-before-cli-fix1` before later corrections.

Review against `1328cc18` exposed the real CLI exception boundary. Malformed
release selection produced an uncaught traceback through both direct script
and module execution. The corrected driver shares its package identity with
the coordinator. Missing-release controls exposed a second raw filesystem
failure; capture and final revalidation now raise the bounded bootstrap error.
The exact diagnostics and absent output are checked through actual subprocesses.
Original failures and corrected results are retained in
`bootstrap-cli-diagnostic1-*` and `bootstrap-cli-diagnostic2-*`.

The new context module covers 22 methods, including every final-stage tool,
selection, plan, release drift, final release disappearance, historical
omission and incompatible authority selection. The corrected five-module run
selects 264 methods per host. Independent rereading checks 508 passes and twenty
expected host skips in `bootstrap-behavior-context3-paired-independent.json`.
Its exact five files are retained in `bootstrap-context-after-cli-fix3`.

The first run after seed adoption fails five methods on each host because the
old object locks describe the earlier emitter. The affected methods cover libm,
kernel entry, SIMD, Doom compatibility and `returns_twice`. Their original
source guards pass. Both failed runs remain retained in
`bootstrap-behavior-context4-*`.

Fresh old-seed and new-seed compiles reproduce the original locks and repeat
the replacement bytes on both hosts. Independent review checks 56 complete
objects from seven unchanged sources against the existing direct frame-word
read and adjacent register-transfer rules. It preserves branch targets, symbol
identities and extents, data, relocations and addends. Evidence is
`stack-seed-golden-objects1-paired-rewrite-review.json`.

The private tests receive seven reviewed active-object locks and 46 full-kernel
locks and the kernel object total of 4,162,872 bytes. The kernel changes
also use the existing paired 624-object review. Source digests, source counts,
semantic assertions, rejection checks and deadlines remain intact. The exact
edits are retained in `stack-reviewed-seed-golden-adoption1/closed.json`.

The unchanged 264-method selection then passes on both hosts: 508 executions
and twenty expected skips across 528 selections. Independent rereading checks
the five exact adopted inputs and both logs in
`bootstrap-behavior-context5-paired-independent.json`. Those inputs remain
retained in `bootstrap-context-after-seed-lock-review5`. The two original full
public methods remain separate acceptance requirements.

Inspection of the running full fixture finds another stale profile guard. Its
command selects long-path behavior and captures 99 inputs, but its later
independent capture omits that selection and gets 98. Two old literals still
expect 81. The private fixture now selects the same long profile, expects 99,
compares the complete report input map and requires the promoted snapshot
digest. The running original source copy stays unchanged, and corrected full
execution remains required. The 6,000-second bootstrap and 3,000-second Windows
preparation limits remain intact. The exact diagnosis is
`stack-public-source-profile-diagnosis1.json`.

The original Linux full selection closes in 2,078.706 seconds. Its bootstrap
command passes the original status and output assertions, then the fixture
rejects the old 2,048-byte Windows image hash. The failed test log remains
retained. Review finds three stale product locks: that image, the native
Windows compiler's small object, and the native linker's runtime program.

Fresh paired controls compile four unchanged C cases twice per compiler and
build two identical startup objects per host. Complete review checks forty
objects and sixteen PE images. Only the two approved local instruction rules
change the C objects; branches, symbol extents, data and relocations remain
mapped. Cross-linking each complete old/new object set with both linkers gives
the same image bytes, and the replacement products match both qualified
generations. Twelve actual native Windows executions pass the old/new images,
including runtime file contracts and bad-argument rejection.

The historical 33,792-byte runtime lock does not reproduce under the current
source with either the previous seed or Git review baseline. Both produce the
same 35,840-byte image. The initial reproduction attempt remains failed and
retained; the corrected comparison uses that measured baseline. The literal
last changed in `88bb2d26`. The replacement is 33,280 bytes. The private full
method receives three exact reviewed hashes and two sizes, preserving every
other assertion and deadline. Evidence is
`stack-public-golden-products2-paired-rewrite-review.json`,
`stack-public-golden-native-runtime1/closed.json` and
`stack-public-reviewed-lock-adoption1/closed.json`. Corrected complete public
execution and normal OS acceptance remain open.

The original Windows selection closes in 3,997.645 seconds. Its fixed-point
method fails the stale native compiler/linker output locks after the bootstrap
command succeeds. Its complete long/alias preparation method passes, including
the original unreleased-parent rejection. Independent rereading binds both
failed host selections and that successful preparation to their exact source
copies in `stack-public-entry-tests1-failures-paired-independent.json`. The
corrected full methods and actual Make entry points run from separate immutable
1,700-file copies; their closed results remain required.

The corrected Linux full selection now passes in 2,379.158 seconds with its
declared Windows-method skip. Independent rereading checks the exact source,
log and original method limits in
`stack-public-entry-tests2-linux-independent.json`. The actual native Windows
Make recipe passes in 2,632.352 seconds. Its independent rereader checks all
3,423 published regular files and 159 staged products against the separately
qualified fixed point in `stack-public-make1-windows-independent.json`.

The completed compatibility selection remains bound to current selected code:
four complete inputs agree, and the fifth differs only inside its two excluded
full methods. Every other byte agrees after removing the AST-defined method
ranges. Evidence is `bootstrap-context5-selected-source3-independent.json`.
The Linux Make recipe subsequently passes in 2,472.824 seconds. Paired
independent rereading checks all 7,761 published regular files and 291 staged
products in `stack-public-make1-paired-independent.json`.
The corrected Windows full selection remains pending.
Fresh paired OS rebuilds and ordinary SDK publications are running separately.

## Current committed producer cohort

The separate candidate uses committed revision
`2d04ff25c3191eeacb71bb052f316817dc0954a7` and 99 producer inputs with SHA-256
`9bbcbb975781f2acd687add5e952cca317328276a94fb15ca781b5512ce51955`.
Both preparations pass with conventional host code producers forbidden.
Independent rereading checks 198 source copies, 291 stage artifacts and
97 matching final-stage pairs. Its separately authored release selects twelve
exact tools and is 2,736 bytes, with SHA-256
`aa39be2ebe2fe86f0ecd70ec2ca4937a0b6ccac3c28e67732e7ee3200c62c28f`.

Complete native Linux qualification passes in 1,973.318 seconds. Independent
rereading verifies all 4,338 published regular files, 132 staged products,
the actual release and source bindings, final-stage equality and both behavior
generations. The report records 73 success cases, 66 failure cases and seven
help cases. Evidence is `stack-committed-qualification3-linux-independent.json`.

The first Windows qualification builds all 159 staged products, then fails
when a checked child cannot start from its deep private working directory.
Those products match the accepted preparations and remain retained. The exact
unchanged production profile helper reproduces the launch failure in the deep
layout and passes in a shorter layout, with the same tools and release. This
does not expand the long-filename profile to support long child working
directories; ADR 0410 keeps those capabilities separate.

The fresh complete Windows qualification passes in 2,492.113 seconds from a
byte-exact short copy of all 1,695 committed source inputs. It retains the
original producer deadlines, behavior checks, release, source revision and
forbidden host producers. Its report records sixty success cases, 54 failure
cases and seven help cases across the two named behavior generations.

Paired independent rereading checks all 7,761 published regular files and
291 staged products against the accepted preparations. Evidence is
`stack-committed-qualifications4-paired-independent.json`. The failed deep-layout
attempt remains rejected. The shorter successful working directory does not
extend the supported long-filename profile to long inherited child directories.

The private worktree now contains the exact fifteen qualified seed files,
updated independent Python pins and twelve measured seed-size policy rows.
`stack-qualified-seed-private-adoption1-independent.json` verifies every prior
and replacement file, restricts the pin and policy edits, and rechecks all
99 unchanged compiler producer inputs. The public full fixed-point and native
Windows entry points, normal consumers and OS/runtime checks remain open. See
[ADR 0446](../adr/0446-carry-qualified-scalar-emitter-seeds.md).

Both fresh OS builds now pass independent review of 1,700 source controls,
99 producer inputs, 429 objects, sixteen artifacts, six user products, full
syscall ABI, preserved FAT data and four strict four-CPU boots. Whole-object
review accepts only the two existing local instruction rules, the complete
manual payload and independently reconstructed kernel-symbol data. Evidence
is `stack-qualified-os1-paired-independent.json`. Every corresponding product
and complete image agrees byte for byte. Only the three measured kernel-size
policy rows change. Linux external hello, ls and cat pass independent FAT and
runtime checks. Windows external ls retains an EHCI DMA-revocation panic.
A separate Windows cat case passes independent checks. Ordinary Linux SDK
publication passes independent review of 104 inputs, 77 complete stage pairs,
22 ELF artifacts and its manifest. All four SDK profiles now pass in
`stack-qualified-sdk3-four-profile-independent.json`, with 104 inputs, 77 pairs
and 22 ELF artifacts per profile. Windows external-user acceptance remains open.

The earlier full-kernel lock update missed fourteen port-I/O entries because
its selector handled only direct assertions and the 37-entry dictionary.
Their replacements already have complete paired review. Update those fourteen
size/hash pairs, verify all sixty byte locks and preserve every other AST node.
Evidence is `stack-reviewed-kernel-port-lock-adoption2/closed.json`. Complete
original kernel-frontier modules pass all 36 methods on each host from separate
consumer copies, with their original assertions, skips and deadlines.
