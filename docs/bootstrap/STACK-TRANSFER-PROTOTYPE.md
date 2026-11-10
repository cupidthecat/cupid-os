# Scalar-load stack-transfer prototype

The private emitter implements [ADR 0444](../adr/0444-fold-proven-scalar-load-stack-transfers.md).
It replaces one proven adjacent PUSH/POP pair with a register MOV after the
ordinary IR handlers have validated both instructions. Only a four-byte
integer load followed by a word unary or binary consumer qualifies.

All fifteen native and Cupid-built frame, rotate, transfer and direct-entry
controls pass on each host.
The old producer fails the new selector while passing its boundary and runtime
controls. The retained literal-tail case prevents an immediate byte from being
mistaken for a PUSH instruction. A valid branch entering the consumer preserves
both operands; removing only that guard makes the actual program fail. No active
compiler or seed is installed.

The Linux repeated-block SHA experiment takes 43.585 seconds with the old host
object and 16.406 seconds with the new object. Both match the independent
digest. This measures 200 MiB of separate compression inputs. The complete
native-producer publication suites also pass on Windows and Linux in 869.905 and
1,645.457 seconds, including the Linux 32 MiB memory case. Independent rereading
checks complete outputs and all 34 input files. The Cupid-built counterparts
pass in 708.450 seconds on Windows and 1,293.170 seconds on Linux under the
same 600-second case and 1,800-second suite limits. Their complete objects and
executables match the native-produced products on each host. Independent
receipts cover all four runs and retain the two oversized-candidate close-result
differences between hosts.

Both current Cupid compilations pass. Each resulting derivative produces two
matching 628,668-byte emitter objects under the original 360-second limit.
Independent rereading checks the retained source, libraries and commands. The
libraries match earlier accepted input receipts; their deleted preparation
paths are unavailable. New proofs live in the repository workspace.

All 58 ordinary object modes pass on both hosts with the reviewed private
expectations. Independent rereading checks 307 matching objects: 200 reviewed
rewrites, 105 unchanged objects and two objects with separate rotate capability
evidence. All 10,268 original source and diagnostic strings remain unchanged.
The expectations are applied to the detached private worktree. Fresh strict
native builds pass the same 58 modes and the seven-tool self-host link within
its existing 180-second Python limit. Both actual Cupid-built callers also pass
all 58 ordinary modes after compiling within the original 1,800-second limit.
Their older separate self-host inventory failures remain retained.
Both earlier seven-tool links also time out at their unchanged 1,800-second
limit. Independent rereading preserves all passing ordinary results and all
four separate self-host failures on each host.

The separate paired review covers 33 objects and is complete. Its final private
proposal passes all 124 native ordinary and self-host mode executions and both
actual i386 host-adapter executions. Eight complete i386 products match between
hosts. Final Cupid-built callers use a new 1,694-input source copy and the same
original limits. Its strict audit passes every contract after explicitly
classifying seven optional fixtures outside the evaluated default roots. The
422-root, four-generated and 53-strict CPP inventory stays unchanged.
All 26 selected existing ownership controls pass. Both final Cupid callers
compile successfully and pass all 58 ordinary modes; their separate self-host
frontiers, CupidObj subsets and host-adapter links pass within their original
limits. Both adapters execute successfully. Their seven-tool links time out
at 1,800 seconds and remain failures. Both complete three-stage preparations
pass from the frozen, uncommitted 99-input producer snapshot with conventional
host producers blocked. The complete prepared cohorts pass all ordinary and
self-host object modes. Their seven-tool links take 1,709.252 seconds on Windows
and 1,648.417 seconds on Linux. Independent rereading checks 138 phases,
124 modes, two actual adapter executions and eight matching complete products
in `stack-object-fullcohort2-paired-independent.json`.

The full Windows 156-source frontier passes in 1,858.271 seconds, within 2,340
seconds. Complete independent review accepts 152 objects with only the approved
local rewrites and four unchanged objects. The first Linux attempt times out
without publishing a result. An identical full-source Linux-storage replay
keeps every check, the compiler, profile and original deadline unchanged and
passes in 1,488.105 seconds. Paired independent rereading checks all 624 produced
objects, 478 raw inputs per host and the complete 1,694-input Linux clone.
Corresponding objects agree byte for byte in
`stack-full-frontier2-paired-independent.json`. The original timeout remains
failed.

The actual SDK IR contract compiles within 900 seconds on each host, in 495.482
seconds on Windows and 539.476 seconds on Linux. Both complete objects and
linked programs agree. All 58 unchanged IR modes pass on each host with the
original link and runtime limits. Independent rereading checks all source,
producer and library bindings plus 116 executions in
`stack-sdk-ir-controls1-paired-independent.json`. This closes the IR checkpoint;
complete SDK publication and committed compiler qualification remain open.

All four ordinary observer, private-capacity and snapshot suites pass: 396
selected methods, 382 executed methods and 14 expected platform skips.
Independent rereading checks their complete retained products, 88 snapshot
invocations and 60 complete candidate files. Corresponding native and
Cupid-produced objects and images agree within each host. The Linux snapshot
suites preserve their 32 MiB address-space limits.

Before adoption, qualify the complete paired producer cohort. Then run the normal OS
build and strict runtime checks with that cohort. Preserve the prior SDK timeout
records and original limits. The corrected Cupid-built corpus caller independently passes its
422-root/53-strict inventory on both hosts, but both complete SDK default runs
still failed their original compiler deadlines.
