# Retained required disk files

The private template and publisher can now capture bootloader and kernel files
through selected retained observers and continue into one guarded publication
transaction. The complete small-image and legacy scope passes independent
review. Checked 200 MiB reuse still exceeds the original 600-second limit.
This capability does not yet own the normal image command or its Python recipe.
[ADR 0457](../adr/0457-capture-required-disk-files-through-retained-observers.md)
records the interfaces and authority rules.

## Source and ownership

The accepted small-scope copies are `cp7/disk-required-handoff-source3` on Windows
and `/var/tmp/disk-required-handoff-source3` on Linux. Their
`required-handoff-source-controls.json` captures 209 source/support files and
109 private producer inputs. Eight existing paths change: the host, template
and publisher implementation/header pairs, shared stdio header and shared
runtime. Three paths are added: the handoff C/Python fixtures and a separate
legacy-template fixture that preserves the original template contract.

`cupidbuild_host_publication_transaction_open_wide` creates no implicit input.
It retains the existing output-parent and previous-output rules, lock and
private storage. Capacity stays within 1 through 2^61-1 bytes. Required inputs
are subsequently explicit observations and captures.

`cupidbuild_host_transaction_observers_share_file` compares cached regular-file
entries under the exact primary or already borrowed external observers. It
rechecks every borrowed lifetime, observes no new leaf and clears a failed
result. `cupidbuild_host_transaction_require_primary_observer` requires the
existing exact primary binding without another bind or authority grant.

`cupidbuild_disk_template_capture_observed` validates the paired seed cohort,
retains both selected required-file roots, freezes complete inputs and runs the
existing CupidObj producer. Physical hardlinks reuse a capture. Copied path
strings survive input-table growth. The complete boot file is retained even
though its image view uses five sectors; the kernel remains bounded by the
pre-partition extent.

`cupidbuild_disk_publish_captured` consumes that registered template and those
required captures. It preserves sequential optional staging, collisions,
missing-source observations, complete verification and guarded publication.
Already borrowed optional observers retain their authority. Both previous
entry points remain supported and are exercised in full.

The current normal producer still has 99 inputs, exactly committed
`acbbd8342d8901169c5484742b168277ce9bbdd1`, snapshot
`5a2d30853cc1e064c93a2795428c7511822f8833e03085e5e044efc895f0fe9b`.
The installed fifteen-file seed projection retains parent revision
`a1cc8f3abc7e3d988f1285220deaf05798c30034`. This private source is not integrated,
qualified or installed. Ownership remains 449 CupidBuild and three Python
actions across 452 transforms. TempleOS is excluded.

## Complete small-image acceptance

All four `disk-required-handoff-{native,checked}-build-{windows,linux}3` builds
pass. Each retains five complete caller programs and 209 source copies.
Checked production uses the normal-source stage-four compiler and separately
copied parent tools, two workers and the original 360/120/180-second producer
limits. Changed host/template/publisher and fixture objects pass strict
CupidDis checks. Both shared runtime objects pass separate sixty-second strict
checks.

The closed native Windows acceptance retains its earlier eighty handoff and
sixteen template passes, then reruns only its failed external/observer/UNC
selections under `native-windows4-runtime-closed.json`. Native Linux retains its
complete passing attempt three. Both checked complete retries close under
`checked-{windows,linux}4-runtime-closed.json`. Windows takes 2,821.527 seconds;
Linux takes 2,178.286 seconds. Every original individual case bound remains.

| Selection across four callers | Selected | Executed | Platform skips | Calls |
| --- | ---: | ---: | ---: | ---: |
| Required handoff, including byte input | 320 | 310 | 10 | 310 |
| Existing template | 64 | 64 | 0 | 64 |
| Existing external publisher | 176 | 172 | 4 | 172 |
| Ordinary observer identity | 56 | 52 | 4 | 52 |
| Windows UNC observer | 48 | 48 | 0 | 54 |
| Total | 664 | 646 | 18 | 652 |

`disk-required-handoff-independent7-products/closed.json` passes in 155.391
seconds. It rereads every selected method and complete result sequence, all
209 controls and their builder/runtime archives, the 109 private and 99 normal
producer inputs, installed seed files, actual tools and all original bounds.
It compares 23 complete checked object pairs and corresponding complete images.

