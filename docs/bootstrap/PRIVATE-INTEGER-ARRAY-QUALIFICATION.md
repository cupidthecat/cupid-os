# Combined private halfword and array qualification

The final candidate combines real two-byte private integer storage, eight
fixed network signatures, typed constant integer array lists and the review
repairs for scalar strides, multidimensional `sizeof`, SIMD broadcasts and
cast diagnostics. It retains the existing unsigned-word representation and
four-byte cdecl slots.

## Qualified capability

`short` and `I16` are signed two-byte objects; `unsigned short` and `U16` are
unsigned two-byte objects. Each has two-byte alignment. Pointers and fixed
cdecl argument slots remain four bytes. Loads, stores, casts and results use
the declared signedness and low sixteen bits. Both halfword types promote to
`int` for default arguments. Existing unsigned-word widening remains intact.
Scalar pointer arithmetic uses the declared stride, array descent retains the
remaining row shape for `sizeof`, and the two SIMD broadcast helpers accept
halfword scalars. Explicit halfword casts accept arithmetic scalars and
represented object or function addresses; vector, record and void operands
receive errors.

Fixed native signatures cover `htons`, `ntohs`, `bind`, `connect`, `accept`,
`sendto`, `recvfrom` and `udp_send_raw`. Ports use halfwords; descriptors and
error results remain signed words, while IPs and lengths retain unsigned words.
Peer-port outputs write two bytes through `U16 *`. Positive native oracles
inspect complete outgoing stack words, neighboring elements and results with
unrelated upper EAX bits. Named calls, record callbacks and class methods
normalize signed and unsigned halfword results. Negative cases check arity,
incompatible arguments and recovery in the same compiler state.

Global, block-static and persistent REPL integer arrays use the shared flat
constant-list helper. It reserves zeroed storage and writes the declared one-,
two- or four-byte little-endian elements. Partial lists retain omitted zeros;
character constants, escapes, arithmetic constants and trailing commas use
the existing constant grammar. Array typedefs follow the same path. Block-static
array data initializes once and retains later changes. A failed REPL declaration
restores earlier data and names, preserves existing arrays and permits reuse
of the rejected name. Rejections cover excess elements, nonconstant calls and
division by zero in static JIT, static AOT and persistent REPL contexts.

The array qualification covers flat integer lists. Nested lists, inferred
bounds, pointer difference and indirect integer `++` or `--` remain separate
requirements. Direct scalar updates and supported compound assignment retain
their existing behavior. The separately recorded scalar-string and repeated
block-static scalar initialization limitations are outside this array work.
The guest network checks complement the positive native ABI oracles; they do
not qualify a connected peer or successful packet-transfer path. The earlier
[type](PRIVATE-HALFWORD-TYPES.md), [binding](PRIVATE-HALFWORD-BINDINGS.md) and
[initializer](PRIVATE-INTEGER-ARRAY-INITIALIZERS.md) records retain their failures
and narrower historical acceptance scopes.

## Build and runtime evidence

Both final four-module selections pass 537 methods. Windows records 84.286
unittest seconds with no skips; Linux records 8.701 seconds with eight
Windows-only skips. The separate actual Feature 23 helper oracles execute JIT
and AOT on both hosts. The nine compiler, guest and harness input files retain
their final reviewed bytes.

Both final kernel measurements succeed with conventional producer variables
forbidden. Their three complete products match:

| Product | Bytes | SHA-256 |
| --- | ---: | --- |
| Flat kernel | 9,369,664 | `4c1f8735c8383bab8a344654f2678b6fbe9691369d8652420041e1c01019e2b7` |
| Linked kernel | 9,598,000 | `988c4bc139dc74dafe17a2cac05edfa179f557b27215182f0923ab8110338a04` |
| First-pass kernel | 9,466,928 | `b1200f67a5f433eb400c02e0a1ec488cb6bddc0263386f7f9604f56f2c3654e9` |

