# ADR 0234: Promote long-double and JPEG support into the checked seed

## Status

Accepted on 2026-08-04.

## Context

Revision `c31f062fc67c78b553919c2600dd953d252cb58b` gives hosted CupidC
bounded decimal `long double` object emission and gives CupidObj transactional
sequential-JPEG validation through `wrap-jpeg`. It was committed and pushed
before the promotion candidate was built.

The preceding checked seed can build the revised 19-source Toolchain closure,
but its own CupidC and CupidObj images do not expose those capabilities. The
build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

## Decision

Promote all five stage-three images as one checked cohort:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `1dc9061912f127d231d320940ba781781af663bde83852a613910394709ecc76` | yes |
| CupidC | 2,582,400 | `03084115bcacb1987db5513c8a8be9b7d884029b03ab4b212bf40d997871ae79` | yes |
| CupidDis | 379,648 | `a45fc4c57afd3bb02980e514d58c11588ba3a8bfa2f05ca348fe465cfdaf9749` | no |
| CupidLD | 266,672 | `2bdb6ce6b04678bb89c6bb4f7afac7e152ce6c4a07c4e14e1b3aee0c899008ec` | yes |
| CupidObj | 279,004 | `8975f1f106bd144a2467e98ab3e972c83105d3db7e305703bcc8bd3eda9b983f` | no |

CupidC and CupidObj change from the preceding cohort. CupidASM, CupidDis, and
CupidLD remain byte-identical. The manifest continues to verify all five
files together because the fixed point and its producer lineage cover one
Toolchain generation, not a set of independent caches.

The 5,440-byte manifest has SHA-256
`1302d48c541850b5248df05d07a8f4d7a68fe070dd6118edadbecd280b309ad1`.
It names the pushed capability revision, the static i386 Linux ABI, the
existing producer lineage, and the unchanged build plan.

## Evidence

The transition completed in 737.17 seconds. It froze 41 inputs with SHA-256
`2d2a3253a9559a7e450d3f8755bc66ca2f5e0136d41045c7aeea04949a8d177d`.
All nineteen C objects, startup, and five tool images matched between stages
two and three. Both stages passed five help cases, twelve successful
operations, and eight useful failures. CupidC and CupidObj differed from the
preceding seed; CupidASM, CupidDis, and CupidLD matched. The 15,057-byte report
has SHA-256
`d45a0b4c5afb4feb06216d3f2da5aad7f084912d7a291798296e81c57fef5132`.

`make verify-bootstrap-seed` accepts the promoted manifest and all five
static ELF32 images. Focused checked-seed tests run the promoted CupidC decimal
`long double` case and its precision rejection. They also compare checked
CupidObj `wrap-jpeg` with ordinary binary wrapping, reject a progressive
frame, and preserve the caller's existing output.

An independent post-promotion rebuild completed in 774.524 seconds with both
host code-generator commands poisoned. All five seed images matched stage two
before the stage-two and stage-three comparison. A direct hash pass matched
the nineteen C objects, startup object, and five tool images across those
stages. The rebuilt tools repeated the five help, twelve success, and eight
failure cases. The 15,055-byte report has SHA-256
`405abd7b5ceecf05037521e63fb8744cf5a474ea70c23b990055c64f641cc0a1`.

The focused checked-seed JPEG and decimal `long double` tests passed in 5.892
seconds. The complete checked-seed module passed all 42 tests in 841.721
seconds, including another full poisoned-host fixed point and the manifest,
source-drift, ELF, provenance, rollback, and capability contracts.

The canonical build audit retains 719 active inputs, 449 transforms, 255
feature requirements, and 25 accounted unreachable files. Generation and the
independent drift check pass, followed by all 68 audit tests in 939.059
seconds.

The final `make -j4 all test_usb_partitioned.img` build passed in 630.967
seconds. Its 9,069,064-byte final ELF has SHA-256
`2f013bf9a3bc7a7ee986b7a0c8c817e7f0b09873473da0c6ce0bdb5efb16aed9`.
CupidObj flattened it to an 8,862,144-byte kernel with SHA-256
`95a92bac021cdc091df2ca5a5139ccc52e4cf5421e4e9a3565a4be96573d0917`.
The 209,715,200-byte image has SHA-256
`f375ae4bf09bdc76e5e9e19863ed7ea86e3bccebf31fb4a1ccb070b783845bb0`.
The checked JPEG object remains byte-identical at 800,860 bytes with SHA-256
`74ab86d88302c90385bb0b858632b0d6c4ac983d6be28c976dd1a3a348204b3e`.

One promoted-seed Toolchain cohort build ran under the concurrent OS, audit,
and QEMU load. It completed `kernel/lang/as_elf.cc` and thirteen of fourteen
stage-two contract sources before the 900-second compile timeout expired on
`toolchain/tests/cupidc_object_contract.cc`. The command stopped after a total
of 2,349.228 seconds and published no replacement cohort. The isolated retry
passed in 2,986.264 seconds. Its stage-two and stage-three objects and
executables matched, the hosted runtime passed, and all 20 artifacts verified.
The 18,231-byte manifest has SHA-256
`1c2f81f25eb0ee8c09b4ccdd789dfd22aa8765cef86bf7d8b14762d48e6a468e`.

Fresh private-image four-vCPU boots passed the final runtime frontier with
e1000 in 547.392 seconds and RTL8139 in 541.995 seconds. They changed 70,618
and 90,589 framebuffer pixels, respectively, and captured non-silent AC97 and
PC-speaker output. The 149,777-byte e1000 log has SHA-256
`b1b22080e09b6d3e4c75a62cddb1bbb4b7f9ac6557a716469c6b231cba41777b`.
The 154,222-byte RTL8139 log has SHA-256
`2bd4b6c3bf0019e404c7140b4776e4311ffec82f63f8fd42fbcff947f12b777d`.
Neither log contains a panic or fatal runtime marker.

## Rejected alternatives

Keeping the preceding seed was rejected because normal checked runs would
continue to use a CupidC and CupidObj that predate the source capabilities.

Replacing only the two changed binaries was rejected because the five images
and manifest form one fixed-point trust unit. The three unchanged stage-three
images were compared with the preceding files before the cohort was promoted.

Moving the normal JPEG recipe in this promotion was rejected. Seed carriage
and production ownership are separate changes, and the production publisher
must retain its source snapshot, drift checks, path checks, Python parity
oracle, and atomic replacement boundary when it begins calling `wrap-jpeg`.

## Consequences

Checked-seed CupidC now emits the represented bounded decimal `long double`
constants, including the exact one-bit rounding case carried by the seed
test. Checked-seed CupidObj now validates SOF0 and SOF1 input before applying
the ordinary byte-exact wrapper. The fixed-point behavior matrix is now five
help cases, twelve successes, and eight failures.

The normal JPEG recipe still uses Python for validation until its separate
ownership transfer. Python still coordinates the bootstrap, Windows still
runs the static i386 tools through WSL, and a native Windows fixed point and
Python-free coordinator remain open.
