# Freestanding compiler engine

A loadable freestanding CupidC engine rebuilt all nine of its own objects,
then relinked to the same complete image. This ran in a native Linux harness
at the existing guest compiler arena address. Execution inside Cupid OS and
normal-build integration remain unfinished.

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
`/src/toolchain` and `/src/bootstrap/guest`. A guest worker still needs to
map those VFS paths to the compiler's logical paths.

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

## Guest integration still needed

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

The Linux harness does not establish the kernel's memory, stack or process
lifetime behavior. The guest loader must validate the module before copying,
honor the shell's compiler-region ownership, provide a sufficient worker stack
and reclaim job storage when the process exits or is killed. Sources and module
bytes must also reach the guest through a checked build and filesystem path.

The current kernel has about 980 KB of raw-image headroom and 493 KB before
its fixed stack. Embedding this engine would exceed those limits. Keeping the
payload on disk and loading it into the existing compiler arena is the measured
integration direction; a real guest self-recompile is still required.

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
