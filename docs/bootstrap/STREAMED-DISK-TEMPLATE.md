# Bounded CupidObj disk-template producer

This source increment adds `ctool_obj_stream_disk_template` to CupidObj. It
produces the pristine prefix through the FAT16 root directory without allocating
the complete prefix. The existing buffered transform and the new entry share
CupidObj's layout selection and byte author. `disk_image.cc` retains its separate
template validator and renderer, and the Python author remains independent.

The hosted `disk-template` CLI still buffers its sources and output. Its 64 MiB
limits, the checked capture path's 60-second producer deadline, the normal Python
image coordinator, installed seeds, build-plan source counts and default Make
edges are unchanged. This API alone does not adopt a new image owner or qualify
the retained publisher for wider geometry.

## Interface and lifetime

`ctool_obj_disk_source_t` contains a borrowed context, a `ctool_u64` size and a
synchronous `read_exact(context, offset, bytes, count)` callback. The stream
request supplies bootloader and kernel sources, the existing `ctool_u32` image
sector count and FAT start LBA, and a synchronous `write_exact` callback.
`ctool_obj_disk_template_stream_result_t` reports a `ctool_u64` completed byte
count and an `attempted_write` flag.

Both callbacks transfer exactly `count` bytes on `CTOOL_OK`. Any other status
aborts the call and propagates unchanged. Reads stay within the declared source
size. Bootloader reads cover bytes 0 through 445 and 512 through 2559; the MBR
partition table and signature are authored independently. Kernel reads cover its
complete declared extent in increasing order. Only an empty kernel may omit its
reader. Bootloader bytes beyond the five-sector prefix are not consumed.

Complete semantic admission precedes the first source or sink callback: required
callbacks, the five-sector boot minimum, FAT/image ordering, kernel containment,
addition overflow and a valid FAT16 layout. There is no interpretation of kernel
payload bytes. Callback I/O can fail after admission, including after earlier
successful writes.

Sink calls start at offset zero and are contiguous, ordered and nonempty. Each
contains at most `CTOOL_OBJ_DISK_TEMPLATE_CHUNK_BYTES`, currently 65,536 bytes;
the last call may be shorter. Success includes the entire pristine prefix:
boot/kernel bytes, the zero gap, patched MBR, FAT16 BPB, both initialized FATs and
the empty root. It does not emit the remaining image data area.

Each invocation uses a 65,536-byte automatic transfer array, a 512-byte automatic
metadata array and fixed scalar/layout state. It allocates no arena or heap
storage. These are producer workspace figures, excluding the caller's contexts,
callback frames and any storage the sink chooses to retain. Offsets and source
sizes are `u64`; transfer counts remain bounded `u32`. The existing `u32` sector
domain bounds disk positions below 2 TiB, and the admitted layout must fit within
the requested image. A declared kernel addition that would overflow `u64`
returns `CTOOL_ERR_OVERFLOW`; malformed geometry returns `CTOOL_ERR_INPUT`.
Missing required request/result/callback arguments return
`CTOOL_ERR_INVALID_ARGUMENT`.

Request, source, sink and result storage must be disjoint. Sources remain stable
for the whole call. Callbacks must neither reenter the producer nor mutate the
request or source views. They must not retain or alias the transfer array;
borrowed byte pointers expire when each callback returns. Independent calls
retain no shared state and can be retried with valid callback contexts.

Every failure leaves `result.bytes` zero. The producer sets `attempted_write`
before each sink attempt, including one that physically writes part of a chunk
and then fails. A later source failure also leaves partial output. The caller
must discard all partial output in either case; the API cannot rewind a sink.
The flag describes an attempted callback, not durability, ownership or
publication. The entry has no job diagnostics, input capture, file flush or
publication behavior.

## Buffered compatibility

`CTOOL_OBJ_BUILD_DISK_TEMPLATE` retains its request validation order, diagnostic
text and source attribution. It still rejects template extents above the i386
buffer domain before reservation and honors the configured buffer/arena limits.
After admission and reservation it renders the same ranges directly into the
owned buffer. Transform rollback, result clearing and same-job recovery remain
the existing transform's responsibility. The new callback entry does not widen
any buffered or hosted limit.

