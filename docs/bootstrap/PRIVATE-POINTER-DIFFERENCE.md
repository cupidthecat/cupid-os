# Private object-pointer difference

Private CupidC source now subtracts compatible object pointers in element
units and returns a signed four-byte integer. Both addresses are evaluated once.
The emitter subtracts addresses and divides the signed byte distance by the
pointed-to size, including non-power-of-two record sizes. Character, halfword,
word, floating and record pointers are covered. Retained array row shapes must
agree. Different pointer types, distinct record identities, incompatible row
shapes and erased `void *` referents receive a diagnostic.

Windows and Linux native i386 JIT and AOT execution pass the complete
372-method call-ABI module (69.919 seconds on Windows and 7.545 on Linux). New cases cover negative, zero and one-past distances,
12-byte records, row strides, nested arithmetic, unsigned element types,
once-only calls and same-state recovery after each incompatible subtraction.
The original tests failed on byte distances and accepted incompatible pointers.
A first record rejection fixture used unsupported block record definitions;
it now declares its records at file scope so it reaches the intended check.

The checked Windows CupidC seed also compiles the changed parser through the
normal fixed kernel profile. Its standalone ELF32 object has 489,128 bytes and
SHA-256 `57eacfbcd698a93d4e94f05c0402e6196b7c6e4fe5e612a4ce73a741bddc9c58`.
The initial standalone invocation rejected its missing output parent; creating
that parent allowed the original compile operation to pass. A separate focused
persistent REPL test passes across separately declared arrays, returned pointers
and block-static halfword storage.

The Windows build completes compilation, both links and guarded flattening.
Its original Make command fails at the preceding raw-kernel size lock. After
measured calibration, the native sixteen-artifact check and the existing normal
image command pass. The passing follow-up covers policy and image publication;
the original Make command remains a failed run. Installed seeds and normal
build ownership are unchanged. Pointer provenance is a language precondition: both operands must
refer to the same array, including its one-past position, and the distance must
fit the signed result. No runtime provenance check is added. Indirect integer
updates and general pointer-to-array declarations remain separate work.

The separate Windows kernel-build and binding selection passes all 50 methods
in 132.745 seconds. The focused floating update, comparison, truth and binding
selection also passes all 25 methods. Neither selection proves an OS boot.

The checked Linux seed also compiles the parser. Its complete 489,128-byte
object is byte-identical to the checked Windows result, including the SHA-256
recorded above (in the pointer-difference capability record). The original
build-audit check fails because the directory-snapshot and disk-stream contract
sources lack explicit unreachable-source policy entries. Their standalone
Python runners compile those files outside the supported Make roots. The policy
now lists both as `not_reached`, matching the existing standalone contracts;
no build edge or compiler owner changes. Audit regeneration and the deterministic
check both pass. Three existing policy rejection methods also pass, including
missing ownership, classification drift and active/unreachable overlap.

The initial `make -j2 all` stops during Doom compilation with discovered-directory
closure and checked-seed input custody errors on `d_iwad.o` and `d_loop.o`.
No compiler diagnostic accompanies those errors. A focused unchanged rerun,
`make -j2 kernel/doom/src/d_iwad.o kernel/doom/src/d_loop.o`, passes both targets.
The original failure's precise changed input remains unclassified. The normal
build will continue with source, test and audit writers stopped; completed
objects are retained and all custody checks remain enabled.

The GUI smoke runner now types uppercase letters and the punctuation needed
for C source expressions, including parentheses, brackets, quotes and escapes.
The installed QEMU QMP schema confirms the selected key names. All 147 runner
methods pass, including typing a complete pointer-difference block and
rejecting unsupported input before sending any key. This host harness evidence
is separate from the guest runtime result below.

## Windows image and guest qualification

The raw kernel has 9,407,924 bytes, a measured increase of 2,456 bytes.
The parser adds 1,752 text bytes and 65 read-only bytes; the LF manual adds
648 bytes. Link alignment consumes nine fewer padding bytes. A layout-only
link with the old parser and manual reproduces the preceding 9,405,468-byte
raw size while retaining the current symbol blob. Both final ELF sizes stay
fixed: 9,504,400 bytes for pass one and 9,635,472 bytes for the final link.
Only the raw-kernel policy row changes; the other fifteen rows remain exact.

The existing image publisher produces 209,715,200 bytes with SHA-256
`8d3b3e9434c64e1f32e5325c768c6e581707028cea5d60870db6ff08a06b67df`.
Its private-copy guest passes four-CPU max/e1000 validation, all 62 crypto,
ASN.1 and X.509 checks, the ordinary `ls` JIT, and a terminal block that prints
`PTRDIFF=6,-6`. The block completes and the guest survives the existing
post-command observation. The source image retains its complete hash.
The passing guest takes 117.272 seconds under the original probe bounds.

The first guest probe times out. It put a global array declaration before
the print expression on one REPL line; that declaration path returns after
the declaration. The corrected probe puts the statements inside one executable
block. No OS source changes accompany this correction. The keyboard regression
now includes both braces. Both complete final 519-method selections pass:
86.783 seconds on Windows and 9.101 seconds on Linux, without skips.
All twenty focused artifact-policy and ownership cases pass in 9.979 seconds.
An earlier invocation used a nonexistent audit test class; its loader errors
remain recorded separately from the corrected selection.