The final parser object has 487,140 bytes and SHA-256
`70615a3ff05fe34ad1f5340d0f09b4c8403970735ad39e5854caee721a6a694c`.
Earlier object and kernel measurements describe earlier candidate bytes and
do not qualify this final version.

Windows completes the ordinary OS and user builds, the 103-field/101-provider
ABI check and all sixteen exact artifact checks. Its four-CPU guest completes
all 31 commands and two Feature 17 executions in the same guest. The three
Feature 23 binding, initializer and halfword-type markers each occur twice
for their JIT and AOT executions.

The retained Windows originals archive has 55,315,441 bytes and SHA-256
`61fcd46c16b3aa88a07818cfe107f5ff7dffaf9cc13dac26f2b6b17c21ec2386`.
Every one of its 2,751 members is independently reread, including retained
source payloads and archive references, 439 products, the 209,715,200-byte
image, helper oracles and logs.

Two ordinary Linux attempts fail around Doom discovery. Successful detailed,
sparse and first-failure-only diagnostic Make runs do not reclassify those
failures as accepted normal builds. The source-preserving experiments leave
the input guards intact and have not identified the intermittent cause.

Ordinary Linux R3 uses a separate exact 2,238-input verification root. It
keeps the original canonical audit, OS, user, ABI and artifact commands,
producer sentinels and bounds. It passes in 1,558.759 runner seconds, including
the uninstrumented Doom compilation, both normal build commands, the
103-field/101-provider ABI check and all sixteen artifact gates. Every source
control and all three previously measured kernel products remain unchanged;
command cleanup reports no errors.

Independent comparison rereads all 439 complete product pairs and all
209,715,200 image bytes. Each pair agrees between hosts. Eight products differ
from the accepted storage version. The image has SHA-256
`0acf66a2bb72c67d36d62d3369399ce4ac5da5df17fb2e781eb2de1ea868d868`.
The normal originals archive has 46,567,804 bytes and SHA-256
`44f2fb1d684697c0bbdbb693430b00ad77a47cced950bdc6b2c8a2d6566f6aee`.
All 2,834 archive members are reread. They retain both failed ordinary Linux
attempts and the original diagnostic recipes and trace limitations.

The independent Linux guest uses a frozen diagnostic image that matches all accepted Windows image
bytes. It passes the USB fixture, all 31 frontier commands and two Feature 17
executions in the same guest in 1,039.873 runner seconds. The three exact
Feature 23 markers each occur twice. Its final checks confirm unchanged source
and image bytes and no command-cleanup errors.

The runtime collector rereads both 2,238-input captures and both complete
images, checks every ordered command pattern with its captured graphics PID,
and archives the original serial logs, output streams, receipts and recipes.
All 34 runtime archive members are reread. The archive has 795,201 bytes and SHA-256
`5e131e3530fbe52c7a9233b8e8f350e5fd415aaa9748ff80e519a59ed12b39d5`.
The Linux runtime result retains its diagnostic-image provenance. The
separate ordinary R3 receipt establishes normal-build acceptance for the
same image bytes.

## Working-tree adoption and remaining acceptance

The protected Windows baseline closes with one failure and 72 skips among
3,476 tests, without drift among its 2,260 inputs. Its strict behavior-stage
comparison rejects the reviewed installed seed cohort. The retained failure
and production-authorization controls support the reviewed reversible
working-tree transition; they do not reclassify the failed suite or recover
its deleted stage-tool payloads.

The seventeen-file seed projection is applied and both root seed verifiers
pass. The private source adopter applies thirteen patch files within fourteen
controlled paths, including the unchanged oracle. All nine candidate inputs
and all 99 hosted producer inputs retain their expected bytes. The measured
frontier test adopts exactly six constants and preserves all other AST nodes.
These operations pass their working-tree guards. A later five-test-only
repair also passes its guarded adoption; the final acceptance cohort has
2,265 source controls. The original protected baseline and later failed
acceptance attempts remain failed.