The existing CupidObj contract adds useful mixed-error checks: a missing kernel
precedes malformed boot bytes and bad geometry, malformed boot bytes precede
geometry, and geometry precedes a short boot. Kernel overlap still names the
kernel input and its exact LBA diagnostic. Existing buffered layout, output-limit,
i386-overflow, rollback and recovery cases remain present.

## Qualification source and current evidence

`toolchain/tests/cupidobj_disk_stream_contract.cc` is a standalone caller shared
by native and supplied Cupid-built executables. Its independent byte oracle
checks every produced byte. The span oracle classifies and clips u64 regions
once per callback span, then compares every byte with bounded u32 loops against
source bytes, independent MBR/BPB arrays, FAT literals or fixed fills. It does
not sample bytes or shorten the logical traversal. All six buffered parity
fixtures check every buffered byte with both the original `expected_byte`
oracle and the span oracle before comparing the stream. Eleven selectors cover:

| Selection | Contract |
| --- | --- |
| `parity-small` | 38,400-byte buffered/stream parity, every region |
| `parity-active` | 10,697,216-byte active-layout parity |
| `fat-cycle` | Existing two-state FAT-size cycle selects the next cluster size |
| `exact-fit` | Kernel ends exactly at the FAT boundary |
| `empty-kernel` | Missing reader allowed for zero kernel bytes |
| `chunk-boundary` | Complete extent is exactly one 65,536-byte chunk |
| `large-prefix` | Complete 67,143,168-byte logical output, above 64 MiB |
| `wide-u64` | Complete 4,295,132,672-byte logical output and 4,295,095,808-byte synthetic kernel; both callback offsets cross `UINT32_MAX` |
| `admission` | Eleven invalid request/geometry/overflow cases before any callback, then recovery |
| `sink-failures` | First, middle and final callback failure after simulated partial consumption, followed by recovery |
| `source-failures` | Initial boot failure and later kernel failure after a successful write, followed by recovery |

The two large cases generate and inspect bytes through bounded callbacks. They
write no multi-gigabyte file and prove no image publication or sustained physical
I/O capacity. Their declared initial execution bounds are 180 seconds per ordinary
selector and 600 seconds for `wide-u64`. These are contract qualification bounds;
no production timeout changes.

`tests/test_cupidobj_disk_stream.py` normally builds the standalone caller from
`ctool.cc`, `ctool_host.cc`, `elf32.cc` and `cupidobj.cc` using strict native C11
flags. Clang receives `--no-default-config`. Set
`CUPIDOBJ_DISK_STREAM_PROGRAM` to one absolute existing executable to suppress
native compilation and run the identical selectors against a Cupid-built caller.
Set `CUPIDOBJ_DISK_STREAM_PRODUCTS` to a fresh absolute directory outside the
checkout to retain products and exclusive command receipts. With no products
setting, a fresh temporary directory is retained. The driver never builds a
Cupid compiler, changes a seed or silently falls back from a supplied caller.

The first qualification selection closed with 19 native tests passing. Current
CupidC, CupidASM and CupidLD builds and both existing buffered contracts passed.
Checked active-layout, above-64-MiB prefix, source-failure and sink-failure cases
completed individually with return code zero. The checked test
selection reached its aggregate 600-second deadline before the 4 GiB case
completed, so that checked attempt remained incomplete. No compiler
capability error was observed. The timeout does not establish its cause.

The subsequent test-only span-oracle revision leaves the production author,
all eleven selectors, logical extents, callback traversal, negative assertions
and 180/600-second bounds unchanged. Fresh Windows qualification passes all
19 native methods and all 11 supplied Cupid-built methods. Its 16 checked
commands also pass both existing buffered contracts. The checked selection
completes in 77.241 unittest seconds; the whole checked invocation closes in
179.197 seconds with child status zero, null error and no remaining owned
processes.

