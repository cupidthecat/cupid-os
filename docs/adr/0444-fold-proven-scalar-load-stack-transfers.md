# ADR 0444: Fold proven scalar-load stack transfers

## Status

Implemented privately on 2026-10-06. Native and Cupid-built compilers pass the
frame, rotate, stack-transfer and direct branch-entry controls on both hosts.
Two Cupid-built emitter generations agree. Complete 200 MiB extent suites pass
with native and Cupid-built producer prototypes under the original limits.
Their retained objects and executables agree within each host. Reviewed ordinary
object expectations pass all 58 modes on each host and are applied to the private
worktree. Reviewed separate self-host expectations also pass all native controls
on both hosts. Final derivative callers also pass those three self-host modes
and execute their adapters, while both seven-tool links time out. The complete
prepared cohorts pass all four self-host modes on both hosts. Compiler cohort
qualification now passes for the exact committed `2d04ff25` source cohort on
both hosts. Its replacement seeds are installed privately under ADR 0446.
Complete public bootstraps and paired normal OS gates now pass. Windows
external-user acceptance remains open. All four SDK publication profiles pass
independent verification. The revised 174,193-byte manual passes paired
reconstruction and four strict boots. Its measured raw-kernel length is
9,292,324 bytes; the compiler producer remains unchanged.

## Context

The immediate-rotate prototype still exceeds the Linux 200 MiB case limit.
SHA word arithmetic frequently follows a scalar load with a unary or binary
consumer. The emitter pushes the loaded EAX value and immediately pops it
into EAX or ECX. Those two stack operations can use a register move.

## Decision

Record a PUSH EAX only after the shared encoder emits its complete one-byte
32-bit instruction. At the next IR instruction, require a preceding four-byte
integer LOAD, a four-byte integer unary or binary result, an adjacent recorded
push in the same text buffer and no branch entering the consumer. The ordinary
consumer handler must finish its existing validation and emission first.

Replace PUSH EAX and POP EAX or POP ECX with the shared encoder's two-byte MOV.
The total text size stays fixed, so later patch and relocation positions do
not move. Map the consumer's source offset to the MOV start. The pair has the
same result registers, flags, stack depth and subsequent memory-read behavior.

The preceding load still occurs once, including a volatile read. Naked
functions and kernel stack-reset functions keep their existing path. Other
producer kinds, literal pushes, non-word values and consumer branch entries
do not qualify. This adds no source rewrite, public IR field or publisher
exception. Snapshot identities, complete file digests and deadlines stay in
force.

## Evidence and limits

Both previous native prototypes fail the new register-transfer selector.
Their literal-tail, wide-value and runtime controls pass. After implementation,
all thirteen controls pass with strict native emitters on each host. The new
fixture checks unsigned edge values, unary complement, binary XOR, a volatile
pointer read, loops and complete 64-bit values. A literal ending in byte 50
keeps its original immediate-push protocol; a byte search alone would corrupt
that immediate. Existing frame and rotate controls also pass.

The retained Linux SHA workload takes 43.585 seconds with the old host object
and 16.406 seconds with this prototype's object. Both return the independently
computed digest. The workload performs 3,200 separate 65,536-byte hashes,
totalling 200 MiB. It does not establish a whole-file or publication result.
Evidence is `stack-transfer-emitter1-red-*`, `stack-transfer-emitter1-native1-*`
and `stack-transfer-emitter1-hash-benchmark-linux`.

The current Cupid seed emits the same 670,308-byte prototype object on each
host. Each derivative then produces two matching 628,668-byte emitter objects
within the original 360-second producer limit. Their SHA-256 is
`c7544143482af1f7b344568314201bf5a358e618207a07d142bc6a8128d28945`.
Independent rereading checks all 1,693 captured source inputs, retained library
bindings, commands, outputs and both fixed points. This is an emitter comparison;
it does not qualify a complete tool cohort.

The direct-entry fixture supplies valid IR with two live operands at a jump
into a binary consumer. It preserves both POP instructions. Removing only the
consumer-entry guard changes the emitted target and makes the actual i386
program return one instead of zero on both hosts. The regular test checks the
target bytes and executes 25 pairs of edge values. Fresh native builds pass
all fifteen method selections. Cupid-built fixture callers compile in 170.499
seconds on Windows and 181.942 seconds on Linux, within 360 seconds. Both
Cupid-built compiler derivatives then pass all fifteen regular methods. Separate
retained frame, rotate and transfer products also execute successfully.