Both full forty-method modules pass: Windows takes 2,151.788 runner seconds
and Linux 1,803.778. Their archive passes all 32 member rereads. It has 871,925
bytes and SHA-256
`9fcafbc865871c11f2346e2889d64f6b1eef8f3f25257bb39244e3d9a36cbeb2`.
The [module index](evidence/verification-halfword-array-frontier-modules-20261012.json.gz)
binds the original logs, exact candidates and source maps. Source readback
restorations recover accepted bytes, with the limits recorded in the
[protected baseline record](PROTECTED-BASELINE-SEED-TRANSITION.md).

Windows acceptance combines eight retained build/guest command results with
one fresh original 3,526-method suite, including its original 72 skips.
Linux acceptance covers a fresh forced-native normal rebuild and guest
checks; no complete Linux suite is claimed. See the [final five-test proof](evidence/verification-final-root-combined-five-test-acceptance-20261013.json.gz).

Linux captures 439 held engineering products after Windows aggregate PASS
and compares its forced-native outputs against those exact raw bytes.
The Windows prefix records three measured kernels and the image; it does
not record all 439 products. The collector archives the 439 snapshot
payloads, three measured kernels and GUI contract. Image identity and
executed whole-byte equality are retained; image and USB fixture payloads
are omitted. The collector checks retained source maps, without a new
source-file readback or a complete source archive.

The accepted CTXT and all 99 producer inputs stay unchanged. Normal
ownership remains 449 CupidBuild actions and three Python coordinators.
TempleOS remains read-only reference material, excluded from these counts.
Full self-hosting and source-ownership transfer remain unfinished. Exact
final documentation/source closure still precedes a coherent commit.

## Input-delivery failures during final acceptance

The first engineering Windows attempt passes its normal build, ABI and artifact
checks, then fails the original Feature 17 repeat. The requested
`/bin/feature17_iso.cc` reaches shell dispatch as `/bin/featre17_iso.cc`.
The subsequent preprocessing failure and preceding REPL parse error do not
establish a compiler regression: shell fallback can parse a nonexistent path
as input. All 2,265 frozen controls and the guarded image remain unchanged.
The original failed receipt, measured kernel products and logs are preserved
in a fully reread 58-member archive.

A focused monitor trace reproduces a second loss: the first execution passes,
but the second command arrives as `/bin/feature17_iso.c`. The retained traffic
contains every requested key and monitor acknowledgement without a host send
error. This localizes the observation after host submission; it does not
identify the failing guest or emulator component. Its 21-member preservation
archive retains the failed diagnostic as failed.

Later passive diagnostics pass both executions. A low-volume QEMU trace records
98 key transitions and 98 HID queue-empty events. The latter mark processing of
the final queued scancode in QEMU, not empty USB reports or guest callbacks.
USB capture records all 98 report-state changes; native attachment events
identify the keyboard's EHCI path. The first capture has 3,151 packets and 158 successful
eight-byte keyboard completions. The lighter capture removes four high-volume
EHCI events and retains 3,354 packets, 155 successful keyboard completions and
the same 98 state changes. Both captures contain the requested `u` and repeated
`c` press/release sequences and no failed or short keyboard completions.

These successful captures do not explain the earlier losses. QEMU input,
device reports, the guest completion callback, keyboard queue and terminal
routing remain separate observation points. Capture I/O can alter timing, and
the clock anchors do not calibrate the host and emulator clock domains.
The 49- and 54-member archives preserve the two capture runs and their limits.

Fresh engineering acceptance uses the original uninstrumented GUI entry point,
command cadence, success patterns and time bounds. Its five normal checks and
same-guest Feature 17 repeat pass. Windows aggregate acceptance later retains
those results and newly runs the complete suite. Linux freshly rebuilds
normal products and passes the same-guest repeat and full runtime frontier. The
original failure remains recorded and no input-delivery repair is claimed.