The wide case completes all 4,295,132,672 sink bytes and all 4,295,095,808 kernel
bytes. Both highest callback offsets exceed `UINT32_MAX`. Admission reports
eleven rejections before callbacks, and every sink/source failure trial recovers.
Independent evidence checking verifies 4,263 inputs, eight source files, 65
distinct source copies, all command receipts and retained products, with full
model guards repeated after reporting. Known exclusive writers and unknown
original writers remain distinct.

The original caller, document, receipts and products remain retained separately.
Its timeout remains a failed result and establishes no compiler or runtime
cause. This Windows result precedes the fresh paired qualification below.
The new API does not extend the buffered CLI or qualify a hosted streaming adapter.

## Linux setup failure and continuation hold

The fresh Linux attempt completes its existing buffered native regression, then
fails while compiling the streaming contract during unittest setup. GCC 13
reports `-Werror=maybe-uninitialized` for the uninitialized boot fixture
`ctool_u8 memory[2560]` passed to `init_request`. Unittest reports zero executed methods; no API runtime
result follows from that failure. A separately retained caller initializes this
array with `{0}`. Both independent source reviews pass. That six-byte fixture
change leaves every selector, assertion, extent, flag and deadline unchanged;
the fresh native and supplied-program qualification below covers the corrected
caller on both hosts.

The earlier Linux failure stays unclassified because its original `/tmp` raw
evidence disappeared. It is separate from the retained GCC setup failure.
Neither result supports a compiler or runtime defect claim.

A later Windows check finds held-handle `ctime_ns` drift in the frozen source
cohort while contents and other checked metadata still match. The original
expected models remain unchanged and current continuity is on HOLD. A fresh
private source cohort must record its own exclusive creation identities and
admission; byte ancestry does not restore the old custody chain. Completed
Windows results above remain saved historical evidence.

## Fresh Windows and Linux qualification

The current 20,102-byte standalone caller initializes the 2,560-byte boot
fixture with `{0}`. This six-byte test change resolves the retained GCC 13 setup
failure without changing production source, selectors, assertions, extents or
deadlines. The authoritative source now contains the qualified caller.

| Host and caller | Passing selection | Additional regression evidence |
| --- | --- | --- |
| Windows native | 19 methods | Buffered contracts included in the selection |
| Windows Cupid-built | All 11 stream methods | All 16 commands succeed, including both buffered contracts |
| Linux native | All 11 stream methods, 5.610 unittest seconds | Separate buffered build and regression pass; all three commands succeed |
| Linux Cupid-built | All 11 stream methods, 92.644 unittest seconds | Separate buffered regression passes; all 12 commands succeed |

The fresh Windows checked selection takes 81.649 unittest seconds and its
complete invocation closes in 187.663 seconds. Both Windows invocations close
with child status zero, null controller error and no remaining owned processes.
Independent checking validates the 4,263 inputs and complete command/product
joins after closure. This is a fresh custody chain; it does not repair the
historical held-handle metadata discontinuity.

Both Linux invocations also close with child status zero, null controller error
and separate successful Linux and Windows cleanup records. Native execution
takes 103.027 seconds for the complete invocation; checked execution takes
445.417 seconds. Independent checking validates 407 unchanged original inputs,
34 copied inputs, six seed roles and all 104 exported raw artifacts. The 35
native and 69 checked bodies total 15,922,519 bytes. Saved full models and
creation identities remain distinct; unknown native writers remain null.

Every case agrees between native and Cupid-built Linux callers. Six fixtures
retain complete buffered parity. The large-prefix case checks all 67,143,168
bytes. The wide case checks all 4,295,132,672 output bytes and all 4,295,095,808
kernel bytes in 65,539 writes. Its highest read offset is 4,295,030,272 and its
highest write offset is 4,295,098,368. Eleven admission rejections occur before
callbacks, then recover. All three sink-failure trials and both source-failure
trials also recover. The wide cases retain bounded callback storage and create
no multi-gigabyte output file.