The native-producer extent suites pass in 869.905 seconds on Windows and
1,645.457 seconds on Linux, within the unchanged 1,800-second suite and
600-second case limits. Linux also replaces the complete 200 MiB candidate and
previous output under a 32 MiB address-space limit. Independent rereading checks
all 34 input files, actual i386 products, retained invocations and complete
output digests. It retains the two existing oversized-candidate cleanup result
differences between hosts. Historical equal-output stat values were not saved;
identity and timestamp preservation remain assertions of the original tests.

The Cupid-built producer suites pass in 708.450 seconds on Windows and
1,293.170 seconds on Linux under the same limits. Independent rereading checks
the complete files and retained invocations, including Linux's 32 MiB case.
All corresponding native and Cupid-produced objects and executables match
byte for byte on each host. The paired receipts preserve the same two
oversized-candidate close-result differences. These four publication suites
qualify the retained extent callers; they do not qualify the complete compiler.

The original frame preparation directories were removed during external cleanup.
New proofs use source and object copies retained inside the repository workspace.
Those library bytes match the earlier accepted input receipts. The immediate
prototype's original-path recheck still failed and is retained as a failure.
Copies establish their current byte identities; they do not restore deleted
original observations.

The final ordinary review retains 307 matching objects on both hosts. Of these,
105 are unchanged and 200 differ only through reviewed frame reads and adjacent
register transfers. The review checks every decoded byte, mapped branch target,
symbol extent, data section, relocation identity and addend. The two rotate
objects use their separate capability evidence. All 10,268 original source and
diagnostic strings remain unchanged. Two bit-field counters now recognize a
direct frame read as well as the former indirect read; their required object
reads, stores and values remain checked. The atomic core count excludes the
removed parameter-frame pointer read and still requires the target volatile read.

All 58 ordinary modes pass with the final private proposal on both hosts. The
proposal is applied to the detached private worktree. Fresh strict native Make
builds also pass all 58 modes and the seven-tool self-host link within its existing
180-second Python limit. Three separate self-host controls initially fail their
older byte or layout expectations. Their complete paired review subsequently
passes, and the final private proposal passes all native controls. Earlier failed proposals remain
retained, including the incomplete function-array bindings and failed diagnostic
setup. Evidence is `object-expectation-final28-paired-independent.json`,
`object-local-rewrite-final28.json`,
`object-expectation-final28-applied-private.json` and
`stack-object-fresh-native1-*`.

The separate review covers 33 objects: 32 reviewed changes and one unchanged
object. CupidObj also uses the exact pure unsigned rotate leaf and ten
constant-count calls to that local leaf. The review checks their complete old
and new encodings, retires only the corresponding PC32 call relocations and
rejects a branch into any removed span. All remaining relocation identities
and addends stay checked. Its 1,023 zero and bit-basis checks cover every defined
rotate count from 1 through 31. Evidence is `object-local-slow-rewrite5.json`.

All thirteen active self-host inventories bind to complete paired objects.
The emitter source contains six additional functions, so its definition count
changes from 368 to 374. Its current input is independently bound to the
680,368-byte prototype source. The host-adapter image keeps its 21,592 bytes,
entry, sections, symbols, ABI and relocation count; its text changes from 5,897
to 5,365 bytes. Determinism, useful negatives, rollback, recovery and the value
oracles remain in the original controls. The native final proposal passes all
124 ordinary and self-host mode executions plus actual i386 host-adapter
execution on both hosts. Eight complete i386 products agree between hosts;
the seven self-host tools are unchanged from the earlier native run. Evidence
is `native-object-proposal30-paired-independent.json`. The incomplete 71-byte
review pattern remains a recorded failure; the accepted rule requires the
complete 75-byte baseline function.

Actual Cupid-built callers compiled from the ordinary proposal in 801.660
seconds on Windows and 881.123 seconds on Linux, within the existing
1,800-second contract compile limit. Both pass all 58 ordinary modes. Their
three older self-host inventory failures remain failures.
Both earlier seven-tool links time out at the unchanged 1,800-second limit.
Independent rereading preserves these failures and all 116 passing ordinary
executions in `stack-object-cupid2-paired-independent.json`.

