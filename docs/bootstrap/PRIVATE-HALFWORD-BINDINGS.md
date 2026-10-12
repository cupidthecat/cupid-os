# Private network halfword bindings

The isolated follow-up to the [halfword type candidate](PRIVATE-HALFWORD-TYPES.md)
publishes fixed native signatures for eight more network functions: `htons`,
`ntohs`, `bind`, `connect`, `accept`, `sendto`, `recvfrom` and `udp_send_raw`.
Their declared ports occupy two bytes, while each cdecl argument still occupies
a complete four-byte stack slot. IPs and lengths retain unsigned word types;
descriptors and error results retain signed word types. Peer-port outputs use
`U16 *`, so native two-byte writes preserve the neighboring array element.

## Native result boundary

Independent i386 assembly reads the complete outgoing stack slots. Its native
halfword returns deliberately poison the upper EAX bits. Fixed descriptors
alone leave five assertions failing: direct `htons` and `ntohs` calls expose
the high bits, as does the same-state recovery program. Normalizing a named
halfword call result fixes those cases. A retained record callback then
reproduces two more failures and needs the same conversion after cleanup.

Review finds a third path through class-method syntax. A native
`Device_ReadPort` and `Device_ReadSigned` oracle verifies the receiver and
returns poisoned signed and unsigned halfwords. Both JIT and AOT cases fail
before the class-method result normalization and pass afterward. All three
call paths now sign-extend `I16` or zero-extend `U16` results. Other result
channels keep their preceding behavior.

The initial three capability methods produce 65 assertion failures against
the legacy registrations. The final five ABI methods cover all eight native
declarations in JIT and AOT, repeat each positive stack case 1,000 times,
reject 48 invalid calls, recover after two failures in the same compiler
state, and execute named, record and class-method callbacks. The binding-table
test checks exact native widths and the result inventory. Legacy result-only
registrations retain their preceding expectations.

## Guest probe and integration

Feature 23 keeps its twelve preceding network checks and adds all eight
halfword declarations. It checks two-byte signed/unsigned storage and two
neighbor-port sentinels. Closed and listening TCP sockets exercise rejection
paths before peer writes or SYN output. Invalid `sendto` and `recvfrom`
descriptors return before buffer access. The 1,473-byte UDP request rejects
before copying its two-byte buffer or transmitting a packet. Both created
sockets are still closed. These paths complement the independent positive
ABI oracles; they do not establish a connected peer or packet-transfer test.

JIT and AOT guest patterns require the exact twenty-binding, eight-halfword
and two-guard marker. Negative harness tests reject missing markers and
reduced counts. The complete four-module selections pass 524 tests on each
host, with no Windows skips and eight Windows-only skips on Linux. All 2,245
controlled inputs remain unchanged during each final run. Both review axes
report no remaining required finding. The continuing optional duplication
in negative test loops is recorded in the Standards report.

Adding the native `accept` registration exposes five unrelated callback
fixtures that used the same function name. Ten occurrences within that one
method become `accept_callback`; its diagnostics and recovery assertions are
unchanged. An initial recipe incorrectly expects nine occurrences and fails
before writing. The following integrations mistakenly launch anyway and
replay the unchanged failures. Those recipes, failed runs and launch-order
record remain preserved. The final runs use the verified scoped rename.

The [evidence index](evidence/private-halfword-bindings-20261012.json.gz)
binds all 142 independently reread originals, including final source
snapshots, failures, assembly oracles, reviews and logs. The archive has
5,137,550 bytes and SHA-256
`6697bfbc275314b66ee4c8f0a414bbaeac7d83843747447273a2bbeac6442576`.

Seven files differ from the preserved type-only candidate. The 99 standalone
producer inputs and engineering checkout's active compiler remain unchanged.
Separate Windows and Linux OS roots contain the candidate and the same
qualified seventeen-file seed/pin/policy projection. Cold kernel measurements
are queued after the four SDK profiles to preserve the existing producer
timing bounds. Complete normal builds, ABI and artifact gates, and all 31
network-aware guest commands remain required before adoption. No installed
seed change, source-owner transfer or complete C support is claimed.
`TempleOS/` remains read-only reference material.
