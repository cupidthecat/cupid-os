# Native user links and terminal records

Normal Make now uses the paired `cfd9f140` six-tool release and passes
`bootstrap/seeds/release.json` to seeded CupidBuild commands. The default
`hello`, `ls` and `cat` executable recipes call `cupidbuild link-user` at
`0x01C00000`. Explicit alternative `USER_TEXT_ADDRESS` spellings retain the
existing numeric override path. Configurable output directories, aliases and
UTF-8 paths keep their existing checks.

The operation freezes the user object, manifest, selected release and complete
tool cohort. It runs checked CupidLD and CupidDis, validates the external ELF,
rechecks inputs and publishes through the retained output parent. Equal
executables preserve timestamps. Matching a release record establishes agreement
with the reviewed record; it does not authenticate the record's authority.

The canonical graph retains 766 active sources, 255 features, 452 transforms
and 41 accounted unreachable sources. CupidBuild owns 447 actions and Python
owns five. The remaining Python actions are:

| Output | Operation |
| --- | --- |
| `cupidos.img` | Disk image publication |
| `test_iso/hello.iso` | ISO9660 image publication |
| `user/test-syscall-abi` | User syscall ABI verification |
| `toolchain/all` | Toolchain manifest verification |
| `toolchain/build/cupidc-contracts/manifest.json` | Toolchain contract publication |

## Evidence recovery

The local receipt directory was deleted. Named receipts in the historical
sections below are unavailable, including the original failed runs. Their
reported outcomes remain historical observations; no deleted acceptance
receipt has been reconstructed.

Recovery v523 restores the saved staged tree
`19882c42fe9eff09bb1a47f3f1101d5a01cafa02` and verifies all 1,567 source files
against the surviving export v492. The 107,883,251-byte source includes both
complete LFS assets, checked against their Git oids and sizes. The recovery
receipt is 244,234 bytes, SHA-256
`bb1158de42753a7acade00e3ce13d22d3c2cca135d58e2cb7a37a8755933428e`.
Deleted unstaged work and old qualification receipts are not recovered.

Both replacement producers v543 start without an image and copy observed
surviving v503 source, generated files and objects into new roots. Normal Make
runs with host C/assembly commands forbidden, followed by forced compilation
and linking of hello, ls and cat. Every child closes successfully. All sixteen
artifacts, 431 typed link inputs, the complete version-5 ABI, user objects and
executables, and the embedded manual match across hosts. The new image on each
host is 209,715,200 bytes, SHA-256
`151145dbb680bf9ee256f018bcbf788b58dddfdc2d041c936cfcc6602d19e1b9`.
The manual is 70,539 bytes, SHA-256
`f17e015ec1868c4f8c916b23de6bf28feda37fcd967a6ab0d174a3c7c177ae6d`.

Each host selects the same 291 production, serial, policy, audit and
seed/manifest methods. Windows executes 286 and declares only the five named
POSIX pthread skips; Linux executes all 291. The new eight strict 180-second
four-CPU max/e1000 boots pass: kernel raw disassembly followed by JIT ls, then
separate hello, ls and cat boots on each host. The complete SMP/crypto/network
contract and each external program's PID, output count, FNV checksum, exit
and lease release remain checked. The 37 WSL calls used by Windows tests have
closed Linux owners and process groups.

Independent Linux v565 and Windows v583 checks pass. The paired v584 verifier
rereads captured content before final native file/directory membership,
ancestor and file identities. Its owned Windows Job and admitted single-process
Linux reader close successfully, with a silent Linux zero exit acknowledging
the final native checks. Both terminal review axes pass.

