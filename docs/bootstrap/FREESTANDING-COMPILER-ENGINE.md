# Freestanding compiler engine

A loadable freestanding CupidC engine rebuilt all nine of its own objects,
then relinked to the same complete image. This ran in a native Linux harness
at the existing guest compiler arena address. Cupid OS also rebuilt and
relinked the eleven-object standalone compiler. The rebuilt executable ran
inside Cupid OS and reproduced `ctool.o`.

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
that a missing source preserves the object already published.

```sh
make -j2 all
make -j4 -C bootstrap/guest guest-compiler source-iso
python3 bootstrap/guest/check_guest_runtime.py
```

Pass `--frontend` to add the complete frontend compile. This replay stages
the ISO in HomeFS and uses one vCPU with a 3,600-second phase deadline.
Use `--timeout` to set a different deadline.

A four-vCPU run rebuilt `ctool.o` and released the worker lease. The 41,864-byte
file read from the guest disk matches the checked host object. A subsequent
missing-source request preserved it. Its SHA-256 is
`b3c53624564e96d88100950babb28d768c40eb288195d958cdc4a7c4a5c0fa20`.
After the arena change, a fresh replay matched the current 41,804-byte
`ctool.o`, SHA-256
`fd735b2efc66fb954b60578559eb1acb84ba5cee0f1428ac9b4575a17d9c0e17`.
Through the Linux syscall fixture, the full frontend produced the same
1,024,260-byte object as the checked host compiler and released its allocations.
In Cupid OS, the instrumented worker completed the same object with the source
ISO mounted from HomeFS and a 3,600-second QEMU deadline. The persisted object
matched the checked host bytes with SHA-256
`247ef308ec75bd3079497bc97cbc4454b0e062675868bc8d982e611304addd87`.
Earlier runs expired at 900 and 1,800 seconds. The automated HomeFS replay
completed with both saved objects matching their checked references.

## Compile with production options

Add options after the source and output lines in `/home/cupidc.job`.
With options present, list all include roots in the intended search order.

| Job line | Effect |
| --- | --- |
| `include /kernel/core` | Search for quoted and angle headers |
| `angle /toolchain/hosted/i386-linux/include` | Search for angle headers |
| `define DEBUG=1` | Define a macro |
| `force /kernel/doom/dglibc_compat.h` | Process a header before the source |
| `doom` | Enable the hosted Doom compatibility rules |

The replay uses the checked kernel, Doom shim and vendored Doom profiles.
Each batch boots once for up to eight sources from one profile. Its source
ISO preserves the original paths. The replay compares each saved object
with the complete checked host object before passing it to a link.

```sh
make -C bootstrap/guest guest-compiler
python3 bootstrap/guest/check_guest_profile_runtime.py
```

Use `--production-root` to select a separate completed checked build.
Repeat `--source` to select other repository-relative sources from the
checked production profiles.

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

`make -C bootstrap/guest check-guest-linker` compares the fixed and script
layouts with the checked host tool. Twelve rejected requests preserve the
previous output. The host syscall fixtures also check exact-size allocation
reuse, recovery under heap pressure, and cleanup. The guest adapter caps the
cache at eight MiB and drains it before publishing a file or returning.
The four-vCPU QEMU run linked `hello.elf`, rejected a request targeting the kernel
stack and then ran the preserved ELF. Its 13,992 persisted bytes matched the
checked host link.

The source ISO includes both worker sources, their shared adapter,
`user/cupid.h` and `/src/bootstrap/guest/TOOLS.CTXT`. Its checked readback
matches 47 files and seven directories. The first relink used one guest
object and ten checked host objects. The full reconstruction below uses
eleven guest-built inputs.

## Rebuild the standalone compiler in Cupid OS

For the first eight objects below, use the corresponding `.cc` paths under
`/toolchain`. Compile `cupidc_engine.cc`, `guest_job.cc` and `cupidc_guest.cc`
from `/bootstrap/guest` in the source ISO. Pass each logical `.cc` path and its
`/home` output path in `/home/cupidc.job`, then run `exec /disk/cupcc`.
Once all objects are in `/home`, use this `/home/cupidld.job` and run
`exec /disk/cupld`.

```text
/home/rebuilt.elf
0x01e00000
cupidc_guest_run
/home/ctool.o
/home/elf32.o
/home/x86.o
/home/cupidc_pp.o
/home/cupidc_type.o
/home/cupidc_frontend.o
/home/cupidc_ir.o
/home/cupidc_emit.o
/home/cupidc_engine.o
/home/guest_job.o
/home/cupidc_guest.o
```

