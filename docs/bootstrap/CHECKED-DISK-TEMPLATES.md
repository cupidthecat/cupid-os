# Capture a checked disk template for retained composition

The separate prototype now runs the authorized paired CupidObj producer before
opening the native candidate store. `cupidbuild_disk_template_capture` captures
the complete fifteen-file seed cohort, freezes the kernel with the requested
geometry bound, runs the selected host's `disk-template` operation and transfers
its accepted private output into the same transaction's frozen-input table.
The transfer keeps the complete file identity and digest without copying the
payload. The result owns its three path spellings through input-table growth.

`cupidbuild_disk_io_open_frozen` accepts those registered boot, kernel and
template inputs. It checks their canonical snapshots against the original
geometry bounds, captures the previous public image and opens the owned store.
The existing live-input opener retains its capture protocol. Both openers share
the boot-prefix, previous-image, candidate and callback checks.

The normal disk recipe remains Python-owned. This module supplies checked
template capture. The [required-file publisher](CHECKED-DISK-PUBLISH.md) now
connects it to staging and validation and passes separate four-caller acceptance
with independently reconstructed images. Optional discovery, the normal CLI,
complete source qualification and replacement seed adoption remain separate
integration requirements.

## Lifetime and limits

The caller supplies a transaction and observer over the same retained root,
authorizes the paired release and selects CupidObj role 4. The complete boot
input is already registered; the producer consumes its five-sector prefix.
Kernel capture is limited to `fat_start_lba * 512 - 2560` bytes. The template
must include the complete prefix through the FAT boundary and fit the image.

Keep the observer alive through transaction close. Calls and the borrowed
lifetimes are serialized, and argument, result and diagnostic storage are
disjoint. Failure clears the result and requires discarding the operation.
Successful capture opens no candidate and publishes no image. The caller still
owns complete format/content validation and guarded publication.

The checked producer retains its 60-second bound. CupidObj and the existing
private-output path retain their 64 MiB payload limit. Full-width candidate I/O
does not widen that template-producer limit. Accepted geometry with a prefix
above that capacity still requires a separate producer extension before the
normal native handoff can cover it.

## Four-caller evidence

Sixteen methods pass through native and matched Cupid-built callers on both
hosts. Seven positive cases cover the complete pristine image, owned paths
after table growth, a longer boot capture, a UTF-8 root, preserved FAT files
under a changed kernel, force formatting and equal-image timestamp preservation.
Nine rejection cases cover the selected role, geometry, an unregistered boot,
a missing kernel, a changed selected tool, actual producer failure, an
unregistered template and both registered-input geometry bounds.

Every runtime invocation keeps 180 seconds. Both Linux callers also retain
the 32 MiB address-space limit. Matched builds use the separately retained
mixed-wide compiler with two workers and the original 360-second compile,
120-second assembly and 180-second link bounds.

| Caller | Closed selection | Seconds |
| --- | --- | ---: |
| Native Windows | `disk-template-capture-native-windows2` | 41.584 |
| Native Linux | `disk-template-capture-native-linux2` | 28.716 |
| Cupid-built Windows | `disk-template-capture-checked-windows1` | 329.695 |
| Cupid-built Linux | `disk-template-capture-checked-linux1` | 267.559 |

The complete matched builds take 76.514 seconds on Windows and 81.697 seconds
on Linux. Their normal producer observations contain 104 inputs; the additional
prototype modules and fixture sources are also captured and explicitly built.
This is a source observation, not complete producer qualification or adoption.
The installed qualified cohort retains its 99 inputs.

Independent rereading passes all 64 executions in 40.177 seconds. It checks
complete source/header bytes, the actual parent compiler, every build artifact,
original process and memory controls, complete output bytes, exact timestamps
and namespace cleanup. All four corresponding positive images agree byte for
byte. The reuse case compares the full prior image, replaces its boot/kernel
prefix and preserves the complete FAT suffix, including `KEEP/NESTED.TXT`.

Evidence is `disk-template-capture-independent1-products.json` under
`C:/Users/admin/cp7`. Complete source/build archives are
`C:/Users/admin/cp7/disk-template-capture-accepted1-windows` and
`/var/tmp/disk-template-capture-accepted1-linux`. They were copied after the
closed successful executions, checked against every original source control
and retained before the next source change.

The first native selection fails only its reuse assertion on each host. Its
fixture supplies an unchanged kernel and correctly receives `changed=0`.
The accepted selection changes the actual kernel generation, retains both
ordinary producer templates and requires `changed=1` while comparing every
preserved FAT byte. Both original failed selections remain recorded.

The earlier host ownership-transfer evidence remains in
[private output inputs](PRIVATE-OUTPUT-INPUTS.md). The separate required-file
caller and its original complete-image controls remain in
[retained disk publication](RETAINED-DISK-PUBLISH.md).
