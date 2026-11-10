# ADR 0089: Establish a static i386 CupidC compiler fixed point

## Status

Accepted on 2026-07-23.

## Context

ADR 0088 added a compile-only CupidC command and proved that the first
Cupid-built compiler could reproduce one large implementation object. That
check did not rebuild the other ten C objects in the compiler closure or link
another complete compiler.

The hosted i386 preprocessing profile also had one command-line mismatch. Its
project root accepts quoted and angle includes, while the checked Linux
declaration root accepts angle includes only. The first driver exposed `-I`,
which enabled both forms for every supplied root. The existing sources happened
to resolve to the same headers, but that command did not represent the audited
profile exactly.

A compiler fixed point requires consecutive complete generations. Stage three
must come from stage two, and the comparison must cover every compiler object
and the linked executable. Reusing generation one for both builds would only
repeat the same compiler twice.

## Decision

CupidC accepts two ordered include-root options:

```text
-I PATH                 quoted and angle includes
--include-angle PATH    angle includes only
```

Both forms accept native paths in ordinary command mode. With `--root`, each
include path must be an absolute logical path. The driver passes each root's
form mask to the preprocessor instead of widening every root to both forms.

The fixed-point contract starts with the static compiler produced by the native
object contract. That executable is generation one. It performs these steps:

1. Generation one compiles the eleven C sources in its own closure into the
   stage-two directory.
2. The existing Cupid-built CupidASM assembles
   `toolchain/hosted/i386-linux/start.asm`, and the existing Cupid-built CupidLD
   links stage two at `0x08048000`.
3. Stage two compiles the same eleven sources into the stage-three directory.
4. CupidASM assembles a fresh startup object, and the same CupidLD links stage
   three with the same layout and object order.

The C source set is `ctool.c`, `ctool_host.c`, `cupidc_pp.c`, `cupidc_type.c`,
`cupidc_frontend.c`, `cupidc_ir.c`, `cupidc_emit.c`, `elf32.c`, `x86.c`,
`cupidc_main.c`, and `hosted/i386-linux/runtime.c`. The first ten use strict
C11. The runtime alone enables GNU mode for its variadic built-ins.

Every compile uses `/toolchain` through `-I` and the checked i386 Linux
declaration directory through `--include-angle`. The driver supplies
`__SIZEOF_POINTER__=4`. Two bounded workers compile independent objects.

The link order is startup, driver, emitter, IR, frontend, types, preprocessor,
host adapter, core, ELF32, x86, and runtime. The contract compares all eleven C
objects, both startup objects, the generation-one and stage-two images, and the
stage-two and stage-three images. It also runs stage three's help path. A valid
source must produce the same object through stages two and three, while a
malformed source must produce the same diagnostic and preserve both sentinel
outputs.

`make test-cupidc-fixed-point` runs the focused contract. The regular repository
test discovery runs it as part of the complete gate. Generated stage
directories remain temporary because they duplicate the declared generation-one
image and are not bootstrap seeds.

Each static hosted i386 artifact now depends on its manifest producer. A direct
request for `cupidc-cupidc.elf`, including a request after deletion, reruns the
complete checked link recipe instead of accepting a stale file.

## Alternatives considered

Using `-I` for the Linux declaration directory would let a quoted include find
a header in an angle-only root. The fixed-point command needs the same lookup
rules as the audited profile.

A named repository profile in the driver would reproduce this one build, but it
would couple a general compiler command to the current tree layout. The
include-form option exposes the underlying preprocessor capability and works
with other roots.

Comparing another single implementation object would extend ADR 0088 without
proving a complete generation. The fixed-point gate therefore rebuilds every C
object and relinks the executable.

Running generation one for both stage directories would prove deterministic
repetition by one compiler. Stage three runs the stage-two executable so the
test crosses a real generation boundary.

## Consequences

CupidC has a reproducible compiler fixed point for the static i386 Linux target.
The three linked compiler images are byte-identical, static ELF32 executables
with entry point `0x08048000` and no unresolved symbols. The current image is
1,812,712 bytes with SHA-256
`29CD222C6E33590932457D36F3728705134C8C6750947E7CFBC4ABA3B7C5500B`.

The proof covers Linux directly or Windows through WSL. It does not establish a
native Windows fixed point. Generation zero, native contracts, and native
hosted commands still use GCC or Clang. Normal Cupid OS C objects also remain
host-owned.

The fixed point covers CupidC. CupidASM and CupidLD supply the same checked
startup and link operations to both compared stages, but the test does not
rebuild those tools with the stage-two compiler. Checked seeds, fresh-checkout
bootstrap independence, whole-toolchain fixed points, and production ownership
remain open.

ADR 0090 follows this compiler-only checkpoint with the complete five-tool
static i386 fixed point. It leaves this ADR's original evidence and claim
boundary intact.