| New retained receipt | Bytes | SHA-256 |
| --- | ---: | --- |
| `user-link-requalification-windows-v543.json` | 5,188,039 | `e507a7136885b80773a762873f2ee58b4ef50d77011072d4db252a75d793176e` |
| `user-link-requalification-linux-v543.json` | 4,604,050 | `78eae91450e6e9b4905c04354433b0eecad94d48f44f5db864c8e85c81c0fc6f` |
| `user-link-requalification-windows-terminal-v583.json` | 3,367,681 | `89a38ea817263925f1f5f5e2f5bd91e3d7844b8bd96bd77d49b8cbf00df3f128` |
| `user-link-requalification-linux-terminal-v565.json` | 3,042,214 | `116093c9afdda9b30444e7526d212517f5b6e4a1ef5925a8055ac5299f5f4963` |
| `paired-user-link-requalification-independent-v584.json` | 3,204,720 | `1ad6daec163498c95d9adf8501fb4d0d5941c7a92e74da55d331390fd667557f` |

This qualifies image/users/runtime from observed surviving build inputs.
It does not establish a fresh clean kernel or complete six-tool bootstrap,
and its 291-method selection does not replace the deleted 273-method
seed/provenance evidence. Historical Windows v505 passed four boots; historical
Linux v505 failed at its missing-source check before runtime began. The new
paired result supplies its own evidence. Full Doom acceptance remains open.

The new failed attempts remain separately recorded. Linux v551 rejects six
generated import-cache files in the frozen source. Archive v562 preserves
those exact files and restores original source membership without replacing
source bytes or identities. Windows v565 fails before inspector launch because
the command line is too large. The replacement inspector uses a bounded,
byte-checked request file and explicitly selects Ubuntu. Windows v579 fails
on a proc entry disappearing during a stat read. Captured-function controls
reproduce the old failure and verify narrow ENOENT/ESRCH handling while
permission and I/O failures still propagate. Those corrections lead to v583;
they do not reclassify either failed verification.

## Historical release and adoption observations

Complete paired default self-consumption checks 228 stage artifacts, 76
final-stage pairs and 7,300 behavior files. Long checks 231 artifacts, 77 pairs
and 7,300 behavior files. Both terminal review axes pass. Their now-unavailable
receipts were `paired-complete-self-consumption-qualifications-default-independent-v242.json`
and `paired-complete-self-consumption-qualifications-long-independent-v266.json`.

The isolated adoption cohort passes its 273-method Linux seed/provenance suite,
with 17 expected platform skips. The original Windows full invocation remains
failed at its inner 3,000-second timeout. Independent combined acceptance binds
its 272 successful methods and one separately successful focused retry with a
7,200-second deadline. It accepts all 273 distinct methods; it does not turn
the original invocation into a success or establish the timeout's cause.

Linux completes the 90-input, 78-producer-input, 22-artifact publication with
67 comparison pairs. Both fresh normal OS builds complete successfully and
produce identical 431-input link cohorts, all sixteen declared artifacts and
the same 200 MiB image. Forced user Make builds preserve all three accepted
objects/executables and the complete version-5 ABI: 103 fields, 101 providers
and a 412-byte i386 table. Windows v414 and Linux v413 recorded those results.

The public integration copies nineteen implementation, seed, policy and test
files from that accepted cohort. Twelve Make binding methods pass again in the
public worktree. Canonical audit regeneration succeeds through `make bootstrap-audit`.

## Terminal completion race

The first paired four-CPU runtime attempt fails on both hosts. Windows `hello`
loses its strict PID/value match when a raw terminal completion message splits
the syscall record. Linux `hello` and `ls` pass, but `cat` loses its checksum
match for the same reason. Both original v427 failed receipts and serial logs
were recorded before deletion and are now unavailable. Relaxing the matching
rules would hide the defect.

`shell_gui_run_pending_command` now sends its completion message through the
existing `serial_printf` formatter. The formatter already shares the BKL with
syscall logging. The serial driver and panic code retain their original bytes.
Command execution, clearing and prompt ordering stay in the same function.