At `c8447942`, all eleven objects produced inside Cupid OS matched the
checked host objects. The complete 2,504,444-byte executable saved by the
guest linker matched the checked host ELF, SHA-256
`a2c0c9ff6f0cfb17c17e10cba4e0c5d567f0ba575d577e4baac7242c6cf0fe88`.
`exec /home/rebuilt.elf` then rebuilt `ctool.o` under a second name. All
41,864 bytes matched and both worker leases were released.

The rebuilt CupidC also compiled both CupidLD sources. CupidLD then linked five
guest-produced objects into a byte-identical 251,904-byte copy of itself.
The new linker produced the checked hello ELF from an object assembled inside
Cupid OS. That program ran and released its external image lease.

## Rebuild and run the user examples

On Linux or WSL, build the checked host references and the guest workers.
Use a completed OS build for the replay.

```sh
make -C user all
make -C bootstrap/guest guest-compiler guest-linker
python3 bootstrap/guest/check_guest_user_runtime.py
```

The replay mounts the example sources, compiles and links `cat`, `hello`
and `ls` in Cupid OS, and runs each from HomeFS. It checks program output
and exit against the loaded PID and compares all six objects and ELFs.
Use `--production-root` to select a separate checked OS and user build.

## Relink the production kernel

Put `script` on the second line of `/home/cupidld.job` and the linker script
path on the third line. The remaining paths are the objects in link order.
CupidLD uses the same script parser and assertions as the checked host tool.

```text
/home/kernel.elf
script
/disk/link.ld
/disk/obj0/i000.o
```

From a completed checked Linux or WSL build, run the kernel replay.

```sh
make -C bootstrap/guest guest-compiler guest-linker guest-ksyms guest-object
python3 bootstrap/guest/check_guest_kernel_link.py
```

Repeat `--rebuild-source PATH` to compile selected inputs in Cupid OS
before linking. The replay checks each saved object against the host build
and uses the guest copy for both links. Paths are relative to the repository.
The replay with `--rebuild-source kernel/core/kernel.cc` matched both ELFs,
the symbol source and object, and the raw kernel. The resulting disk booted.

The replay gets the ordered inputs from the normal Make recipes. It stages
the checked pass-one objects and runs both links inside Cupid OS. Between
them, the guest generates and compiles the kernel symbol table. It flattens
the final ELF, compares the five saved artifacts, then boots a disk made
with the guest-produced raw kernel. `--production-root` selects another
completed checked build.

The symbol worker takes the pass-one ELF and output source paths, one per
line in `/home/cupidksyms.job`. Stage the worker as `/cupks` and run
`exec /disk/cupks`. CupidC reads an input under `/home` from HomeFS. Put
`ksyms.h` and `types.h` beside the generated source so the compiler can
resolve the quoted includes. `make -C bootstrap/guest check-guest-ksyms`
compares the source with checked CupidBuild and checks rejected jobs.

To rebuild the bootloader, stage `boot/boot.asm` as `/boot.asm` on the guest
disk. CupidASM writes the mixed-mode range map along with the raw image.

```text
as -f bin --map /home/boot.map -o /home/boot.bin /disk/boot.asm
dis --raw --range-map /home/boot.map --require-known /home/boot.bin
```

All 2,560 guest-produced bytes matched the checked bootloader. A rejected
assembly preserved the saved image and its map remained usable by CupidDis.

## Check raw boot assembly inside Cupid OS

Stage `cupidraw-guest.elf` as `/cupraw`. Put the source path, the HomeFS
output path and `bootloader` or `smp-trampoline` in `/home/cupidraw.job`,
one field per line. Run `exec /disk/cupraw`.

The worker sends CupidASM's ranges and control edges directly to CupidDis.
It checks instruction decoding and branch targets before publication. The
trampoline must also match the kernel's approved mixed-mode layout.

```sh
make -C bootstrap/guest guest-raw check-guest-raw
python3 bootstrap/guest/check_guest_raw_assembly_runtime.py --production-root /path/to/checked/build
```

The guest matched the checked bootloader and AP trampoline byte for byte.
A shifted trampoline was rejected and left the saved binary intact.

## Rebuild the kernel assembly objects

```sh
make -C bootstrap/guest guest-assembler
python3 bootstrap/guest/check_guest_assembly_runtime.py
```

The standalone CupidASM worker reads an input path and a `/home` output
path from `/home/cupidasm.job`. It uses case-sensitive symbols and the
source's declarations. The shell command keeps its kernel call bindings.
CupidDis checks the object before the worker saves it.

