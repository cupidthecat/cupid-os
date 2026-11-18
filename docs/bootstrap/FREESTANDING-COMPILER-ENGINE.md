# Freestanding compiler engine

A loadable freestanding CupidC engine rebuilt all nine of its own objects,
then relinked to the same complete image. This ran in a native Linux harness
at the existing guest compiler arena address. The standalone version also
rebuilds a compiler object inside Cupid OS. The complete in-guest fixed
point and normal-build integration remain unfinished.

## Build and verify

Run from the repository root. Linux, WSL and Windows use their own checked
CupidC and CupidLD seeds to build the same i386 ELF.

```sh
make -j4 -C bootstrap/guest
```

The module is `build/guest-engine/cupidc-engine.elf`. On Linux or WSL
with i386 execution enabled, run:

```sh
make -j4 -C bootstrap/guest check-linux
```

The check uses the same bounded image plan for the full engine and a small
ELF fixture with gaps and zero-fill. The loader checks the complete image
before copying it, then clears gaps and BSS. It rejects a short destination
before writing. Run `make -C bootstrap/guest check-load` for these checks and
the malformed ELF cases without the compiler replay.

The full check calls the loaded entry on a two-MiB Linux stack. It checks
the request ABI, a malformed source and the input size limit. Rejected
requests preserve a sentinel file. Successful requests return their job
allocations and reproduce all nine objects and the relinked ELF byte for byte.

Native Windows and WSL produced the same 2,486,352-byte module. Its SHA-256 is
`732dabe3f2086109c85661b9aa4c648188bee70dcc746bcb4e80bd62b658b9bd`.

## Package the compiler inputs

`make -j4 -C bootstrap/guest source-iso` builds
`build/guest-engine/engsrc.iso` through CupidBuild's checked ISO publisher.
CupidObj authors the candidate from a private copy of the engine, source
files and headers. CupidBuild verifies the candidate before it replaces the
public image. The ISO keeps the ELF at the root and each source file at its
repository-relative path.

Run `make -C bootstrap/guest check-source-iso` with xorriso installed.
It reads back all files, checks that an incomplete inventory leaves the
output intact, and verifies that an unchanged rebuild keeps its timestamp.

To inspect the ISO from Cupid OS, stage it onto a copy of an existing
system image:

```sh
cp cupidos.img build/guest-engine/engboot.img
python3 tools/hostbuild.py stage --image build/guest-engine/engboot.img \
  --fat-start-lba 20480 build/guest-engine/engsrc.iso:/engsrc.iso
```

Boot the copy and run `mount /disk/engsrc.iso /src`. The compiler entry is
available at `/src/cupidc-engine.elf`, and the sources are under
`/src/toolchain` and `/src/bootstrap/guest`. The standalone worker preserves
those logical paths while reading through the VFS mount.

## Standalone compiler executable

`make -j4 -C bootstrap/guest guest-compiler` links the engine and its
Cupid OS adapter as `build/guest-engine/cupidc-guest.elf`. The adapter reads
a source path and an output path from `/home/cupidc.job`. Those two lines
use LF endings. For example:

```text
/toolchain/ctool.cc
/home/ctool.o
```

The input is read through the `/src` mount and keeps its logical
`/toolchain/ctool.cc` path in the compiler. The output goes through the
home filesystem's rename operation after the compiler produces a complete
object. `make -C bootstrap/guest check-guest-compiler` runs the same
adapter and engine on Linux or WSL with a test syscall table. It checks
byte-for-byte output and rejects malformed requests and failed publication.

The executable needs a leased compiler arena, a two-MiB process stack and
worker cleanup before it can run in Cupid OS.
[Standalone program decision](../adr/0484-build-cupidc-as-a-standalone-guest-program.md)

## Original Linux experiment

Checked CupidC compiled the unchanged core, ELF, x86, preprocessor, type,
frontend, IR and emitter sources with `--gnu --freestanding`. A small ninth
translation unit exposes the preprocess/parse/emit pipeline through an ordinary
toolchain job. CupidLD linked those objects at `0x01100000`, with no hosted
runtime or unresolved imports in the module.

The caller supplies the existing allocator, file and diagnostic callbacks.
The experimental version-one request occupies 156 bytes on i386. Invalid ABI,
request size or mismatched parser/preprocessor options leave the result
untouched. Accepted requests use the existing invocation transaction.

The Cupid-built Linux harness validates the ELF load ranges, maps the reserved
address range and calls the loaded engine. It compiles every engine source,
including the complete frontend, IR and emitter. Each resulting object matches
the checked compiler's object byte-for-byte. Relinking all nine reproduces the
whole first module.

| Check | Result |
| --- | --- |
| Frozen producer inputs | 114; snapshot unchanged |
| Complete object pairs | Nine identical |
| Complete module | 2,486,352 bytes; identical |
| Load span | 2,418,055 bytes within the nine-MiB compiler arena |
| Rejected requests | 54, followed by successful compilation |
| Tracked allocations remaining after each call | Zero |
| Largest tracked live allocation total | 95,309,779 bytes, compiling the frontend |
| Resumed harness/build time | 571.301 seconds |

The negative cases cover a wrong ABI, short request, language-mode mismatch,
GNU-mode mismatch, missing input and an insufficient source-byte limit.