The checker independently reconstructs every positive eight-MiB image through
the separate template/FAT oracle. A raw reader checks FAT mirrors, complete
memberships, cluster chains, crosslinks, cycles and padding. Negative prior
bytes, timestamps and final namespaces remain checked. Deliberate same-size
edits must change exactly the intended digest while preserving its timestamp.
Only Windows line endings and its recorded native read-error errno difference
are normalized in result comparison. Exact template-production diagnostics are
checked before normalizing their platform path or retained descriptor spelling.

The accepted source includes all eighty handoff methods and the complete
existing template, external publisher and observer methods. It exercises
released caller storage, input growth, external and hardlinked required files,
prepared nested output parents, output/input aliases, optional captures,
collisions, missing paths, restored-time edits and invalid or late authority.

## Large-image results and performance investigation

All four native fresh/reuse cases pass at 200 MiB. Windows takes 35.336 and
74.104 seconds; Linux takes 26.471 and 59.164 seconds. Both checked fresh cases
also pass and produce complete oracle-equal images. Both checked reuse cases
time out at 600 seconds. Linux retains 32 MiB of address space. Their original
failed outputs and private namespaces remain held; they do not prove completed
cleanup or large publication acceptance.

Separate unchanged-library tracing builds preserve the full geometry and
limits. Windows enters guarded publication at 300.426 seconds and leaves at
597.351 seconds before timing out in close. Linux enters at 232.527 seconds,
leaves at 493.472 seconds and also times out in close. These diagnostic runs
retain their initial inputs before launch and durable phase records.

A separate pure-byte SHA probe returns the independent digest through native
and Cupid callers. Its initial four-MiB medians are 0.025/0.274 seconds on
Windows and 0.019/0.220 on Linux. The existing rotate leaf already emits ROR.
A separate literal-transfer compiler prototype preserves instruction offsets
while replacing selected validated PUSH-immediate/POP spans with MOV and NOP.
Both checked compiler derivatives reach a three-generation emitter fixed point
and pass ten capability controls. This is a limited emitter/derivative check;
the complete producer and branch-entry regression scope remain open. Whole
host-object comparison accepts 2,742 Windows and 2,385 Linux transfers, with
every other byte, symbol and relocation unchanged.

Replacing only that host object lets the Windows diagnostic reuse close in
589.996 seconds. Linux still times out in close at 600 seconds, after publication
returns at 488.571 seconds. These separate traces do not qualify the compiler or
accept the complete large-image cohort. Neither the SHA workload nor a returning
publication function substitutes for successful bounded end-to-end close.
Complete image/FAT comparison and strict guest boots remain required after all
large cases pass.

## Preserved failures and remaining integration

Source one assumes LF-only readiness. Source two corrects that fixture and then
exposes the absent hosted getchar declaration. Source three supplies the
standard byte-input entry points under
[ADR 0458](../adr/0458-read-hosted-bytes-through-fgetc-and-getchar.md). Earlier
disk-full and WSL I/O failures remain separate terminal attempts. Lossless NTFS
compression of closed generated images verifies all complete hashes, lengths
and modification times; no files are deleted. Fresh retries preserve every
earlier accepted selection.

The independent checker's failed versions assume a different outer receipt
schema, empty stderr for deliberate producer failure, an ordinary test root for
the Unicode fixture, a template-boot filename for the legacy source fixture,
named Linux input paths instead of inherited descriptors, or an unprefixed
Linux observer executable. Corrections retain every original scope predicate
and use the actual retained records. All failed receipts and checkers remain.

Full command integration still needs transaction-scoped revalidation of the
original selected parent bindings, external manifest and output authority,
accepted large geometries, complete pathname diagnostics and distinct-input
capacity. Capture/discovery owners must remain alive through every borrowing
transaction. Source plans must include all new modules and data tables before
complete producer qualification and normal OS, SDK, public and Make consumers.
The current replacement-seed SDK/public/Make queue remains a separate gate;
root seed adoption and the pending manual commit await its complete acceptance.