The guest reproduced `kernel/cpu/isr.o` and
`kernel/core/context_switch.o` byte for byte. A truncated instruction
failed inspection, and the saved ISR object kept its original bytes.
Add `--rebuild-assembly` to the kernel replay to use both guest objects
in both production links.

## Compile peer tool objects

`make -C bootstrap/guest peer-objects source-iso` builds checked host
references for the shared CupidASM, CupidDis and CupidObj cores and packages
their sources and headers. The guest compiler accepts `/toolchain/cupidasm.cc`,
`/toolchain/cupiddis.cc` and `/toolchain/cupidobj.cc` in the same job format.
Compare the resulting objects with the corresponding files in
`build/guest-engine`.

## Flatten an ELF image inside Cupid OS

Build the CupidObj worker with `make -C bootstrap/guest guest-object`. Stage
`build/guest-engine/cupidobj-guest.elf` as `/cupobj`. Put the ELF input and
the raw output path in `/home/cupidobj.job`.

```text
/disk/kern.elf
/home/kernel.bin
```

Run `exec /disk/cupobj`. The worker accepts ELF inputs up to 16 MiB, writes
the initialized load image, and releases its worker lease. On Linux or WSL,
`make -C bootstrap/guest check-guest-object` compares the output against the
checked hosted CupidObj and exercises rejected requests.

In a four-vCPU guest, the production ELF flattened to 9,507,492 bytes.
HomeFS readback matched the checked host output, SHA-256
`01f737eb769ec52948c8739e0fc9062f147a7e888b4b255576bc42c4acfdaf6b`.

## Wrap a kernel asset inside Cupid OS

Use the same CupidObj worker and add two lines to `/home/cupidobj.job`.
Choose `wrap` for binary data or `wrap-text` for text with normalized line
endings. The final line is the path from the repository root. It determines
the `_binary_*` symbols even when the staging filename differs.

```text
/disk/text.src
/home/text.o
wrap-text
/bin/ls.cc
```

With a completed checked build, run the asset check on Linux or WSL.

```sh
python3 bootstrap/guest/check_guest_asset_runtime.py --production-root /path/to/checked/build
```

The saved `bin/ls.o` and `image.bmp.o` matched the checked objects. A request
with a parent path component left the existing `ls` object intact.

## Generate kernel installation sources inside Cupid OS

Build the installation worker and stage `cupidinstall-guest.elf` as
`/cupinst`. Run `exec /disk/cupinst` with the destination, kind and
repository path groups in `/home/cupidinstall.job`, one field per line.

```text
/home/demos_programs_gen.cc
demos
--demos
/demos/hello.asm
```

The `bin` request uses `--bin`, `--headers`, and `--browser`. The `docs`
request uses `--ctxt`, `--doc-assets`, and `--home-assets`. Keep each heading
in order, including empty groups. CupidObj uses the inventory to name the
embedded objects in the generated source.

```sh
make -C bootstrap/guest guest-install guest-compiler check-guest-install
python3 bootstrap/guest/check_guest_install_runtime.py --production-root /path/to/checked/build
```

The replay uses the installation recipes from Make. All three saved
sources and their CupidC objects matched the checked build byte for
byte. An invalid demo path preserved the preceding generated source.

## Build the disk template inside Cupid OS

Build the worker with `make -C bootstrap/guest guest-disk` and stage it
as `/cupdisk`. Put the bootloader, raw kernel and output paths in
`/home/cupiddisk.job`, one per line.

```text
/home/boot.bin
/home/kernel.bin
/home/disk-prefix.img
```

Run `exec /disk/cupdisk`. The result contains the boot and kernel blocks,
both FAT16 tables and the empty root directory for a fresh 200 MiB disk.

On Linux or WSL, `make -C bootstrap/guest check-guest-disk` compares the
prefix with the checked host tool and checks rejected jobs.
`python3 bootstrap/guest/check_guest_disk_runtime.py` runs the job in
Cupid OS, compares the prefix, extends the file with zeroes and boots it.
The replay accepts `--boot` and `--kernel` to select saved guest artifacts.

Run the full boot-media replay from a completed checked Linux or WSL build.

```sh
make -C bootstrap/guest guest-compiler guest-linker guest-ksyms guest-object guest-disk
python3 bootstrap/guest/check_guest_kernel_link.py --rebuild-disk \
  --rebuild-source kernel/core/kernel.cc
```

CupidASM assembles the bootloader and CupidDis checks its range map.
A second guest runs the disk worker with the bootloader and raw kernel
read back from the kernel replay. The test zero-extends the saved prefix
and boots it with four virtual CPUs. It checks SMP, TLS, e1000 and DHCP,
then runs the shell's `ls` through CupidC.

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