The first harness build failed because the narrow hosted declarations do not
provide `exit`. The Linux-only harness now exits through the existing syscall
adapter. Compiler and production runtime sources did not change. The failed
command and original harness remain in the evidence.

## Worker stack and lifetime

The kernel's default process stack is 32 KiB. A second Linux harness calls the
unchanged engine on a separately mapped stack with inaccessible pages at both
ends. Compiling the full frontend faults on both four-KiB and 32-KiB stacks,
without publishing output. With two MiB, every one of the nine sources compiles
successfully. All objects and the complete relinked engine remain byte-identical,
all 54 request controls pass and tracked allocations return to zero.

The largest painted-stack write span is 48,028 bytes, during frontend compilation.
That measurement covers this workload's written bytes; it does not bound all
valid compiler inputs. The guarded run takes 536.090 seconds. Its stack-switch
trampoline belongs only to the Linux experiment. Guest integration must allocate
an actual process stack through the scheduler.

The guest ELF loader validates segments before copying and gives the
standalone worker its own lease at 30–34 MiB. The scheduler supplies the
two-MiB stack and reclaims live worker allocations after CPU detachment.
The ordinary external and legacy compiler arenas keep their own ownership.
The [worker decision](../adr/0485-lease-an-arena-for-the-compiler-worker.md)
records exit and interrupt behavior.

The scheduler now starts both process entry forms through an aligned
i386 cdecl trampoline. The first-dispatch contract checks the initial stack,
argument delivery, entry return, CPU detachment and deferred stack cleanup.
The [scheduler decision](../adr/0483-align-new-process-entry-stacks.md)
records the original mismatch and its repair.

The current kernel has about 980 KB of raw-image headroom and 493 KB before
its fixed stack. Embedding this engine would exceed those limits. The
standalone worker stays on disk and loads into its own compiler arena.

## Reproduce an in-guest compile

Build the kernel, compiler executable and source ISO with checked tools.
The runtime check starts a disposable four-vCPU QEMU guest, mounts the
source ISO and sends the compile requests through the terminal. It checks
that a missing source preserves the object already published. Add
`--frontend` to include the full frontend compilation.

```sh
make -j2 all
make -j4 -C bootstrap/guest guest-compiler source-iso
python3 bootstrap/guest/check_guest_runtime.py
```

A four-vCPU run rebuilt `ctool.o` and released the worker lease. The 41,864-byte
file read from the guest disk matches the checked host object. A subsequent
missing-source request preserved it. Its SHA-256 is
`b3c53624564e96d88100950babb28d768c40eb288195d958cdc4a7c4a5c0fa20`.
Through the Linux syscall fixture, the full frontend produced the same
1,024,260-byte object as the checked host compiler and released its allocations.
The first full frontend run in QEMU reached its 900-second limit.

## Link objects inside Cupid OS

`make -C bootstrap/guest guest-linker` builds
`build/guest-engine/cupidld-guest.elf`. Stage it onto the guest disk as
`/cupld`. Put the output path, text base, entry symbol and ordered object
paths in `/home/cupidld.job`, then run `exec /disk/cupld`.

```text
/home/hello.elf
0x01c00000
_start
/home/hello.o
```

The base is `0x01c00000` for an ordinary executable or `0x01e00000` for a
worker. The linker and compiler share their VFS and allocation callbacks.
The completed ELF replaces the output through a rename within `/home`.

`make -C bootstrap/guest check-guest-linker` compares both layouts with the
checked host tool. Nine rejected requests preserve the previous output. Both
host syscall fixtures also check exact-size allocation reuse, recovery under
heap pressure, and cleanup. The shared guest adapter bounds cached blocks to
eight MiB and drains them before publishing a file or returning from a worker.
The four-vCPU QEMU run linked `hello.elf`, rejected a request targeting the kernel
stack and then ran the preserved ELF. Its 13,992 persisted bytes matched the
checked host link.

The source ISO includes both worker sources, their shared adapter,
`user/cupid.h` and `/src/bootstrap/guest/TOOLS.CTXT`. Its checked readback
matched all 38 files and seven directories. CupidC rebuilt the shared
adapter inside the guest. CupidLD linked that object with ten checked host
objects and produced a byte-identical compiler ELF. The relinked compiler
ran and rebuilt the same adapter. Building all compiler inputs inside the
guest remains open.

## Retained proof

The module SHA-256 is
`732dabe3f2086109c85661b9aa4c648188bee70dcc746bcb4e80bd62b658b9bd`.
Independent readback rechecks all 114 inputs, both sets of objects, both complete
modules, ELF segment bounds, executable entry and the absence of unresolved
symbols. It also checks every execution result and exported payload.

The [archive](evidence/freestanding-engine-proof-20261110.json.gz) contains
153 file records and 143 payloads, including the experimental API, harness,
controllers, failures, sources and built products. It has 2,470,024 bytes and
SHA-256 `64f59ff10000ea54c4202af9384795597a6fbb816fc74d81df275ae04f9ab34b`.

The [guarded-stack archive](evidence/freestanding-engine-stack-proof-20261110.json.gz)
adds 133 file records, nine payloads and 124 verified references to the first
archive. It retains the two rejected stack sizes, complete nine-source run,
harness, trampoline and independent object/module comparisons. Its 59,167 bytes
have SHA-256
`e81891394f0882aef3d28b34d9df10c9ce3909e9d5a395b64d01a7e5bee6b93a`.