QEMU's pinned [HID implementation](https://raw.githubusercontent.com/qemu/qemu/840320663e/hw/input/hid.c),
[USB completion path](https://raw.githubusercontent.com/qemu/qemu/840320663e/hw/usb/core.c)
and [PCAP writer](https://raw.githubusercontent.com/qemu/qemu/840320663e/hw/usb/pcap.c)
define the observer boundaries. The executable's retained version and identity
bind the installed binary; the source references do not independently prove
its build provenance.

## Frontier capture aliases in retained evidence

The fresh Windows followup completes all eight build and guest commands with
zero exit status. The original GUI frontier runs all 31 commands and prints
its success message. The followup then fails in a separate reader of the
retained serial log, before the complete test suite starts. Its failed receipt
and raw command logs remain preserved in the fully reread 57-member archive.

The GUI stores a captured process identifier under the command's declared
name, `gfx_owner_pid`. The reader stores the regular expression's group name,
`pid`, instead. Resolving the later `exec /gfxhandoff_kill {pid}` command needs
the declared capture name and therefore raises a missing-capture error.

Replaying the original serial log reproduces that error after 22 matched
commands. A shared reader now uses the declared capture name and validates
the named `pid` group as a positive integer. The same log then matches all
31 commands with `gfx_owner_pid=4`. Three negative cases reject a missing
named group, a mismatched dependent process identifier and a truncated log.
This replay does not launch a guest or change the GUI harness, command order,
success patterns, input cadence or operating-system source.

The complete Windows suite runs separately after the repaired reader checks
the retained eight-command prefix. Its record identifies those eight commands
as retained evidence and the suite as the one command executed by the
continuation. Neither continuation nor replay changes the earlier failed
receipt to a passing receipt. The independent input-delivery losses remain
unexplained; this reader repair does not establish an input-delivery fix.

### Terminal suite failure and test fixture repairs

The first complete Windows run of the adopted halfword and integer-array
source finishes with ten failures and 72 skips across 3,526 methods in
15,263.354 seconds. All 2,265 source controls, the three measured kernels
and the accepted image remain unchanged. Owned process and acceptance-lock
cleanup succeed. The original receipt, tracebacks, raw logs, recipes and
guards are retained in the fully reread 36-member failure archive. The run
remains failed.

Six failed methods stop on inventory expectations left behind by the
adopted parser and Feature 23 changes. Ten constants in two tests need
the following measured values:

| Inventory | Earlier expectation | Current canonical audit |
| --- | ---: | ---: |
| `sizeof` occurrences and files | 7,111 / 191 | 7,122 / 192 |
| `return` occurrences | 28,024 | 28,077 |
| `for` occurrences | 5,024 | 5,025 |
| `while` occurrences | 3,089 | 3,090 |
| `switch` occurrences | 237 | 238 |
| `case` occurrences | 1,790 | 1,802 |
| `default` occurrences | 167 | 168 |
| `if` occurrences | 45,196 | 45,311 |
| `else` occurrences | 5,688 | 5,715 |

Every increment is accounted for by retained before/after source captures.
Feature 23 adds all eleven `sizeof` expressions and the one additional
file. The parser adds 32 returns, one for, one while, one switch, twelve
cases, one default, 69 ifs and 24 elses. Feature 23 adds the remaining
21 returns, 46 ifs and three elses. Other adopted source contributes no
count delta. The canonical audit bytes remain unchanged by this repair.

The two-test patch changes only these ten constants. Reversing the changes
reconstructs both original files and their ASTs exactly. All 141 build-audit
and 98 frontend methods, including 862 and 29 assertion calls, remain.
Both hosts reproduce the six original failures before the patch and pass
the same six methods afterward. The clean-manifest and deliberate
stale-include rejection paths still execute.

The complete original 239-method module selection passes on Windows in
1,506.580 seconds and on Linux in 1,179.050 seconds. Both raw logs end in
`OK`, with zero failures, errors or skips. Genuine child and launcher
receipts, all 2,265 source controls and owned cleanup agree with those
results. The Linux retry also has a persistent Windows launcher receipt
and matching before/after checks for the 2,260 tracked names, Git index
and six executable modes. Its module selection, environment and original
36,000-second deadline remain unchanged. Independent review reads all
83 members of the paired preservation and passes the exact patch and
both complete module results.

The first Linux full selection remains failed and incomplete. Its raw log
has 27 passing methods and no terminal summary; the tool session exits one,
and neither the Linux child nor launcher publishes a closure receipt.
That run and its post-interruption source checks remain in a separate
16-member preservation. The later successful retry does not replace it.
The user later reports "i restarted wsl". The response and boot observations
are retained without inferring the timing or cause of the earlier missing
`/tmp` copies. The interruption cause remains unproven by these receipts.

The other four failures occur while compiling extracted floating test
fixtures. The arithmetic, truth and update validators now depend on the
integer and halfword classifiers, and the fixtures omitted those functions
and the appended type values. The three-test repair extracts the actual
production dependencies and retains the existing emitter and validator
functions. It appends the four halfword enum values in production order.
Arithmetic accepts signed and unsigned halfword scalars and rejects their
pointers. Truth accepts all four scalar and pointer types. Direct updates
accept those types and still reject a halfword array.

All eleven original methods and their 30-second native compiler bounds
remain. Existing byte checks, aggregate/vector rejections, diagnostics and
recovery assertions remain. Windows and Linux each reproduce the same four
failures before the repair and pass all eleven methods afterward, with clean
owned cleanup and exact private source maps. The Windows runs also recheck
all engineering source bytes; the Linux receipts guard their private copy.
Independent review rereads all 32 retained members and passes the repair.
The host C compilers serve as the existing reference test oracles.

The float repair retains two failed Linux preparations: a helper-hash
transcription mismatch and an absent temporary source directory. Neither
executes tests. The inventory repair separately retains its absent `/tmp`
copy and a failed HOME-restoration metadata writer. The corrected metadata
writer rechecks the existing HOME fixture before the patch and tests run.
New exclusive HOME and `/var/tmp` copies pass their byte guards; they do
not restore the earlier locations or filesystem identities. The temporary
directory absence causes remain unknown.

The first reviewed adopter, prepared as r3, fails with
`KeyError('external_inputs')`. The legacy Windows receipt does not contain
that field. The failure occurs before the acceptance lock, backups or file
replacement. Its closure records no applied files, no source changes and
no rollback. The actual 2,265-source map still matches the original
boundary. The exact failed recipe, closure, reviews, protocol, helper and
inputs remain in a fully reread 112-member failure archive; the attempt
remains failed.

The r4 adopter reads the verified legacy `old.PINNED` map and retains the
separate prior-review loop. Read-only preflight calls the actual legacy
`preserved_boundary()` and executes those exact corrected loops. All eight
pinned inputs and two prior reviews pass their byte checks. The preflight
retains the 2,265-source boundary, 99 producer inputs and three kernel
product identities. It does not execute the adopter or a suite, build or
guest run. The new adopter uses an exclusive r2 output directory and
preserves the original failed attempt in its protocol.

The Windows, Linux and combined collector callers now bind the same r4
helper, protocol and recipe bytes and require distinct r4 review names.
All eight new adoption, Windows, Linux and collector reviews pass with the
exact current recipe, protocol and helper identities and no required findings.
One independent reviewer completes both review axes. Earlier review bytes and
all r1/r2/r3 prepared variants remain preserved. The Linux and collector preparations retain the original
13 and 21 reversible edits; reversing them reconstructs each original
recipe and AST, including its build, guest, collector scope and bounds.
The final r4 parent preparation archive contains 87 fully reread members.
Preparation supplies no new acceptance result.

The corrected guarded adoption retry closes PASS and applies exactly the
five reviewed test payloads. Its complete 2,265-source after map matches the
actual readback. All 99 producer inputs, installed seeds, accepted CTXT,
three measured kernels and image retain their exact identities. Lock and
temporary-file cleanup pass, and no rollback is needed. The adoption changes
no OS or compiler behavior and performs no staging or commit.

The fresh original Windows 3,526-method suite closes PASS in 16,274.214
unittest seconds with the original 72 skips. Its wrapper records
16,294.755 seconds and exit zero. Aggregate Windows acceptance combines
eight retained prefix command results with that one fresh suite command.
Linux separately passes the fresh forced-native normal rebuild and guest
checks. Its 439-product comparison uses raw held engineering products
captured after Windows aggregate PASS, rather than products recorded by
the Windows prefix. No complete Linux suite is claimed. The original ten-failure suite, failed adopter and
incomplete Linux run remain failed. These private module and adoption
results do not close the full self-hosting mission.

Evidence retained outside the engineering checkout:

- `final-root-windows-suite-failure-originals-20261013.zip`, 36 members,
  SHA-256 `21c300193cbee5ba22d0a88e8531b31437ed41a66fefcfa262871fedbf6f9f32`.
- `inventory-lock-repair-private-20261013/paired-qualified-originals.zip`,
  83 members, SHA-256
  `24da43fa812f09fca0336464d251e977b49ea4b7ed0c39e7f77268c13ce34634`.
- `inventory-lock-repair-review-20261013.json`, SHA-256
  `df6c338452865c994b47e248baf9a2a384cc62a271ed4d90c50dab33af3f9a9e`.
- `inventory-lock-repair-private-20261013/linux-full-interruption-originals.zip`,
  16 members, SHA-256
  `9309f6f7eb90684cefb47660e81dcb347551bbcc151421d7840326c1ee81390e`.
- `wsl-user-restart-response-20261013.json`, the retained user response;
  it does not reclassify the incomplete run or explain earlier directory loss.
- `float-oracle-repair-originals-20261013.zip`, 32 members,
  SHA-256 `226a5eacc562cccfaee84780badfdf0fc1fa9c0e07e4ed41bd8a5fb5e56cb02e`.
- `float-oracle-repair-review-20261013.json`, SHA-256
  `a0a7f1cc1043ba6c4762cd7f49d6d93d1e01b280f399da8ed0e908da05fac8be`.
- `root-five-test-repair-adoption-failure-originals-20261013.zip`, 112 members,
  SHA-256 `6c2f186b96987541852b73c20dc2fba147a453e09c5c2621e0a7c5ee9061f5c9`.
- `root-five-test-corrected-adoption-preflight-r4-20261013.json`, SHA-256
  `0cce546da5d753722cec4d6ff1bca5e68c602d1cd8f59f38d391334551aae88e`.
- `root-five-test-acceptance-preparation-preservation-r4-20261013.json`,
  SHA-256 `caeb5469a61ac8b7e4f260233fc0578d27d4455aa9d917f8fdc7cdfb81e378fc`.
- `root-five-test-acceptance-preparation-originals-r4-20261013.zip`, 87 members,
  SHA-256 `6088203e625d4d50161b6ba6c0fba38ad6041402fd285a0d34a197f31088f6eb`.

The successful retry is retained in
`root-five-test-repair-adoption-r2-20261013/closed.json`, SHA-256
`c638de785802df6bdb8d0a6466ae178f21e0e74aebcefbb89d40d46662822f07`.
The terminal Windows closure is retained in
`final-root-windows-five-test-suite-20261013/closed.json`, SHA-256
`dcc6da6559454eadca65f3237b8d8b4b732d334e1e58391408ca9b01301b3d66`.

The [final five-test proof](evidence/verification-final-root-combined-five-test-acceptance-20261013.json.gz)
binds the terminal host receipts, exact recipes, source maps and raw
archives. The collector archives all 439 snapshot payloads, three
measured kernels and the GUI contract. Image and USB fixture payloads
are omitted. Every original failed run and preparation remains failed;
full self-hosting remains unfinished.