The deterministic POSIX regression compiles that actual function body with the
real serial driver and port/BKL doubles. It forces completion output during a
PID/value record and during a byte-count/checksum record. Both cases fail with
the old call and pass with the fix. Idle behavior, another formatted record
and output before BKL initialization also pass. Windows explicitly skips this
pthread fixture; actual Windows compilation and guest runtime remain separate
requirements.

A broader attempt to guard raw serial entry points was rejected. Panic may
need those entry points while another CPU holds the BKL, and HEADLESS panic
printing can reach them indirectly. Direct API tests did not cover that
indirect path. The adopted change uses the existing formatter at the one
completion call.

## Historical kernel preparation

The earlier kernel source export contains 1,566 staged-index files. It materializes both
retained LFS payloads from the accepted source and verifies each Git LFS oid and
size. Unrelated worktree-only user changes remain excluded. Relative to the
fresh adoption cohort, only `kernel/lang/shell.cc` changes among C sources,
headers and assembly. The updated embedded manual describes the release,
native user links, remaining Python actions and record serialization.

Both v485 kernel preparations copy byte-verified retained inputs into new
roots and complete normal Make. All 431 link inputs and all three kernel
outputs match across hosts. The complete updated manual is embedded.

| Measured output | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.bin` | 9,583,576 | `06359f297f0e458d75a03095f7e5183efa4a9128b9a7391d3d829519cb461555` |
| `kernel/kernel.elf` | 9,810,364 | `f0ca37d93fcca68b0d5a061c9f365d4cc8adcf47a92b267fd0f2882306d0ed6f` |
| `kernel/kernel.elf.pass1` | 9,679,292 | `fcc098fd66c14308d111f36bbda8c46d49bac8a1d0c9af8a8b9883eb7fff62a9` |

Only the raw-kernel exact-size row changes; both measured ELF sizes remain
unchanged. The other thirteen policy rows retain their accepted values.
Normal image and forced-user gates use a separate canonical source export
with this policy and the added adoption guide; C/header/assembly bytes and
the embedded manual must match v485 completely. Those historical receipts
are unavailable. The replacement qualification above checks the current
artifacts and manual independently and passes the strict four-CPU runtime,
including each external program's PID, output/checksum, exit and lease release.

Full Doom gameplay, audio, save/load, reboot and performance acceptance remains
open, as does the remaining Python coordination work. TempleOS is read-only
reference material and stays outside build and progress metrics.

## Private persistent-image suffix progress

The suffix primitive preserves an existing disk after replacing only the
pre-FAT prefix. Native v529 passes twenty-two Windows cases with its declared
POSIX interrupted-I/O skip and all twenty-three Linux cases. Native v564 then
passes seven large cases per host using the actual disk template and a populated
200 MiB FAT16 image. Independent FAT-chain and tail reads establish persistent
file and suffix preservation. Equal publication preserves identity and mtime;
write, read, flush and close failures leave the old output unchanged.

Fresh checked builds v589 use installed CupidC, CupidASM, CupidLD and CupidDis,
with host compiler/assembler commands forbidden and no retained objects reused.
Windows compiles/assembles and certifies eighteen objects, then passes
twenty-two cases plus the POSIX skip. Linux certifies twelve objects and
passes all twenty-three. Its static i386 path exercises the raw seek and
interrupted-I/O adapter; Windows uses the exact native PE32 import profile.
Both terminal review axes pass this private small-case scope.

The largest native large-publication allocation is 10,697,217 bytes; the
16 MiB guard limits a single allocation, not total process memory. The earlier
large read-error check proves rejection, not a failure after a partial suffix
copy. Checked large-disk coverage and a guarded coordinator for template
generation, persistent FAT staging and publication remain required. Normal
image ownership stays with Python, and the graph remains 447/five.

New recovery and qualification evidence is collected under
`build/bootstrap/native-profile-validation-258bb5f3/`. The deleted historical
receipts are unavailable.
Public checks include `make check-bootstrap-audit`, the two user production test
modules and `python3 -B -m unittest tests.test_terminal_serial_record` on a POSIX
host with Clang.
