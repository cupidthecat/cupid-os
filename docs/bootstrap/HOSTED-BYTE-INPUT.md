# Hosted byte input

The private shared hosted library now declares and implements `fgetc(FILE *)`
and `getchar(void)`. Required-file mutation fixtures drove the addition when
current Cupid compilation rejected an undeclared getchar call.
[ADR 0458](../adr/0458-read-hosted-bytes-through-fgetc-and-getchar.md) records the
contract and its remaining integration work.

The implementation reads one byte through the existing unbuffered fread path.
A successful read returns the unsigned byte as int; no byte returns EOF.
Getchar delegates to fgetc(stdin). Existing EOF/error indicators and errno
mapping remain the source of stream state. The Windows runtime includes the
shared C body, so this adds no import or startup assembly.

The source belongs to `disk-required-handoff-source3` on both hosts. The changed
paths are `toolchain/hosted/i386-linux/include/stdio.h` and
`toolchain/hosted/i386-linux/runtime.cc`. All four native/Cupid callers pass
three dedicated byte/state methods within the eighty-method handoff suite.
They check stdin bytes 0, 127, 128 and 255 followed by EOF, immediate EOF, all
256 regular-file byte values, indicator reset, rewind and a write-only read
error. The byte 255 remains distinct from EOF. Late boot/kernel/seed mutation
fixtures also execute the getchar pause.

`disk-required-handoff-independent7-products/closed.json` rereads these complete
inputs, reports and outputs within its 652 actual invocations. The write-only
case must set ferror without feof, clear both indicators and leave an empty
created file. Native Windows keeps errno 73 for that error; the checked runtime
must return a changed nonzero errno. This specific oracle difference is
retained rather than assigned to checked behavior. Both runtime objects pass
`disk-required-handoff-runtime-dis-{windows,linux}1` with known instructions,
local targets and code anchors required, under sixty seconds.

The initial undeclared-function failure and disk-full retries remain recorded
in [the handoff record](REQUIRED-DISK-HANDOFF.md). No normal producer input or
installed seed changes through this private proof. The two library files still
need normal source integration, complete producer qualification and consumer
acceptance. Existing line-input and stream-state acceptance remains documented
separately in [HOSTED-UNSIGNED-LINE-INPUT.md](HOSTED-UNSIGNED-LINE-INPUT.md).
TempleOS remains read-only and excluded.