These results qualify the callback API and its supplied callers. They leave the
earlier timeout, Linux setup failure, missing raw evidence and historical
metadata continuity HOLD unchanged. They establish neither image publication
nor a hosted streaming adapter, new installed seeds or coordinator ownership.

## Windows image and stock boot evidence

The OS checks use the existing image coordinator and buffered producer route.
A fresh ordinary Windows copy retains the 451 compiled products from the
initial stock Make run. The reviewed completion invokes only the sixteen-row
size check and image publisher; compilation, linking and regeneration do not
run again. Completion returns zero and publishes a 209,715,200-byte image with
SHA-256 `7707a8cd57ad7997cdd5b5d015708b8cf6a1b113575074d65f673abeb866a3b0`.
The saved image readback checks its boot sectors and full kernel.

A separate stock private-image boot uses e1000, four CPUs, CPU max and one `ls`
command under the original 300-second harness limit. Child and qualification
statuses are zero, with no controller error or remaining owned processes. All
62 TLS checks, CPU/FPU markers and one ordered compile/JIT/terminal completion
pass. The invocation records 79.865 seconds; its private temporary directory is
empty after cleanup. Image, serial and transient-copy native writers remain
unknown.

The source size policy adopts kernel.bin 9,405,468 bytes, kernel.elf 9,635,472
bytes and kernel.elf.pass1 9,504,400 bytes. The other thirteen rows and all twelve
seed binaries retain their prior values. The original Windows and Linux stock
Make runs remain failed at their stale size checks. This evidence establishes
copied-output completion and one Windows stock boot.

## Linux copied-output image evidence

The earlier Linux preparation copies 2,722 payloads and six controls, but its
completion rejects `/etc/ld.so.conf.d/ld.wsl.conf` before a supervisor or Make
process starts. The saved partial cohort still matches its preparation models.
The changed field and cause remain unknown; that failed attempt is preserved.

A separate fresh Linux preparation and completion pass. The completion retains
451 compiled products and uses four explicit Make `-o` masks for the copied boot
and kernel outputs. All sixteen artifact rows pass. The existing buffered image
publisher produces 209,715,200 bytes, matching the Windows image byte for byte
with the SHA-256 above. Readback checks boot/kernel placement. Compilation,
linking and regeneration do not run again.
Child, terminal and qualification statuses are zero, with no controller error;
Linux cleanup and Windows cleanup separately report no remaining owned work.

A later bounded export preserves all 10,398 bytes of the closed Make, supervisor
and control logs. Independent readers and Root reparse the Make trace as nine
variable assignments followed by the exact size check, its sixteen-artifact
success, one existing image-publisher command and its two success records.
The raw supervisor document equals the supervisor document embedded in the
completion receipt. These are closed records; their later export establishes
no continuous filesystem state.

The original plain Windows2/Linux2 Make runs remain failed at their stale size
checks. Linux stock guest qualification remains open. Four separate QEMU
metadata attempts have failed; the latest stops in strict ELF parsing of
`/usr/share/qemu/s390-ccw.img`. Its actual header and full role ledger remain
unknown, and no attempt supplies an accepted metadata or QEMU availability
report.

The [Windows OS record](evidence/bounded-host-api-windows-os-qualification-20261016.json.gz)
retains the Windows completion, harness and serial bodies, plus the earlier
Linux failure. The [Linux OS record](evidence/bounded-host-api-linux-os-qualification-20261017.json.gz)
contains the fresh plans, receipts, full file models, source joins and exported
raw logs. Standalone API qualification on both hosts remains recorded above.
Hosted callback integration, SDK publication, installed seeds and normal
coordinator ownership retain their separate scope.

## Remaining integration

A hosted streaming producer route still needs retained input admission, guarded
u64 readers, an owned private output sink and its failure cleanup, and a checked
capture handoff with separately qualified limits. The current inherited-FD CLI
route buffers payloads and does not provide that capability. No such adapter is
added here. The normal coordinator's other retained source, image and capacity
dependencies remain separate; this producer does not replace their validation.
