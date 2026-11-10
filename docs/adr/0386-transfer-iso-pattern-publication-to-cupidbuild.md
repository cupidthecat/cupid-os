# ADR 0386: Transfer ISO pattern publication to CupidBuild

Status: Accepted. Both repaired seeds carry the command. Production
regressions pass on Linux and Windows with the `16a86f5b` pair. Final-manual
parallel replays pass on both hosts, along with the private four-CPU runtime
frontiers on the Linux image with E1000 and RTL8139, including their ISO
checks. The Windows image with preserved FAT contents separately passes an
SMP and `ls` smoke.

ADR 0384 added `assemble-iso-pattern` for the 4,096-byte spanning fixture used
by Feature 17. Make now selects that typed command directly for
`test_iso/fixtures/big.bin`. The rule binds `test_iso/big_pattern.asm`,
Makefile, the selected manifest, and all six seed images. It passes the
absolute repository root and does not consult standalone CupidASM,
CupidDis, Python, or checked-runner overrides.
Make fixes `ISO_BIG_FIXTURE_SOURCE` with an `override` assignment, and the
audit requires that assembly source to be the first prerequisite because
the command uses `$<`.

CupidASM still authors the unchanged `times 4096 db $` source. CupidBuild
freezes the source and seed, checks every candidate byte and the complete
base-zero data-only map, and asks checked CupidDis to inspect the private
output. The shared transaction rechecks publication state before replacing
the fixture. Failures preserve the previous bytes and timestamp; equal bytes
keep the existing file. The raw map never becomes a public fixture.

This transfers one coordinator without adding an output. The supported graph
keeps 452 transforms: CupidBuild participates in 197, Python in 255, and
CupidDis in ten. CupidASM remains at nine. The separate `hello.iso` recipe
continues to use Hostbuild's frozen inventory, independent renderer, and
guarded publication. The `gen-big` compatibility command and its tests remain
available as an oracle.

The ISO fixture has no profile scheduling dependency. It writes beneath
`test_iso/`, outside the directories retained by Doom profile discovery. The
254-writer barrier remains intact: 171 writers use order-only dependencies,
and the 83 Doom compilations retain ordinary profile content dependencies.

The artifact verifier needs a separate scheduling edge. Make gives
`verify-artifact-sizes` the order-only prerequisite `test_iso/hello.iso`.
This finishes the whole ISO transaction before the verifier pins the
repository root. Even when `assemble-iso-pattern` reuses equal output, its
reservation, candidate, and capture entries change the root's modification
time. A retained inode and unchanged final directory membership do not make
those concurrent writes invisible to the verifier.

A fast parallel Linux replay exposed this overlap and failed the root-drift
check; the serial replay passed. The order-only edge fixes scheduling without
weakening that check, adding ISO bytes to the size-policy inputs, or changing
the 197 CupidBuild / 255 Python ownership split. The audit requires exactly
`["test_iso/hello.iso"]` in the verifier's `order_only_inputs`.

Two audit cases first reported five expected failures. After the repair, four
audit guards passed. Four Make ordering regressions were added; their module
passed 20 cases on Windows and Linux, with four Windows platform skips and
none on Linux. The combined artifact-runner and policy suites passed 67
cases with four platform skips. The bootstrap log records timings and hashes,
including an earlier unexplained Windows fixture-cleanup failure and the
unchanged successful retry. Repaired parallel production replays now pass
on both hosts. The combined audit, artifact-runner, and policy suites pass
155 tests with four platform skips.

The audit requires one exact ISO output, its typed recipe, its three tool
participants, and its distinct source/Makefile/seed input set. It rejects
missing seed members, duplicate inputs, changed options, legacy Python
coordination, reordered source selection, and an added profile scheduling
edge. Twenty-two mutation cases pass. Review exposed that comparing inputs
only as a set did not bind `$<`; the new order and source-override regressions
failed before those two constraints were added.

The real-Make tests copy the unmodified Makefile, active source, and complete
host-matching seed into a private fixture. They cover initial publication,
replacement, timestamp-preserving reuse, wrong-pattern rollback, and drift in
CupidC, which belongs to the frozen cohort but is not launched by this
transaction. `CUPIDBUILD_TEST_SEED_MANIFEST` may select another complete cohort;
it does not substitute the publisher or change its manifest. Run them with:

```sh
python3 -m unittest discover -s tests -p test_iso_cupidbuild_production.py -v
```

On Windows, use `python` in the same command. The provisional `34b597aa` seed
rejected the ISO command. Production tests stayed enabled while the repaired
pair was built; no substitute publisher counted as checked-seed proof.
The promoted pair binds source revision
`16a86f5b1693e017c36c6d902df9946c5d674b17` and source snapshot
`54b411b6ed05725101f5859106facb43639f2d02cb2b0ea2f6334e21375bda55`.
ADR 0382 records both manifest hashes and the clean fixed-point proofs.
Both Make branches and the exact audit contract passed their poisoned-control
checks. With the `962e476b` candidate pair, the real-Make module passed all
four cases without skips in 62.322 seconds on Windows and 31.938 seconds on
Linux. The three ISO CLI cases passed against that pair in 10.579 and 12.469
seconds, respectively. These runs are prior-pair evidence, not acceptance of
the final `16a86f5b` production replay.
The temporary exact-old-seed skips introduced with ADR 0384 are removed.

The final pair passed the corrected 167-case Windows production suite and
the isolated Linux archive's 48-case handoff suite, including the ISO cases
and replaced-output-directory handling. Their two Windows platform skips and
one Linux Windows-junction skip are accounted separately. ADR 0382 and the
bootstrap log record the timings and log hashes. OS-image and runtime
acceptance now pass on the final-manual cohort.

The expected fixture remains 4,096 bytes with SHA-256
`c8f5d0341d54d951a71b136e6e2afcb14d11ed8489a7ae126a8fee0df6ecf193`.
A four-CPU Feature 17 smoke passed before this handoff, including the exact
directory names, Rock Ridge name, JPEG decode, glyph/cache agreement, final
pass, and JIT completion. That is baseline evidence, not a test of the new
production image.

No language, ABI, active source cohort, or C suffix changes. All active CupidC
roots already use `.cc`, and this assembly publication adds no safe rename.
`TempleOS/` remains read-only reference material.