Final callers use
the reviewed complete proposal and a separate 1,694-input source copy. Its
strict exported-source audit passes every contract. Seven optional fixtures
have explicit `not_reached` policy rows because the evaluated default roots
do not select them. Their capability proofs remain separate. The existing
422-root, four-generated and 53-strict CPP inventory is unchanged.
All thirteen selected existing ownership controls pass on each host. Their
paired independent receipt records 26 executions against the unchanged frozen
source. Both final Cupid callers compile within 1,800 seconds and pass all 58
ordinary modes. Their full self-host frontier, CupidObj subset and host-adapter
link pass on each host. Both complete adapters match the native products and
execute successfully. Independent rereading records all six modes and both
actual i386 executions in
`stack-object-cupid30-selfhost1-paired-independent.json`. Their seven-tool links
both time out at the unchanged 1,800-second limit and remain failures.

The reviewed separate expectations are applied to the private worktree after
both earlier live source checks close. Both complete three-stage preparations
pass from the frozen, uncommitted 99-input source snapshot with conventional
host producers forbidden. Windows takes 1,757.258 seconds and Linux 1,846.250
seconds. Independent rereading checks 198 source copies, 291 stage artifacts
and 97 matching final-stage pairs. All producer deadlines and fixed-point
checks remain unchanged. This does not establish committed-source lineage
or qualification. Both prepared compilers pass all 58 ordinary modes, the full
self-host frontier, CupidObj subset and host-adapter link. Their full self-host
frontiers take 1,330.279 seconds on Windows and 1,338.968 seconds on Linux,
within 1,800 seconds. The complete Windows seven-tool link passes in 1,709.252
seconds; Linux passes in 1,648.417 seconds. Independent rereading checks all
138 phases, 124 ordinary/self-host modes and two actual i386 adapter executions.
All eight complete products match the accepted native products and each other
in `stack-object-fullcohort2-paired-independent.json`.

The full Windows kernel frontier compiles all 156 sources twice in 1,858.271
seconds under the original 2,340-second deadline. Its first objects total
4,162,872 bytes. These outputs need complete independent review before the old
locks change. Windows's independent review accepts 152 objects with only the
approved frame-read and stack-transfer changes, plus four unchanged objects.
Every source, complete output, mapped branch target, symbol extent, data
section and retained relocation is checked in
`stack-kernel-local-rewrite1-windows.json`. The first checker compared a list
directly to the approved source tuple and failed before reviewing any object;
its corrected comparison preserves the same complete order. The first Linux
run reaches the original deadline and publishes
no result. The identical 478-input snapshot measures 4.3 to 22.4 seconds on
Windows-mounted storage and about 0.08 seconds on Linux storage. A fresh replay
uses an identical 1,694-input source copy on Linux storage with the same compiler,
profile, checks and deadline. It passes in 1,488.105 seconds. Paired independent
rereading checks all 624 produced objects, 478 raw inputs per host, the complete
1,694-input Linux clone and 313 retained Linux output copies. Corresponding
objects agree byte for byte. The same full baseline review applies to both
hosts. Evidence is `stack-full-frontier2-paired-independent.json`; the original
timeout remains failed.

A subsequent publisher repair uses the accepted candidate capacity when
Windows reopens the candidate before a second checked tool. Its separate
35-input proof passes through all four native and complete prepared producers.
It changes no emitter byte. That source remains separate from the frozen
1,694-input compiler proof. [ADR 0439](0439-bound-large-candidate-publication-explicitly.md)
records its positive, rejection and bounded-memory controls.

The actual SDK IR contract also passes its original 900-second compile limit:
495.482 seconds on Windows and 539.476 seconds on Linux. Both 2,048,284-byte
objects agree. Each prepared host linker uses the complete Linux stage-four
libraries and passes within 360 seconds. Their complete 3,579,488-byte programs
agree, and all 58 unchanged IR modes pass on each host within 900 seconds.
Independent rereading checks the complete source closure, producer/library
bindings, commands, logs and 116 executions in
`stack-sdk-ir-controls1-paired-independent.json`. Failed checker assumptions
about successful output remain retained; the accepted checker requires the
actual mode-specific success lines. This closes the IR checkpoint. Complete
SDK publication and committed compiler qualification remain required.

The original native diagnostic driver links this emitter with earlier native
compiler components. Fresh native controls build all components from the private
worktree. Final actual Cupid-built self-host callers and full paired producer
qualification remain required
before adoption. Normal OS and strict runtime acceptance must use the resulting
qualified compiler.