## Manual-only image checkpoint

Removing 53 bytes of qualification prose from the embedded manual requires
a separate image replay. The manual now contains 87,070 LF bytes, 595 more
than the parent. The parser remains byte-identical to the qualified object.
The existing manual compilation, two links, symbol generation, flattening,
sixteen-artifact check and image publication all pass. The raw kernel has
9,407,872 bytes: 2,412 added payload bytes and eight fewer alignment bytes
produce a 2,404-byte increase over the parent. Both ELF sizes remain unchanged.

That checkpoint's 209,715,200-byte image has SHA-256
`86c074e73a12e799fcb81baeb27c86537aebce99fc5967af9c8d1df50c26064a`.
A fresh private-copy four-CPU guest passes the same crypto, JIT, signed
pointer-distance, terminal-completion and survival checks in 118.074 seconds.
The complete source-image hash is unchanged after the run. The earlier guest
result above remains evidence for its earlier image only.

## Installed demonstration

`feature23_full_access` now checks signed halfword, word, double and complete-row
distances and prints `PASS feature23_pointer_difference` on success. Its existing
overall result also requires these checks. A host contract extracts the installed
probe and executes it through both JIT and AOT output. The complete compiler and
smoke-runner selections pass all 520 methods on Windows and Linux, in 82.492 and
9.702 seconds respectively.

The manual and wiki describe the command and marker. This demo extension follows
the manual-only image checkpoint above. Its normal build and installed JIT/AOT
guest qualification now pass.

A separate layout check copies all 428 link inputs and 322 headers, checks their
bytes before and after use, and runs both CupidLD links, CupidDis symbol export,
CupidObj symbol-source generation, checked CupidC compilation and CupidObj
flattening. Only the demo and manual objects differ from the preceding accepted
cohort. The resulting raw kernel has 9,408,860 bytes; the final ELF has 9,639,568
bytes. Pass one stays at 9,504,400 bytes. Two exact policy rows change and the
other fourteen retain their values; all seventeen policy methods pass.

The 3,392-byte raw increase over the parent comprises 1,752 parser text bytes,
65 parser read-only bytes, 793 demo bytes and 793 manual bytes, with eleven fewer
alignment bytes. The ordinary `make -j2 all` run passes in 2,675.096 seconds.
Independent readback compares all 428 complete object pairs, 322 headers, both
ELFs, the raw kernel and generated symbol source/object with the frozen copy.
Every byte agrees, and all sixteen exact artifact rows pass. Earlier
layout attempts stopped on active output handles and a missing copied header;
their failed results are retained separately from the passing complete capture.

## Complete source frontier

Independent readback validates every Linux first/second object and compares
all 156 complete pairs with the normal Windows production objects. Each cohort
has 4,250,780 bytes. The complete 478-input snapshot has SHA-256
`bd424d0453b86fbb4cec934bcb4254078f00a01985aeea75750592bff72f14cf`.
The mounted-drive CLI completes in about 3,083 seconds. This is deterministic
emission evidence; it does not pass the original 2,340-second unittest bound.

The old fixture came from the halfword checkpoint, before saved raw-file
disassembly. Reconstructing all 478 Git inputs reproduces its exact old
snapshot and identifies nine changed inputs. The parser alone grows by 1,988
object bytes; earlier disassembly work accounts for the other aggregate growth.
The first full Windows forty-method run reaches the aggregate comparison and
fails against a preliminary 4,212,812-byte expectation. Its other 39 methods
pass. The corrected expectation is 4,250,780 bytes; all existing individual
object locks agree with the measured production cohort after the parser update.
The next Windows and native-filesystem Linux methods both reject a stale shell
object lock left by the earlier saved-disassembly work. Linux completes its
emissions in 1,899.314 seconds; Windows takes 1,917.284 seconds. Their failure
remains recorded. Independent comparison checks all sixty object locks and
updates the shell's measured size and hash. Both fresh corrected methods pass:
1,948.697 seconds on Windows and 1,905.151 seconds on native Linux, within the
original 2,340-second deadline. Linux checks all 511 captured files before and
after execution and removes its owned workspace after success. These are
separate passing runs; the earlier aggregate and shell-lock failures remain
failed.

Normal ownership remains 449 CupidBuild actions and three Python coordinators.
This change does not promote a hosted seed or complete self-hosting. TempleOS
remains read-only reference material outside the source and ownership counts.

## Final installed guest

The final four-CPU max/e1000 guest passes ordinary `ls`, all 62 crypto checks,
the installed Feature 23 JIT and its external AOT executable. Both executions
print the pointer-distance and overall pass markers. The AOT process exits under
the same PID reported by the loader; terminal completion and the survival window
also pass. The 123.231-second run leaves all three source inputs and the complete
209,715,200-byte source image unchanged. That image has SHA-256
`8242a078b0cd843eb164edf3d4e7a63c38506f5dc6949014c638277efb3f6664`.

Audit regeneration and deterministic check mode also pass. The
[portable evidence](evidence/private-pointer-difference-20261108.json.gz) contains
raw logs, failed attempts, source identities, complete output comparisons and
all final receipts. Installed seeds and normal tool ownership are unchanged.
