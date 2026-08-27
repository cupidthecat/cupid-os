# CupidASM Assembler

CupidASM is Cupid OS's shared 16/32-bit x86 assembler. The kernel command can
run fixed images immediately, write raw binaries with typed range maps, keep
an ELF32 relocatable object for a later link, or ask CupidLD for a linked
executable. JIT programs run in ring 0 and can call the kernel directly.

The normal ISR and context-switch recipes now enter CupidASM through the
promoted CupidBuild seed. CupidBuild freezes the source and six-tool cohort,
validates the private `ET_REL` object, asks CupidDis to check instruction
coverage, relocations, local targets, and function anchors, then publishes it
atomically. Python no longer participates in these two transforms. ADR 0354
records the handoff.

Source-head CupidBuild also exposes typed bootloader and SMP-trampoline
assembly. These commands retain the raw image and v2 map inside one guarded
transaction, apply the artifact's exact size and layout rules, and require
CupidDis source-edge validation. They are not normal recipe owners yet because
recipe ownership needs its own graph, build, and boot evidence. ADR 0355
records the source-head capability, and ADR 0356 records seed carriage.

---

## Overview

| Feature | Details |
|---------|---------|
| Syntax | Intel (NASM-style) |
| Target | 16/32-bit x86 raw images and i386 ELF32 |
| Assembler type | Single-pass with forward-reference patch table |
| Calling convention | cdecl |
| Output modes | JIT, raw binary plus map, ELF32 `ET_REL`, and linked ELF32 |
| Privilege level | Ring 0 - full system access |
| Source extension | `.asm` |
| Code size limit | 1 MB |
| Data size limit | 1 MB |
| Max source line/string | 1024 bytes |
| Max labels | 8192 |
| Max forward refs | 8192 |
| Include depth | 16 levels |
| Instructions | Expanded x86-32 integer, control-flow, system, FPU, SSE, and atomic set |
| Registers | 24 (8/16/32-bit) |

---

## Getting Started

### JIT Mode - Assemble and Run Instantly

```
> as demos/hello.asm
```

CupidASM assembles the source into memory at `0x01A00000` and executes it without saving a binary.

You can also run `.asm` files directly with `./`:

```
> ./demos/hello.asm
```

### Artifact Output

The older `as -o` form still writes a linked executable:

```
> as -o hello demos/hello.asm
> exec hello
```

Or using the dedicated `cupidasm` command:

```
> cupidasm demos/hello.asm -o hello
> exec hello
```

If `-o` is omitted with `cupidasm`, the output name is derived from the source file (e.g., `hello.asm` -> `hello`).

Select an artifact explicitly with `-f`:

```text
> as -f bin --map mixed.map -o mixed.bin /home/mixed.asm
> as -f elf32 -o module.o /home/module.asm
> as -f exec -o hello demos/hello.asm
```

`bin` writes the flat bytes and requires `--map`. The map uses
`cupid.raw-map.v2` and records the origin, each code16, code32, or data range,
and ordered source-resolved control edges. `elf32` writes an unlinked i386
`ET_REL` file, so undefined symbols and
relocations remain available to a later link. It does not require `main` or
`_start`. `exec` uses the existing entry selection and in-kernel CupidLD link.

The same options work with `cupidasm`. Source-only `cupidasm`, both historical
`-o` orders, and `as -o` continue to select `exec`. A raw image and map are
prepared together. An explicit `-f` requires `-o`. A failed command write or
replacement restores the previous pair. If restoration fails, the backup is
kept for the next command to recover. This protects one running command; the
kernel pair writes matching v1 completion records beside both outputs after
both public replacements. Those records name its private backups and markers,
so a later command can finish cleanup. Without a valid record, retained
backups are restored. The VFS path has no pending record or absence tombstone
before mutation, and its recovery removes a readable target before restoring
the backup. The VFS does not yet provide a crash-atomic two-file transaction
or a concurrent-writer lock. The hosted command uses a separate, stronger
protocol, including absence markers for outputs that did not exist before the
command. Each member receives a linked v2 pending record before either public
target moves. After both replacements succeed, the records advance to v3 one
at a time. One matching v3 record is the pair's commit witness. Recovery treats
v2 as pending, does not let a legacy v1 peer commit it, and reaches the same
decision regardless of marker order. It removes the final valid v3 witness
only after private cleanup succeeds. A nonmatching record cannot clean an
unrelated private pair. Recovery replaces a backup over its target without
deleting the target first, so a failed replacement preserves the readable
public file. ADR 0337 records the kernel boundary, and ADR 0348 records the
hosted pair protocol and kernel v2 edge retention. Native Windows fixed-point CupidASM links the publication
startup and runtime objects and imports DeleteFileA, FlushFileBuffers,
GetFullPathNameA, and MoveFileExA for the same recovery path. The behavior
relink validator reads that plan-derived profile rather than the smaller
ordinary-tool table. Linux native Windows evidence uses the same publication
objects and imports.

For the in-kernel `as -o` path, CupidASM emits one ELF32 relocatable object.
It applies the caller's ordered `main` and `_start` entry candidates, publishes
the selected spelling, and promotes only that code label to a global symbol.
In-kernel CupidLD then resolves relocations and links the executable at the
existing `0x01A00000` text address. JIT keeps the direct fixed-image path.
A private guest smoke ran `as -o /hello-aot /demos/hello.asm`, followed by
`exec /hello-aot`. CupidASM produced a 15,680-byte `ET_REL` object. CupidLD
linked an 8,536-byte ELF with two `PT_LOAD` segments, and PID 4 exited normally.
The complete smoke passed in 79.661 seconds. ADR 0276 records this split.

---

## Program Structure

CupidASM programs use NASM-style section directives. JIT and linked
executables require a `main:` or `_start:` entry label. Raw and unlinked
relocatable output do not.

```asm
section .data
    msg db "Hello from CupidASM!", 10, 0

section .text

main:
    push msg
    call print
    add  esp, 4
    ret
```

### Sections

| Section | Purpose |
|---------|---------|
| `section .text` | Code (instructions). Default section. |
| `section .data` | Initialized data (`db`, `dw`, `dd` directives). |
| `section .bss` | Uninitialized data (`resb`, `resw`, `resd`). Treated as data section. |

Raw output has one flat address space. Its source may select one section name
and may repeat that selection. Selecting a different section reports
`CT6000011` at the new directive. Use ELF32 or fixed-image output when the
source needs independent code and data sections.

An `equ` definition creates an absolute symbol and emits no section storage,
so it may appear before the raw source selects a section. The first
section-bound statement still claims implicit `.text` when no section
directive came first. Labels count as section-bound because they name a
position in their section.

A raw source may contain one `ORG`. A second `ORG` reports `CT6000010` at that
directive and publishes no new output. The request's initial origin is only a
default, so the one source directive may replace it.

### Labels

Labels are defined with a trailing colon:

```asm
my_function:
    ret
```

**Local labels** start with a `.` and are scoped to the nearest non-local label:

```asm
main:
    jmp .done
.done:
    ret
```

An exported or imported function can carry its ELF type in the declaration:

```asm
global dispatch:function
extern scheduler_resume:function

dispatch:
    call scheduler_resume
    ret
```

CupidASM writes these symbols as `STT_FUNC`. A declaration without
`:function` remains `STT_NOTYPE`, so exported data is not mistaken for code.
The type name is case-insensitive. A missing type name or any type other than
`function` reports `CT6000018` and leaves the prior output untouched. ADR 0335
records this boundary. All fourteen exports in the CupidBuild Windows startup
now use the function annotation, and the active-source contract rejects an
untyped startup export. ADR 0347 records that closure.

### Constants

Use `equ` to define numeric constants:

```asm
BUFFER_SIZE equ 1024
MAX_ITEMS   equ 64
```

### Includes

`%include "path.asm"` expands another source file before assembly. Relative
paths are resolved from the including file first, then through the VFS CWD.
Include depth is capped to prevent recursive include loops.

---

## Calling Convention

CupidASM uses the **cdecl** calling convention:

- Arguments pushed right-to-left onto the stack
- Caller cleans up explicit arguments after the call
- Return value in `eax`
- `eax`, `ecx`, `edx` are caller-saved (may be clobbered)
- `ebx`, `esi`, `edi`, `ebp` are callee-saved

`ret` accepts an optional unsigned 16-bit byte count. `ret 4` pops the return
address and then advances `esp` by four bytes. The hosted CupidC
structure-return ABI uses this form to remove its hidden result pointer. The
caller still cleans ordinary explicit cdecl arguments.

CupidASM encodes `ret 4` as `C2 04 00` and rejects `ret 65536` because the
operand does not fit the 16-bit field. CupidDis renders `C2 04 00` as
`ret 0x4`.

### Inspecting mixed-mode raw output

Hosted CupidDis can inspect a flat image that mixes 16-bit code, 32-bit code,
and data without splitting the file. Start with `--mode 16` or `--mode 32`,
then add each later range with `--range-at OFFSET:16|32|data`:

```text
cupiddis --raw --mode 16 --range-at 0x1f:data --range-at 0x210:32 \
    --range-at 0x254:data \
    --base 0x8000 kernel/smp_trampoline.bin
```

Offsets are relative to the start of the file. They must increase and remain
inside the input. CupidDis decodes code ranges and prints data ranges as `db`
rows. The caller is responsible for placing each code transition between
instructions. The older `--mode-at OFFSET:16|32` spelling remains available
when every range contains code.

CupidASM can also author the map from its parsed source:

```text
cupidasm -f bin --map boot.map -o boot.bin boot.asm
cupiddis --require-known --raw --range-map boot.map boot.bin
```

The `cupid.raw-map.v2` file records the exact image size, `ORG` base, coalesced
range starts, and ordered source-resolved control edges. Instructions use their
active `BITS` mode. Data,
alignment, and reserved storage are data ranges. CupidDis rejects a stale
size, repeated or missing fields, invalid kinds, and unordered starts before
decoding. The map option cannot be combined with manual mode, base, or range
options. Version 1 remains accepted as a compatibility input. ADR 0277 records
the first schema.

A v2 row records
its instruction offset, relative, far, or indirect kind, local, external, or
unprovable class, resolved destination, target mode, and far segment when one
is encoded. CupidDis accepts both schemas. Add `--require-source-edges` to bind
decoded bytes to the v2 destinations:

```text
cupiddis --require-known --require-local-targets \
    --require-source-edges --raw --range-map boot.map boot.bin
```

The extra rule catches a branch redirected to a different valid instruction
start and checks immediate far mode transitions. It records indirect register
or memory transfers as unprovable because their destination comes from runtime
state. The guarded boot and SMP transactions require these rows before
publication. ADR 0340 records the v2 contract, ADR 0336 records v1 seed
carriage and adoption, and ADR 0353 records active v2 carriage.

One checked raw-image transaction serves the SMP and bootloader callers. It
owns output locking, source and seed freezing, drift checks, private candidates,
and atomic publication. Each caller retains its image-size and map policy. The
SMP caller requires CupidASM's private map to match the fixed AP startup layout
before CupidDis runs. The accepted map stays pinned through inspection and is
removed with the private transaction root. The
expanded eleven-test suite passed in 1.708 seconds, including direct mismatch
and live-output drift checks for both callers. Parent-replacement tests exposed
a POSIX candidate leak when private work lived below the output parent. Private
roots now live directly below the stable repository root. Both caller modules
pass all 10 tests on Windows and through WSL. The normal bootloader Make edge
calls the guarded transaction with the production manifest and full checked
seed. Hostbuild publishes only after CupidASM and CupidDis accept the private
image and map. ADR 0283 records the cutover.

The ISR and context-switch objects now enter the shared checked assembly
transaction too. CupidASM writes a private ELF32 relocatable, hostbuild applies
the shared structural validator and requires executable bytes, and CupidDis
must decode every executable byte. The source, seed, candidate, live output,
and output parent must still match before atomic publication. The later final
kernel gate remains as a whole-kernel check. ADR 0286 records this object
boundary.

The normal SMP trampoline recipe uses CupidASM's own map as a publication gate.
Hostbuild freezes the selected seed and source, asks CupidASM for private image
and map candidates, and requires the map to match the canonical 4 KiB policy.
It then runs CupidDis with `--raw --range-map`, `--require-known`,
`--require-local-targets`, and `--require-source-edges`. The local-target option always requires
`--require-known`. This production call also supplies `--raw` and the exact map:
code16
`[0x000, 0x01f)`, data `[0x01f, 0x210)`, code32 `[0x210, 0x254)`, and data
`[0x254, 0x1000)`. A local-target check on raw input that contains code16
rejects images larger than 65,536 bytes because wrapped target mapping would
be ambiguous. Four direct relative targets must land on instruction starts in
the matching code mode. The v2 map also binds the far mode transition and
records the indirect call as unprovable. Only a validated candidate may
replace the prior output. ADR 0271 records the fixed map. ADR 0308 records the
source-derived handoff.

The production boot transaction applies the same local-target rule to its
2,560-byte candidate and range map. It checks nine direct relative targets and
binds the far jumps through v2 source edges. Both callers distinguish a target outside the image,
inside data, in the wrong mode, or in the middle of an instruction. Far
pointers and indirect register or memory targets remain outside the rule. A
displacement that reaches a different valid instruction start fails when it
does not match the source-resolved destination. ADR 0300 records the older
boundary, ADR 0340 records the source-resolved rule, and ADR 0336 records its
production adoption.
ADR 0305 records raw-image carriage. ADR 0312 records the relocatable-object
promotion and production adoption. ADR 0318 records the preceding linked-image
promotion, ADR 0323 records the preceding code-anchor promotion, and ADR 0336
records the parent v1 promotion. ADR 0353 records the active paired v2
promotion.

CupidDis can apply the same explicit option to a static ELF32
relocatable object:

```text
cupiddis --require-known --require-local-targets program.o
```

Each executable `PROGBITS` section gets one reusable instruction-start map for
reporting, relocation ownership, and anchor checks. Local-target validation
adds only its required target walk. An unrelocated direct relative target must stay inside that
section and land on an instruction start there. A relocation at the operand
field leaves the destination for link time, while the existing executable
relocation rule still checks its field. Failures distinguish a target outside
the section from one in the middle of an instruction. Checked-seed `ET_EXEC`
still receives the focused rejection.

The active ISR and context-switch objects pass the checked-seed policy. Eleven
ISR call relocations are excluded from the local count. A one-byte change to a
context-switch branch produces one mid-instruction failure. Both promoted
seeds carry this form, and production object publication selects it before
replacing the prior object. ADR 0309 records the source boundary, and ADR 0312
records carriage and adoption.

Checked CupidDis also accepts the option for linked i386 ELF32 input. It
scans nonoverlapping file-backed executable load regions twice. A direct target
may cross regions, but it must land on an instruction start. The report
separates targets outside loaded memory, in loaded memory without file-backed
executable code, and inside an instruction. A `PT_DYNAMIC` or `PT_INTERP`
header rejects the image as outside the static certification domain. ADR 0314
records the source boundary.

The generated active-source audit and its check both pass. The Linux audit
records 23 failure groups, six help groups, and 29 success groups. The Windows
audit records 12 failure groups, six help groups, and 16 success groups.
Both checked seeds now carry the linked-image rule. The normal kernel publisher
uses it on the pass-one and final ELFs before flattening.

Checked CupidDis also has a static ELF code-anchor option:

```text
cupiddis --require-known --require-code-anchors program.o
cupiddis --require-known --require-code-anchors program.elf
```

For an object, it checks every defined function against decoded starts in
executable `PROGBITS`. For a linked image, it checks the ELF entry and defined
functions against file-backed decoded starts. Function aliases are separate
anchors. Both promoted seeds carry the option. Production checks the ISR,
context-switch, hosted startup, and linked kernel boundaries. ADR 0320 records
the linked source rule, ADR 0335 records the object rule, and ADR 0336 records
its v1 promotion and adoption. ADR 0353 records active v2 carriage.

### Requiring complete code coverage

Checked-seed CupidDis can validate one or more files without printing a
disassembly:

```text
cupiddis --require-known FILE [FILE...]
```

Success means every selected code region decoded without an unknown, invalid,
or truncated instruction. A failure names the input and reports known,
unknown, invalid, and truncated counts. CupidDis continues through later
inputs so one run reports the whole failing set. Declared raw data and
non-executable ELF regions do not count. Ordinary rendering still accepts one
file and keeps its existing output. The normal kernel path runs strict
validation and flat extraction against one frozen cohort of all 429 audited
root object outputs plus the pass-one and final kernel ELFs. Its 9,076-byte manifest lists those 431 unique
paths in graph order and has SHA-256
`4f1936423ae06418fc2f75603c29a91997608fe82f48c323321523aed25a2ab0`.
An immutable first-opcode index preserves exhaustive selection, and the
checked 128 KiB throughput contract passes within 30 seconds. ADR 0262 records
the command, ADR 0266 records indexed decoding, and ADR 0265 records seed
carriage and production adoption.

Source-head and checked-seed CupidDis extend the object form of this policy. A relocation in
an executable section must begin at a decoded four-byte operand field.
`R_386_PC32` requires a relative field, and `R_386_32` requires a
non-relative field. The typed report counts total and unmatched executable
relocations. Data-section relocations remain outside the code policy. The
checked Linux and Windows seeds carry this rule, so the normal object
transactions reject unmatched executable relocations before publication. ADR
0290 records the relocation boundary, and ADR 0292 records the seed promotion.

An earlier poisoned-host normal `make -j2` passed in 1,057.969 seconds and ran
the separate strict gate before CupidObj flattened the kernel. It produced a 9,162,816-byte
final ELF with SHA-256
`a0b57cd886369762b65d657bb3f2915ada8f30b52102535add89466eaf4f5976` and an
8,946,332-byte raw kernel with SHA-256
`4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d`.
At that handoff checkpoint, hostbuild froze the selected seed manifest and all
five artifacts, the 431-entry input manifest and cohort, and the existing
`kernel.bin` boundary. Checked CupidDis validated the private cohort before
checked CupidObj flattened the frozen final ELF. Hostbuild rechecked live trust
inputs and the output before parent-relative atomic publication. Every failure
preserved the prior raw kernel. The transaction passed with exit 0 in 187.054
seconds and published the same 8,946,332-byte raw kernel and SHA-256. The
focused hostbuild suites each passed 31 tests on Windows and in WSL;
platform-specific cases were skipped on the opposite host. Moving private
flatten extraction onto the shared pinned-path helper remains deferred
maintenance.
The next 2026-08-13 poisoned-host checkpoint completed through the checked
native Windows execution seed. Its first invocation stopped at the 602.5-second
command limit; the resumed build finished in 968.5 seconds, for 1,571.0 seconds
of cumulative work. These artifacts superseded the earlier identities above
when the checkpoint was recorded.
The 2,560-byte `boot.bin` has SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The 9,056,612-byte pass-one ELF has SHA-256
`e2f63b5cd9c4e2769b9d6bc893ab5cf778951b97aec954ece6cbac0cc429e92a`,
the 9,179,492-byte final ELF has SHA-256
`1bc06263dbf9849e6d2c594b6fb4be2a3f3b673c91f69d23a2d2e639b1f64776`,
and the 8,962,776-byte raw kernel has SHA-256
`3170aa71eafa656b1f6e23c918f1f472860f513c9c5cd0376d7d4f5f8a7d891c`.
The exact-size prerequisite accepted all nine artifacts before publishing the
209,715,200-byte image with SHA-256
`3b5dd6523a90d6ed0543a6ab2464892f3289b876654f9869f88db0901940b91e`.
A four-vCPU RTL8139 frontier passed from this image in 820.7 seconds. Private
CupidC emitted both feature-13 derived-update markers, compiled and loaded the
dedicated external ELF as PID 4, and reported that same PID exiting. The full
SMP, framebuffer, audio, USB detach/replug, and survival checks passed. The
completed dual-NIC checkpoint immediately before this
rebuild used image SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.

The guarded 2026-08-14 production checkpoint includes in-kernel CupidLD and
the guarded normal boot edge. A poisoned-host normal build passed in 674.693 seconds
after CupidDis accepted all 431 inputs. The pass-one ELF is 9,211,340 bytes,
the final ELF is 9,334,220 bytes, and the raw kernel is 9,114,084 bytes. Their
SHA-256 values are
`2a6f5deafb580b30254483179d6dade9ed4ed7b17b39f9368137b1ff14932263`,
`bc855462c1f8f42e34d94a974443f7c6e565d60b1913e3b6f33b3e6e375f3ed6`, and
`8b5d73e74538ce11c1fb074f88b3852d690038aa5cb3a8de3ce222e9df88cade`.
The 209,715,200-byte image has SHA-256
`813c9b0c78f795c1ac9fcff59b9c4111a958a07eb1e3943dc7af60c536521110`.
A private four-vCPU QEMU boot reached JIT completion in 49.257 seconds.
The definitive four-vCPU E1000 and RTL8139 boot frontiers remain pre-freeze
runtime evidence. They passed
with exits 0 in 794.034 and 758.667 seconds. Both passed SMP, frontier,
framebuffer, AC97, and PC speaker checks without changing the source image.

### Hosted i386 commands

CupidC emits the unchanged C source closures for CupidC, CupidASM, CupidDis,
CupidLD, and CupidObj under the checked four-byte i386 Linux ABI. CupidASM
assembles the repository `_start` and `int 0x80` system-call boundary. CupidLD
then links five deterministic static i386 commands without unresolved symbols.

The repository runtime supplies the checked file, heap, memory, string,
`errno`, `getcwd`, and formatted-output calls required by those commands. It
has unbuffered streams and single-threaded process state. Each closure object
is emitted twice before the repeated links. A sixth generated executable
checks allocation, tail release, files, seeks, errors, arguments, memory
comparison, and strings. WSL checks the sibling commands against the native
tools for raw and ELF32 assembly, include resolution, raw disassembly with mode
changes, fixed-address linking, object wrapping, and missing files. Successful
output matches byte for byte or as text. Invalid assembly and malformed linker
input follow the same failure behavior and diagnostics.

At the pre-stack-probe checkpoint, the five static tools crossed one complete
stage boundary under WSL. Generation-one CupidC, CupidASM, and CupidLD built all
19 C objects, startup, and the five stage-two images. Stage-two CupidC,
CupidASM, and CupidLD repeated the build for stage three. Every object and
linked CupidC, CupidASM, CupidDis, CupidLD, and CupidObj image matched byte for
byte. The two stages also agreed
on raw and ELF32 assembly, disassembly, symbol listing, fixed-address and
scripted linking, binary and canonical-text wrapping, executable flattening,
help, and useful failures.

The checked i386 Linux seed at that checkpoint included CupidASM and bound it
to the complete toolchain build plan. The bootstrap copied all 50 source inputs
into a private root. Checked CupidASM assembled stage-two startup there, and
the stage-two assembler produced the byte-identical stage-three startup below
the same root. The private and live closures were checked after each stage and
after behavior tests. Startup objects and the rest of the fixed-point evidence
were published together only after the full gate passed. See
[Toolchain Bootstrap](Toolchain-Bootstrap) for the manifest and staged build.
An independent poisoned-host reproof passed in 766.9 seconds. All five seed
images matched stage two, and stage two matched stage three across all nineteen
C objects, startup, five tools, and the 5/18/16 behavior matrix.
Normal Cupid OS C roots and Toolchain contracts now use checked CupidC.
A host compiler remains only for explicit native oracles and hosted
development commands.

Checked-seed CupidASM also assembles the repository Windows entry. An imported
call such as `call dword [__imp_WriteFile]` emits `FF 15` with one known,
zero-addend `R_386_32` relocation. CupidLD binds that operand to its IAT cell.
A direct call emits `R_386_PC32` and fails at link time. An absolute import
reference with a nonzero addend also fails, so an input cannot address past
the IAT cell. Both rebuilt assemblers produce identical Windows startup
objects. Source head also assembles the native tool entry and twelve shared
cdecl API bridges through both stages, plus four publication bridges for
CupidLD. Those startup objects match, and CupidLD links all five native tool
closures. Windows runs help plus a useful success and failure path for each
tool. CupidDis also checks quoted raw-input parity, while CupidLD checks exact
output, candidate collision, failure diagnostics, and cleanup. Those PE images
formed the preceding checked Windows execution seed used by output-bearing
production recipes. The Linux seed remains the build-plan root. The native
driver pairs both
manifests and builds native stages two through four. It compares stages three
and four. At the preceding v1 checkpoint, Windows and Linux passed the complete
final-pair artifact and behavior gates on one frozen uncommitted source
snapshot. Linux later passed its clean proof, promoted stage four, and passed a
1,473.9-second reproof from the new seed. Native Windows then passed its clean
proof in 1,253.4 seconds, promoted the 438,784-byte CupidASM image with SHA-256
`c54bb09f1eb317a23d1680da25c78a5a439bde44654ae8b908ddca11fd7e56d6`,
and passed a 1,061.3-second reproof with every initial seed comparison true.
ADR 0268 records the shared runtime, ADR 0269 records CupidLD publication, ADR
0272 records checked carriage and production selection, and ADRs 0278 and 0279
record native reconstruction and convergence. ADRs 0280, 0281, and 0292
record preceding Linux and Windows promotions. ADR 0318 records the preceding
linked-image promotion, ADR 0323 records the preceding code-anchor promotion,
and ADR 0336 records the parent v1 promotion. ADR 0353 records the active paired
v2 promotion.

### Function Example

```asm
; add_numbers(a, b) - returns a + b in eax
add_numbers:
    push ebp
    mov  ebp, esp
    mov  eax, [ebp+8]     ; first argument
    add  eax, [ebp+12]    ; second argument
    pop  ebp
    ret

main:
    push 27               ; second arg
    push 15               ; first arg
    call add_numbers
    add  esp, 8           ; clean up 2 args
    ; eax = 42
    ret
```

### Stack Frame Layout

```
         ┌─────────────┐  (high addresses)
         │  arg 2      │  [ebp+12]
         │  arg 1      │  [ebp+8]
         │  return addr│  [ebp+4]
         │  saved ebp  │  [ebp]     ← ebp points here
         │  locals...  │  [ebp-4], [ebp-8], ...
         └─────────────┘  (low addresses, esp grows down)
```

---

## Data Directives

| Directive | Size | Example |
|-----------|------|---------|
| `db` | 1 byte | `msg db "Hello", 10, 0` |
| `dw` | 2 bytes | `port dw 0x3F8` |
| `dd` | 4 bytes | `count dd 42` |
| `resb` | Reserve N bytes | `buffer resb 256` |
| `resw` | Reserve N words | `table resw 16` |
| `resd` | Reserve N dwords | `array resd 8` |
| `rb` | Alias of `resb` | `buffer rb 256` |
| `rw` | Alias of `resw` | `table rw 16` |
| `rd` | Alias of `resd` | `array rd 8` |
| `reserve` | Alias of `resb` | `scratch reserve 64` |
| `times` | Repeat | `times 10 db 0` |
| `equ` | Constant | `SIZE equ 1024` |
| `align` | Pad to an address or section boundary | `align 16, 0x90` |

You can declare reserve/data directives in either style:

```asm
buffer resb 256
buffer: resb 256
array:  rd 8
scratch reserve 64
```

### Alignment

`align POWER_OF_TWO[, FILL_BYTE]` advances to the next requested boundary.
The boundary must be a nonzero power of two, and the optional fill must fit
in one byte. The fill defaults to zero.

```asm
section .data
    db 1
    align 16, 0xcc
aligned_buffer:
    times 512 db 0
```

For a raw binary, the boundary applies to `ORG + output offset`. For an ELF32
object, it applies to the section offset and raises the section's recorded
alignment. For a fixed image, CupidASM includes the caller-provided absolute
region base in the calculation. NOBITS sections may use only zero fill; the
padding changes memory size without adding bytes to the file.

### String Data

Strings are `db` directives with quoted text. Newline is `10`, null terminator is `0`:

```asm
section .data
    hello  db "Hello, World!", 10, 0
    prompt db "> ", 0
    digits db "0123456789", 0
```

### Arrays

```asm
section .data
    numbers dd 5, 3, 8, 1, 9, 2, 7, 4
    count   dd 8
```

---

## Instruction Reference

CupidASM uses the shared Cupid Toolchain x86 catalogue. The checked seed and
source head carry 604 forms, 249 canonical mnemonics, and 64 register names,
with catalogue fingerprint `55A8970F`. The catalogue includes signed x87
`FILD` and `FISTP` memory operands at 16, 32, and 64 bits and canonical `SETP`
and `SETNP` byte predicates. Four forms cover canonical SHRD with
immediate or fixed CL counts. The forward x87 form encodes
`FSUB ST(1), ST(0)` as `DC E9`. The four preceding x87 forms encode and decode
80-bit `FLD` and `FSTP`
memory operands, i686 `FUCOMIP ST0, ST(i)`, and operand-free `FLDZ`. Both checked stages rebuild
this catalogue, which drives instruction encoding and decoding for all sixteen
i686 conditional moves. They accept 16-bit or 32-bit same-width register and
memory sources in either mode. Common alias spellings assemble to the same
bytes, while CupidDis prints canonical names. Three-operand `IMUL` accepts a
16-bit or 32-bit register destination, a same-width register or memory source,
and an immediate. CupidASM uses `6B /r` when the value fits a signed byte and
`69 /r` otherwise. ADR 0207 records forward stack subtraction, ADR 0208
records its seed promotion, ADR 0226 records SHRD, and ADR 0228 records
SHRD's first seed carriage. ADR 0243 records the preceding seed, ADR 0252
records the x87 integer forms, ADR 0258 records the preceding promotion, ADR
0259 records the parity predicates, ADR 0265 records their preceding seed
carriage, and ADRs 0280 and 0292 record preceding seeds. ADR 0305 records raw
local-target carriage, ADR 0312 records the preceding local-target seed, ADR
0318 records the preceding linked-image seed, ADR 0323 records the preceding
code-anchor seed, and ADR 0336 records the parent v1 seed. ADR 0353 records the
active paired v2 seed.

`setp` and `setnp` accept one byte register or memory destination in either
mode. They encode as `0F 9A /r` and `0F 9B /r`. Address-size overrides work
through the ordinary byte-memory recipe. This source-driven slice does not
add the `setpe` or `setpo` aliases. The guest disassembles and executes the
bounded `test_fpaug.cc` parity cases before running the full feature-13
comparison and truth behavior. The GUI shell also mirrors disassembly listings
to serial after its normal sink and redirection checks, which makes production
CupidDis output visible to the automated runtime proof.

`fild` and `fistp` accept only signed integer memory operands. The checked seed
supports `word`, `dword`, and `qword` widths. Register, byte, and 80-bit memory
operands fail before CupidASM publishes output. CupidDis renders the same
canonical width spellings from the shared rows.

`fsub st1, st0` emits `DC E9` and computes `ST1 - ST0` into `ST1`.
The second operand is fixed at `ST0`; reversing the registers is rejected.
The older `fsubr st1, st0` form remains `DC E1`.

`fucomip st0, st1` emits `DF E9`. The first operand must be `ST0`; the
second operand selects `ST0` through `ST7`. The instruction compares `ST0`
with that source, writes ZF, PF, and CF, and pops `ST0` once. CupidC follows
it with `fstp st0` when a long-double comparison must discard the surviving
x87 value.

Ordinary padding NOPs use the same model. `nop` emits `90`. A word or
doubleword register or memory operand emits `0F 1F /0`, with normal
operand-size, address-size, and segment overrides. An unsized memory operand
uses the current mode's default width:

```asm
bits 32
nop [eax]          ; 0F 1F 00
nop word [eax]     ; 66 0F 1F 00

bits 16
nop [bx + si]      ; 0F 1F 00
nop dword [bx+si]  ; 66 0F 1F 00
```

Fixed-point builds can use these NOP forms directly from the checked seed.

CupidDis also recognizes five exact 32-bit Clang alignment strings with two
through six leading `66` bytes followed by
`2E 0F 1F 84 00 00 00 00 00`. This is a private decode-only exception.
CupidASM cannot request redundant prefixes, and every other repeated-prefix
spelling remains invalid.

### Data Movement

| Instruction | Description | Example |
|-------------|-------------|---------|
| `mov` | Move data | `mov eax, 42` / `mov eax, [ebp+8]` |
| `push` | Push to stack | `push eax` / `push 42` / `push msg` |
| `pop` | Pop from stack | `pop eax` |
| `lea` | Load effective address | `lea eax, [ebx+ecx*4]` |
| `xchg` | Exchange values | `xchg eax, ebx` |
| `movzx` | Move with zero-extend | `movzx eax, al` |
| `movsx` | Move with sign-extend | `movsx eax, al` |
| `cmovcc` | Move when a condition is true | `cmovne eax, [value]` |

### Arithmetic

| Instruction | Description | Example |
|-------------|-------------|---------|
| `add` | Add | `add eax, ebx` / `add eax, 10` |
| `sub` | Subtract | `sub eax, 1` |
| `mul` | Unsigned multiply (EDX:EAX) | `mul ebx` |
| `imul` | Signed multiply | `imul ebx` / `imul eax, ecx, 0x228` |
| `div` | Unsigned divide (EAX/reg) | `div ecx` |
| `idiv` | Signed divide | `idiv ecx` |
| `inc` | Increment by 1 | `inc eax` |
| `dec` | Decrement by 1 | `dec ecx` |
| `neg` | Two's complement negate | `neg eax` |

### Bitwise & Logic

| Instruction | Description | Example |
|-------------|-------------|---------|
| `and` | Bitwise AND | `and eax, 0xFF` |
| `or` | Bitwise OR | `or eax, 1` |
| `xor` | Bitwise XOR | `xor eax, eax` |
| `not` | Bitwise NOT | `not eax` |
| `shl` | Shift left | `shl eax, 2` |
| `shr` | Shift right (logical) | `shr eax, 1` |
| `sar` | Shift right (arithmetic) | `sar eax, 1` |
| `rol` | Rotate left | `rol eax, 4` |
| `ror` | Rotate right | `ror eax, 4` |

### Comparison & Test

| Instruction | Description | Example |
|-------------|-------------|---------|
| `cmp` | Compare (sets flags) | `cmp eax, 0` |
| `test` | Bitwise AND test (sets flags) | `test eax, eax` |
| `setp` | Set byte when parity is set | `setp dl` |
| `setnp` | Set byte when parity is clear | `setnp byte [eax]` |

### Control Flow

| Instruction | Description | Condition |
|-------------|-------------|-----------|
| `jmp` | Unconditional jump | - |
| `call` | Call function | - |
| `ret` / `ret imm16` | Return, optionally releasing stack bytes such as `ret 4` | - |
| `je` / `jz` | Jump if equal / zero | ZF=1 |
| `jne` / `jnz` | Jump if not equal / not zero | ZF=0 |
| `jl` | Jump if less (signed) | SF≠OF |
| `jg` | Jump if greater (signed) | ZF=0 and SF=OF |
| `jle` | Jump if less or equal | ZF=1 or SF≠OF |
| `jge` | Jump if greater or equal | SF=OF |
| `jb` | Jump if below (unsigned) | CF=1 |
| `ja` | Jump if above (unsigned) | CF=0 and ZF=0 |
| `jbe` | Jump if below or equal | CF=1 or ZF=1 |
| `jae` | Jump if above or equal | CF=0 |
| `jp` / `jpe` | Jump if parity / parity even | PF=1 |
| `jnp` / `jpo` | Jump if not parity / parity odd | PF=0 |
| `js` | Jump if sign (negative) | SF=1 |
| `jns` | Jump if not sign | SF=0 |
| `jo` | Jump if overflow | OF=1 |
| `jno` | Jump if not overflow | OF=0 |

### System & Misc

| Instruction | Description |
|-------------|-------------|
| `nop` | No operation |
| `hlt` | Halt CPU |
| `cli` | Clear interrupt flag |
| `sti` | Set interrupt flag |
| `int` | Software interrupt (`int 0x80`) |
| `iret` | Return from interrupt |
| `iretd` | 32-bit return from interrupt alias |
| `in` | Read from I/O port |
| `out` | Write to I/O port |
| `lgdt` / `lidt` | Load descriptor tables |
| `sgdt` / `sidt` | Store descriptor tables |
| `ltr` / `str` / `sldt` | Task/LDT register helpers |
| `smsw` / `lmsw` | Machine status word helpers |
| `invlpg` | Invalidate one TLB page |
| `cpuid` / `rdtsc` | CPU identification and timestamp counter |
| `rdmsr` / `wrmsr` | Model-specific register I/O |
| `sysenter` / `sysexit` / `syscall` | Fast syscall-family opcodes |
| `clts` / `wbinvd` / `invd` | Privileged CPU control/cache ops |
| `leave` | Destroy stack frame (`mov esp, ebp; pop ebp`) |
| `cdq` | Sign-extend EAX into EDX:EAX |
| `cbw` | Sign-extend AL into AX |
| `cwde` | Sign-extend AX into EAX |
| `pushad` | Push all 32-bit general registers |
| `popad` | Pop all 32-bit general registers |
| `pushfd` | Push EFLAGS |
| `popfd` | Pop EFLAGS |
| `bswap` | Byte-swap a 32-bit register |
| `xadd` / `cmpxchg` | Atomic read-modify-write primitives |
| `lock` | Prefix for supported atomic memory operations |

### String Operations

| Instruction | Description |
|-------------|-------------|
| `rep` | Repeat prefix for string ops |
| `movsb` | Move byte (ESI -> EDI) |
| `movsd` | Move dword (ESI -> EDI) |
| `stosb` | Store AL at EDI |
| `stosd` | Store EAX at EDI |

---

## Registers

### 32-bit General Purpose

| Register | Index | Typical Use |
|----------|-------|-------------|
| `eax` | 0 | Accumulator, return value |
| `ecx` | 1 | Counter (loops, shifts) |
| `edx` | 2 | Data, I/O port, mul/div high bits |
| `ebx` | 3 | Base pointer (callee-saved) |
| `esp` | 4 | Stack pointer |
| `ebp` | 5 | Base/frame pointer (callee-saved) |
| `esi` | 6 | Source index (callee-saved) |
| `edi` | 7 | Destination index (callee-saved) |

### 16-bit and 8-bit

The assembler also supports 16-bit (`ax`, `cx`, `dx`, `bx`, `sp`, `bp`, `si`, `di`) and 8-bit (`al`, `cl`, `dl`, `bl`, `ah`, `ch`, `dh`, `bh`) register names.

---

## Kernel Bindings (JIT Mode)

In JIT mode, the assembler pre-registers kernel functions as labels. Programs can `call` them directly using cdecl convention (push args right-to-left, caller cleans stack).

### Console Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `print(const char *str)` | Print a null-terminated string |
| `putchar` | `putchar(char c)` | Print a single character |
| `print_int` | `print_int(int n)` | Print a decimal integer |
| `print_hex` | `print_hex(uint32_t n)` | Print a hex value (0xNNNNNNNN) |
| `clear_screen` | `clear_screen()` | Clear the terminal |

### Memory

| Function | Signature | Description |
|----------|-----------|-------------|
| `kmalloc` | `void *kmalloc(size_t size)` | Allocate heap memory |
| `kfree` | `kfree(void *ptr)` | Free heap memory |

### Strings

| Function | Signature | Description |
|----------|-----------|-------------|
| `strlen` | `int strlen(const char *s)` | String length |
| `strcmp` | `int strcmp(const char *a, const char *b)` | Compare strings |
| `memset` | `memset(void *dst, int val, size_t n)` | Fill memory |
| `memcpy` | `memcpy(void *dst, const void *src, size_t n)` | Copy memory |

### File System (VFS)

| Function | Signature | Description |
|----------|-----------|-------------|
| `vfs_open` | `int vfs_open(const char *path, int flags)` | Open a file |
| `vfs_close` | `vfs_close(int fd)` | Close a file descriptor |
| `vfs_read` | `int vfs_read(int fd, void *buf, size_t n)` | Read from file |
| `vfs_write` | `int vfs_write(int fd, const void *buf, size_t n)` | Write to file |
| `vfs_seek` | `int vfs_seek(int fd, int off, int whence)` | Seek file position |
| `vfs_stat` | `int vfs_stat(const char *path, stat_t *st)` | Stat file |
| `vfs_readdir` | `int vfs_readdir(int fd, dirent_t *ent)` | Read directory entry |
| `vfs_mkdir` | `int vfs_mkdir(const char *path)` | Create directory |
| `vfs_unlink` | `int vfs_unlink(const char *path)` | Delete file |

### Process Control

| Function | Signature | Description |
|----------|-----------|-------------|
| `exit` | `exit()` | Exit JIT program (returns to shell) |
| `yield` | `yield()` | Yield CPU to scheduler |
| `getpid` | `getpid()` | Current process PID |
| `kill` | `kill(pid)` | Kill process by PID |
| `sleep_ms` | `sleep_ms(uint32_t ms)` | Sleep for N milliseconds |

### Shell / Program

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell_execute` | `shell_execute(const char *line)` | Execute shell command line |
| `shell_get_cwd` | `const char *shell_get_cwd()` | Get shell CWD string |
| `exec` | `int exec(const char *path, const char *name)` | Launch executable |

### System

| Function | Signature | Description |
|----------|-----------|-------------|
| `uptime_ms` | `uint32_t uptime_ms()` | Get system uptime in ms |
| `memstats` | `memstats()` | Print memory statistics |

### Networking - BSD sockets

Ports passed to / returned from these calls are network byte order
(`htons(80)` for HTTP). See [Networking](Networking) for protocol
details.

| Function | Signature |
|---|---|
| `socket` | `int socket(int type)` - `2`=TCP, `1`=UDP |
| `bind` | `int bind(int fd, U32 ip, U16 port)` |
| `listen` | `int listen(int fd, int backlog)` |
| `accept` | `int accept(int fd, U32 *peer_ip, U16 *peer_port)` |
| `connect` | `int connect(int fd, U32 ip, U16 port)` |
| `send` / `recv` | stream I/O on TCP socket |
| `sendto` / `recvfrom` | UDP datagram I/O |
| `close` | `int close(int fd)` |
| `setsockopt` | `int setsockopt(int fd, int level, int opt, void *val, U32 vlen)` - level=`SOL_TLS`(1), opt=`TLS_ENABLE`(1), val=hostname for TLS 1.3 upgrade |
| `sock_avail` | `int sock_avail(int fd)` - bytes buffered (0 = recv would block) |
| `sock_state` | `int sock_state(int fd)` - returns `tcp_state_t` enum |
| `dns_resolve` | `int dns_resolve(char *name, U32 *out)` |
| `htons` / `ntohs` / `htonl` / `ntohl` | byte-swap helpers |

Equ constants registered alongside: `IP_PROTO_ICMP`, `IP_PROTO_UDP`,
`IP_PROTO_TCP`, `SOCK_TYPE_UDP`, `SOCK_TYPE_TCP`.

### Networking - interface info & raw protocol

| Function | Description |
|---|---|
| `net_get_ip` / `_gateway` / `_dns` / `_mask` | Primary NIC info, returns `U32` |
| `net_get_mac(out)` | Fills 6-byte MAC into `out` |
| `net_link_up` | 1 if link up |
| `net_rx_packets` / `net_tx_packets` | Counters |
| `net_rx_drops` / `net_tx_errors` | Drop / error counters |
| `ip_parse(s, out)` | `"a.b.c.d"` -> uint32 |
| `ipv4_send(dst, proto, payload, plen)` | Raw IPv4 (auto-fragments > MTU) |
| `arp_resolve(ip, mac_out)` | Blocking 500 ms ARP |
| `arp_dump`, `arp_get_entries` | Cache inspection |
| `icmp_send_echo(dst, id, seq, paylen)` | Ping request |
| `icmp_wait_reply(src, id, seq, timeout_ms)` | Block for matching reply |
| `udp_send_raw(dst, sport, dport, data, len)` | One-shot UDP |

### Block devices

| Function | Description |
|---|---|
| `blkdev_count` | Number of block devices |
| `blkdev_read(idx, lba, count, buf)` | Read N sectors from blkdev[idx] |
| `blkdev_write(idx, lba, count, buf)` | Write N sectors |
| `ata_read_sectors(drive, lba, count, buf)` | Direct ATA read |
| `ata_write_sectors(drive, lba, count, buf)` | Direct ATA write |

### Keyboard, serial, speaker, PIT - direct driver access

| Function | Description |
|---|---|
| `keyboard_read_event(out)` | Pop one event |
| `keyboard_inject_scancode(sc)` | Synthesize scancode |
| `keyboard_get_shift` / `_ctrl` / `_alt` / `_caps_lock` | Modifier state |
| `serial_read_char` / `serial_write_char` / `serial_write_string` / `serial_has_rx` | Direct COM1 |
| `pc_speaker_on(freq)` / `pc_speaker_off()` | PC speaker square wave |
| `pit_set_frequency(channel, hz)` | Reprogram PIT |
| `timer_delay_us(us)` | TSC busy delay |
| `outb` / `inb` | Raw 8-bit port I/O for new drivers |

### PCI introspection (by index)

| Function | Description |
|---|---|
| `pci_device_count` | Number of PCI devices found at boot |
| `pci_get_vendor(idx)` | 16-bit vendor ID |
| `pci_get_device_id(idx)` | 16-bit device ID |
| `pci_get_class(idx)` | Packed `class<<16 | sub<<8 | prog_if` |
| `pci_get_irq(idx)` | IRQ line |
| `pci_get_bar(idx, bar)` | BAR value (0..5) |

### SMP / LAPIC / paging / PMM

> Shared-state work must be protected with `bkl_lock` and `bkl_unlock`.

| Function | Description |
|---|---|
| `lapic_get_id` | This CPU's local APIC ID |
| `lapic_eoi` | End-of-interrupt (only from a real ISR) |
| `bkl_lock` / `bkl_unlock` | Big kernel lock - recursive ticket spinlock |
| `paging_map_mmio(phys, size)` | Identity-map an MMIO region |
| `pmm_alloc_page` / `pmm_free_page(page)` | 4 KB physical page allocator |

### Audio - AC97 driver

| Function | Description |
|---|---|
| `ac97_init` | Probe + init AC97. Returns 0 on success in eax |
| `ac97_start` | Arm DMA |
| `ac97_stop` | Halt + mute |
| `ac97_set_master_volume(pct)` | 0-100 master volume |
| `ac97_set_pcm_volume(pct)` | 0-100 PCM channel volume |
| `ac97_get_master_volume` | Returns last-set master pct (0 if absent) |
| `ac97_get_pcm_volume` | Returns last-set PCM pct |
| `ac97_tsc_sleep_ms(ms)` | TSC busy-wait |
| `ac97_is_present_int` | 0 / 1 |
| `ac97_smoke_sine` | 440 Hz triangle 2s |
| `ac97_smoke_sweep` | 50→8000 Hz sweep |
| `ac97_smoke_pan` | 1 kHz with L↔R pan |
| `audiotest_all` | sine + sweep + pan + opl |

### Audio - MIDI / OPL3 synth

| Function | Description |
|---|---|
| `midiopl_init(genmidi_lump, lump_len)` | Parse Doom GENMIDI patches |
| `midiopl_reset` | Silence channels, keep patches |
| `midiopl_feed(bytes, len)` | Stream MIDI bytes into synth |
| `midiopl_render(out_stereo, frames)` | Pull s16-stereo @ 22050 Hz |
| `midiopl_set_volume(0..127)` | Master synth volume |
| `opl_smoke` | OPL3 smoke test |

### Audio - PCM mixer

s16 stereo @ 22050 Hz, 16 slots.

| Function | Description |
|---|---|
| `mixer_init` | One-time init |
| `mixer_play(slot, pcm, frames, ch, loop, vol_l, vol_r)` | Start playback (returns 0 in eax on success) |
| `mixer_stop(slot)` | Stop slot |
| `mixer_active(slot)` | 1 if playing |
| `mixer_set_volume(slot, vol_l, vol_r)` | Per-slot volume |
| `mixer_fill(out, frames)` | Mix all active slots into `out` |

### Imaging - in-memory codecs

| Function | Description |
|---|---|
| `png_decode_mem(data, len, &out_pixels, &out_w, &out_h)` | PNG → fresh XRGB heap buffer (caller `kfree`s) |
| `jpeg_decode_mem(data, len, &out_pixels, &out_w, &out_h)` | Baseline JPEG, same convention |
| `bmp_decode_to_surface_fit(path, sid, w, h)` | Decode BMP into `gfx2d_surface[sid]`, fit to w×h |
| `kdeflate_raw(src, src_len, out, out_len)` | RFC 1951 raw DEFLATE; returns produced bytes / negative |

### 2D Graphics (full parity with CupidC)

CupidASM exposes the `gfx2d_*` surface, including:

These calls use the same process-wide framebuffer and resource state as
CupidC. Direct drawing must stay inside `gfx2d_fullscreen_enter` and
`gfx2d_fullscreen_exit`, or inside a window paint scope. Borrowed surface,
image, and font pointers remain valid only while that scope is held.

| Group | Functions |
|---|---|
| Image slots | `gfx2d_image_load` / `_load_mem` / `_free` / `_draw` / `_draw_region` / `_draw_scaled` / `_draw_transformed` / `_get_pixel` / `_width` / `_height` |
| Glyphs / text | `gfx2d_char` / `_char_scaled` / `_text_n` / `_text_simple` / `_text_width_n` / `_glyph_advance` |
| Shapes | `gfx2d_circle_thick` / `_line_thick` / `_tri` / `_tri_fill_gradient` |
| Gradients | `gfx2d_gradient_h_round` / `_v_round` / `_radial` |
| Capture | `gfx2d_capture_screen_to_surface` |

### GUI window API (full parity with CupidC)

| Function | Description |
|---|---|
| `gui_win_create(title, x, y, w, h)` | Create a window, returns wid in eax |
| `gui_win_close(wid)` | Destroy window |
| `gui_win_is_open(wid)` | 1 if still alive |
| `gui_win_focus(wid)` | Bring to focus |
| `gui_win_can_draw(wid)` | 1 if app may draw this frame |
| `gui_win_draw_frame(wid)` | Begin the legacy frame scope; pair it with `gui_win_flip` |
| `gui_win_content_x` / `_y` / `_w` / `_h(wid)` | Inner content rect |
| `gui_win_begin_paint` / `_end_paint(wid)` | Compositor paint scope |
| `gui_win_invalidate(wid)` / `_invalidate_rect(wid, x, y, w, h)` | Mark dirty |
| `gui_win_present(wid)` / `_flip(wid)` | Present back-buffer |
| `gui_win_poll_key(wid)` | Pop next key from this window's queue, -1 if empty |

The retained `gui_win_begin_paint` and `gui_win_end_paint` pair is preferred.
It selects the window surface and holds cross-process render ownership until
the target and clip state have been restored. Process cleanup releases an
abandoned fullscreen, retained, or legacy scope before PID reuse.

### libm

Float / double libm functions are bound directly. Caller is responsible
for the float ABI: push the argument(s) on the stack (4 bytes for
`float`, 8 for `double`), call, then `fstp` the result from the FPU
top-of-stack.

| Group | Functions |
|---|---|
| Trig | `sin` / `sinf`, `cos` / `cosf`, `tan` / `tanf`, `asin` / `asinf`, `acos` / `acosf`, `atan` / `atanf`, `atan2` / `atan2f` |
| Hyperbolic | `sinh` / `sinhf`, `cosh` / `coshf`, `tanh` / `tanhf` |
| Power / log | `exp` / `expf`, `exp2` / `exp2f`, `log` / `logf`, `log2` / `log2f`, `pow` / `powf`, `sqrt` / `sqrtf`, `cbrt` / `cbrtf` |
| Round / abs | `fabs` / `fabsf`, `ceil` / `ceilf`, `floor` / `floorf`, `round` / `roundf`, `trunc` / `truncf`, `fmod` / `fmodf` |
| Misc | `hypot` / `hypotf`, `nextafter` / `nextafterf` |

### Example: Audio smoke test

```asm
main:
    call ac97_init       ; init AC97 codec
    test eax, eax
    jnz  .done
    call ac97_smoke_sine ; 2s 440 Hz triangle
.done:
    ret
```

### Example: Using Kernel Bindings

```asm
section .data
    greeting db "Hello, ", 0
    name     db "CupidOS", 0
    newline  db 10, 0

section .text

main:
    push greeting
    call print
    add  esp, 4

    push name
    call strlen       ; returns length in eax
    add  esp, 4

    push eax
    call print_int    ; print the length
    add  esp, 4

    push newline
    call print
    add  esp, 4
    ret
```

---

## AOT Syscall Table (ELF Programs)

> Syscall table version: **3**. The layout is append-only. Programs built
> against version 2 still work and observe the larger `SYS_TABLE_SIZE`.
> `kernel/core/syscall.cc` has `_Static_assert` guards on the offsets below, so
> reordering a field causes a compile-time failure.

AOT-compiled programs receive a pointer to the syscall table at `[esp+4]` when executed. Use `SYS_*` constants (pre-defined as `equ` values) to call kernel functions indirectly:

```asm
section .text

main:
    mov  ebx, [esp+4]      ; syscall table pointer

    push msg
    call [ebx + SYS_PRINT]
    add  esp, 4

    ret

section .data
    msg db "Hello from AOT!", 10, 0
```

### SYS_* Constants

| Constant | Offset | Function |
|----------|--------|----------|
| `SYS_VERSION` | 0 | Version field |
| `SYS_TABLE_SIZE` | 4 | Table size |
| `SYS_SIZE` | 4 | Alias of `SYS_TABLE_SIZE` |
| `SYS_PRINT` | 8 | print() |
| `SYS_PUTCHAR` | 12 | putchar() |
| `SYS_PRINT_INT` | 16 | print_int() |
| `SYS_PRINT_HEX` | 20 | print_hex() |
| `SYS_CLEAR_SCREEN` | 24 | clear_screen() |
| `SYS_MALLOC` | 28 | kmalloc() |
| `SYS_FREE` | 32 | kfree() |
| `SYS_STRLEN` | 36 | strlen() |
| `SYS_STRCMP` | 40 | strcmp() |
| `SYS_STRNCMP` | 44 | strncmp() |
| `SYS_MEMSET` | 48 | memset() |
| `SYS_MEMCPY` | 52 | memcpy() |
| `SYS_VFS_OPEN` | 56 | vfs_open() |
| `SYS_VFS_CLOSE` | 60 | vfs_close() |
| `SYS_VFS_READ` | 64 | vfs_read() |
| `SYS_VFS_WRITE` | 68 | vfs_write() |
| `SYS_VFS_SEEK` | 72 | vfs_seek() |
| `SYS_VFS_STAT` | 76 | vfs_stat() |
| `SYS_VFS_READDIR` | 80 | vfs_readdir() |
| `SYS_VFS_MKDIR` | 84 | vfs_mkdir() |
| `SYS_VFS_UNLINK` | 88 | vfs_unlink() |
| `SYS_EXIT` | 92 | exit() |
| `SYS_YIELD` | 96 | yield() |
| `SYS_GETPID` | 100 | getpid() |
| `SYS_KILL` | 104 | kill() |
| `SYS_SLEEP_MS` | 108 | sleep_ms() |
| `SYS_SHELL_EXEC` | 112 | shell_exec() |
| `SYS_SHELL_EXEC_LINE` | 112 | Alias of `SYS_SHELL_EXEC` |
| `SYS_SHELL_CWD` | 116 | shell_cwd() |
| `SYS_UPTIME_MS` | 120 | uptime_ms() |
| `SYS_EXEC` | 124 | exec() |
| `SYS_VFS_RENAME` | 128 | vfs_rename() |
| `SYS_VFS_COPY_FILE` | 132 | vfs_copy_file() |
| `SYS_VFS_COPY` | 132 | Alias of `SYS_VFS_COPY_FILE` |
| `SYS_VFS_READ_ALL` | 136 | vfs_read_all() |
| `SYS_VFS_WRITE_ALL` | 140 | vfs_write_all() |
| `SYS_VFS_READ_TEXT` | 144 | vfs_read_text() |
| `SYS_VFS_WRITE_TEXT` | 148 | vfs_write_text() |
| `SYS_MEMSTATS` | 152 | memstats() |

### Phase 4/5 syscall table extensions (v3)

| Constant | Offset | Function |
|----------|--------|----------|
| `SYS_NET_GET_IP` | 156 | net_get_ip() |
| `SYS_NET_GET_GATEWAY` | 160 | net_get_gateway() |
| `SYS_NET_GET_DNS` | 164 | net_get_dns() |
| `SYS_NET_GET_MASK` | 168 | net_get_mask() |
| `SYS_NET_GET_MAC` | 172 | net_get_mac(out) |
| `SYS_NET_LINK_UP` | 176 | net_link_up() |
| `SYS_NET_RX_PACKETS` | 180 | net_rx_packets() |
| `SYS_NET_TX_PACKETS` | 184 | net_tx_packets() |
| `SYS_NET_RX_DROPS` | 188 | net_rx_drops() |
| `SYS_NET_TX_ERRORS` | 192 | net_tx_errors() |
| `SYS_IP_PARSE` | 196 | ip_parse(s, out) |
| `SYS_IPV4_SEND` | 200 | ipv4_send(dst, proto, payload, plen) |
| `SYS_ARP_RESOLVE` | 204 | arp_resolve(ip, mac_out) |
| `SYS_ARP_DUMP` | 208 | arp_dump() |
| `SYS_ARP_GET_ENTRIES` | 212 | arp_get_entries(ips, macs, max) |
| `SYS_ICMP_SEND_ECHO` | 216 | icmp_send_echo(dst, id, seq, paylen) |
| `SYS_ICMP_WAIT_REPLY` | 220 | icmp_wait_reply(src, id, seq, timeout_ms) |
| `SYS_UDP_SEND_RAW` | 224 | udp_send_raw(dst, sport, dport, data, len) |
| `SYS_DNS_RESOLVE` | 228 | dns_resolve(name, ip_out) |
| `SYS_HTONS` | 232 | htons(v) |
| `SYS_HTONL` | 236 | htonl(v) |
| `SYS_NTOHS` | 240 | ntohs(v) |
| `SYS_NTOHL` | 244 | ntohl(v) |
| `SYS_SOCKET` | 248 | socket(type) |
| `SYS_BIND` | 252 | bind(fd, ip, port) |
| `SYS_LISTEN` | 256 | listen(fd, backlog) |
| `SYS_ACCEPT` | 260 | accept(fd, peer_ip, peer_port) |
| `SYS_CONNECT` | 264 | connect(fd, ip, port) |
| `SYS_SEND` | 268 | send(fd, buf, len) |
| `SYS_RECV` | 272 | recv(fd, buf, len) |
| `SYS_SENDTO` | 276 | sendto(fd, buf, len, ip, port) |
| `SYS_RECVFROM` | 280 | recvfrom(fd, buf, len, ip, port) |
| `SYS_CLOSE` | 284 | close(fd) |
| `SYS_BLKDEV_COUNT` | 288 | blkdev_count() |
| `SYS_BLKDEV_READ` | 292 | blkdev_read(idx, lba, count, buf) |
| `SYS_BLKDEV_WRITE` | 296 | blkdev_write(idx, lba, count, buf) |
| `SYS_ATA_READ_SECTORS` | 300 | ata_read_sectors(drive, lba, count, buf) |
| `SYS_ATA_WRITE_SECTORS` | 304 | ata_write_sectors(drive, lba, count, buf) |
| `SYS_SERIAL_READ_CHAR` | 308 | serial_read_char() |
| `SYS_SERIAL_WRITE_CHAR` | 312 | serial_write_char(c) |
| `SYS_SERIAL_WRITE_STRING` | 316 | serial_write_string(s) |
| `SYS_SERIAL_HAS_RX` | 320 | serial_has_rx() |
| `SYS_PC_SPEAKER_ON` | 324 | pc_speaker_on(freq) |
| `SYS_PC_SPEAKER_OFF` | 328 | pc_speaker_off() |
| `SYS_PIT_SET_FREQUENCY` | 332 | pit_set_frequency(channel, hz) |
| `SYS_TIMER_DELAY_US` | 336 | timer_delay_us(us) |
| `SYS_PCI_DEVICE_COUNT` | 340 | pci_device_count() |
| `SYS_PCI_GET_VENDOR` | 344 | pci_get_vendor(idx) |
| `SYS_PCI_GET_DEVICE_ID` | 348 | pci_get_device_id(idx) |
| `SYS_PCI_GET_CLASS` | 352 | pci_get_class(idx) |
| `SYS_PCI_GET_IRQ` | 356 | pci_get_irq(idx) |
| `SYS_PCI_GET_BAR` | 360 | pci_get_bar(idx, bar) |
| `SYS_LAPIC_GET_ID` | 364 | lapic_get_id() |
| `SYS_LAPIC_EOI` | 368 | lapic_eoi() |
| `SYS_BKL_LOCK` | 372 | bkl_lock() |
| `SYS_BKL_UNLOCK` | 376 | bkl_unlock() |
| `SYS_PAGING_MAP_MMIO` | 380 | paging_map_mmio(phys, size) |
| `SYS_PMM_ALLOC_PAGE` | 384 | pmm_alloc_page() |
| `SYS_PMM_FREE_PAGE` | 388 | pmm_free_page(page) |
| `SYS_OUTB` | 392 | outb(port, val) |
| `SYS_INB` | 396 | inb(port) |

Equ constants registered alongside (compile-time literals, no syscall):
`IP_PROTO_ICMP`, `IP_PROTO_UDP`, `IP_PROTO_TCP`, `SOCK_TYPE_UDP`,
`SOCK_TYPE_TCP`.

Example: outbound TCP from AOT assembly:

```asm
section .text

main:
    mov  ebx, [esp+4]                  ; syscall table

    push 2                             ; SOCK_TYPE_TCP
    call [ebx + SYS_SOCKET]
    add  esp, 4
    mov  edi, eax                      ; fd

    push 80
    call [ebx + SYS_HTONS]             ; htons(80)
    add  esp, 4

    push eax                           ; port (network order)
    push 0x08080808                    ; 8.8.8.8 - replace w/ real IP
    push edi
    call [ebx + SYS_CONNECT]
    add  esp, 12
    ret
```

---

## Memory Addressing

CupidASM supports Intel-style memory operands with base, index, scale, and displacement:

```asm
mov eax, [ebx]           ; base only
mov eax, [ebp+8]         ; base + displacement
mov eax, [ebx+ecx*4]     ; base + index*scale
mov eax, [ebx+ecx*4+16]  ; base + index*scale + displacement
mov eax, [label]         ; absolute address (label)
```

**Supported scales**: 1, 2, 4, 8

---

## Memory Layout

| Region | Address | Size |
|--------|---------|------|
| JIT Code | `0x01A00000` | 1 MB |
| JIT Data | `0x01B00000` | 1 MB |

JIT code and data are separate from CupidC's JIT region (`0x01100000`-`0x01A00000`). Both can coexist.

---

## Demo Programs

Demo programs are included in the `demos/` folder:

The hosted corpus contract assembles all 22 files twice as fixed images with
implicit externs disabled. Its binding names match the kernel adapter. The
`parity_gfx2d.asm` fixture includes both `gfx2d_fullscreen_enter` and
`gfx2d_fullscreen_exit`, so the demo can release fullscreen ownership on its
normal and error paths.

| File | Description | Key Concepts |
|------|-------------|-------------|
| `hello.asm` | Hello World | `db`, `print`, sections, entry point |
| `math.asm` | Arithmetic operations | `add`, `sub`, `shl`, `div`, `and`, `or` |
| `loop.asm` | Sum 1..100 = 5050 | `cmp`, `jl`, `inc`, local labels |
| `stack.asm` | Function calls | Stack frames, `push`/`pop`, `call`/`ret` |
| `fibonacci.asm` | Fibonacci sequence | Register saves, loops, `print_int` |
| `factorial.asm` | Recursive factorial | Recursion, `mul`, stack frames |
| `data.asm` | Data directives | `db`, `dd`, arrays, `strlen` |
| `bubblesort.asm` | Bubble sort | Memory indexing, `shl`, in-place swap |
| `fs_syscalls.asm` | VFS/syscall usage | `vfs_open/read/write/close`, `getpid`, constants |
| `reserve_directives.asm` | Reserve directives | `resw` and `resd` layout/size checks |
| `asm_compat_reserve.asm` | NASM-compat reserve syntax | `rb/rw/rd`, `reserve`, `label: resb ...` |
| `syscall_table_demo.asm` | Syscall table calls | `syscall_get_table`, `SYS_*` offsets |
| `parity_core.asm` | Core parity smoke test | shell/process/time/rtc/string parity bindings |
| `parity_gfx2d.asm` | gfx2d parity smoke test | drawing/text/fullscreen-safe gfx2d calls |
| `parity_diag.asm` | Diagnostics parity smoke test | variadic print, logs, heap/stack/register diagnostics |
| `syscall_vfs_extended_demo.asm` | Extended syscall table VFS | `SYS_VFS_*` copy/read/write helper calls |

### Running Demos

```
> as demos/hello.asm
Hello from CupidASM!

> as demos/loop.asm
Sum of 1..100 = 5050

> as demos/factorial.asm
Factorials:
1! = 1
2! = 2
3! = 6
4! = 24
5! = 120
6! = 720
7! = 5040
8! = 40320
9! = 362880
10! = 3628800

> as demos/stack.asm
add_numbers(15, 27) = 42
multiply(6, 7) = 42
```

---

## Complete Example: Bubble Sort

```asm
; bubblesort.asm - Bubble sort on an integer array
section .data
    arr     dd 5, 3, 8, 1, 9, 2, 7, 4
    count   dd 8
    msg_before db "Before: ", 0
    msg_after  db "After:  ", 0
    space      db " ", 0
    newline    db 10, 0

section .text

; print_array() - prints all elements
print_array:
    push ebp
    mov  ebp, esp
    push esi
    push ecx

    mov  esi, arr
    mov  ecx, [count]
.print_loop:
    cmp  ecx, 0
    je   .print_done
    push ecx
    push dword [esi]
    call print_int
    add  esp, 4
    push space
    call print
    add  esp, 4
    pop  ecx
    add  esi, 4
    dec  ecx
    jmp  .print_loop
.print_done:
    push newline
    call print
    add  esp, 4
    pop  ecx
    pop  esi
    pop  ebp
    ret

; bubble_sort() - sorts arr[] in place
bubble_sort:
    push ebp
    mov  ebp, esp
    push esi
    push edi
    push ebx

    mov  ecx, [count]
    dec  ecx              ; outer loop: n-1 passes
.outer:
    cmp  ecx, 0
    jle  .sort_done
    xor  edi, edi         ; inner index
    mov  edx, ecx         ; inner limit
.inner:
    cmp  edi, edx
    jge  .next_pass

    ; Compare arr[edi] and arr[edi+1]
    mov  eax, edi
    shl  eax, 2           ; eax = edi * 4
    add  eax, arr         ; eax = &arr[edi]
    mov  ebx, [eax]       ; ebx = arr[edi]
    mov  esi, [eax+4]     ; esi = arr[edi+1]
    cmp  ebx, esi
    jle  .no_swap

    ; Swap
    mov  [eax], esi
    mov  [eax+4], ebx

.no_swap:
    inc  edi
    jmp  .inner

.next_pass:
    dec  ecx
    jmp  .outer

.sort_done:
    pop  ebx
    pop  edi
    pop  esi
    pop  ebp
    ret

main:
    push msg_before
    call print
    add  esp, 4
    call print_array

    call bubble_sort

    push msg_after
    call print
    add  esp, 4
    call print_array

    ret
```

---

## Differences from NASM

| Feature | NASM | CupidASM |
|---------|------|----------|
| Output formats | ELF, bin, COFF, etc. | JIT (execute) or ELF32 |
| Macros | Full macro system | Not supported |
| Preprocessor | `%define`, `%macro`, `%if` | `%include` only |
| Segments | Full segment support | `.text` and `.data` only |
| Linker | Separate step | Built-in (single file) |
| External symbols | Via linker | Kernel bindings (JIT) |
| Expressions | Full constant expressions | Numeric literals only |
| Local labels | `@@`, `.label` | `.label` (dot prefix) |
| 16/64-bit modes | `[bits 16]`, `[bits 64]` | 32-bit only |

---

## Error Messages

| Error | Cause |
|-------|-------|
| `cannot open <file>` | File not found or not readable |
| `file too large or empty` | Source could not be read or is 0 bytes |
| `no main: or _start: label found` | Missing entry point label |
| `undefined label '<name>'` | Forward reference to a label that was never defined |
| `duplicate label` | Same label name defined twice |
| `too many labels` | More than 8192 labels |
| `too many forward references` | More than 8192 unresolved references |
| `code buffer overflow` | Code section exceeds 1 MB |
| `data buffer overflow` | Data section exceeds 1 MB |
| `expected end of line` | Extra tokens after an instruction |
| `short jump out of range` | Branch target too far for rel8 |

---

## Tips

- Every program needs a `main:` or `_start:` entry label.
- Use `ret` to return from `main` to the shell. Do not use `hlt`, which stops the OS.
- Save `ecx` and `edx` before calling kernel functions because the callee may clobber them.
- End every string passed to `print` with a null byte.
- After `call`, add the number of argument bytes back to `esp` with `add esp, N`.
- Labels may be referenced before their definitions. CupidASM patches these forward references after parsing.
- Mnemonics and register names are case-insensitive, so `MOV EAX, 1` is valid.

## Current checked proof

The earlier 2026-08-14 integration guarded both production ELF32 assembly objects with
private validation and CupidDis inspection. Raw source now rejects duplicate
origins and section switches with stable diagnostics, and every active demo
assembles with implicit externs disabled. The normal image build passed in
625.8 seconds with the host code-generation commands poisoned. A private
four-vCPU guest assembled and ran `/demos/hello.asm` through CupidASM as part
of a 60.5-second parallel smoke pair.

The preceding poisoned-host `make -j4 all` checkpoint passed in 684.260
seconds with all fourteen exact policy artifacts accepted. CupidASM produced
the 2,560-byte `boot/boot.bin`, SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`,
and the 4,096-byte `kernel/smp_trampoline.bin`, SHA-256
`b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90`.
A private four-vCPU `max`/e1000 smoke of the resulting image passed in 64.601
seconds, reached the full JIT completion marker, and found no reject marker.
The source image was unchanged. Its 33,219-byte log has SHA-256
`e39a1905002c2baa483c65eb6e763f4f62907c22f8954873dbb20f4ba5a53e93`.

The earlier v1 Linux CupidASM image was 462,600 bytes with SHA-256
`a6c2f07e722fb4b5152326773a240722d1065785c1110d65c593445b0e88dc80`.
Its 5,573-byte seed manifest had SHA-256
`b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`.
The earlier v1 Windows CupidASM image was 444,928 bytes with SHA-256
`5c21d79b1822831e5d81359fa2b31d85b731ead5a88c6596ced38585e64b87cb`.
Its 2,118-byte manifest had SHA-256
`751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef`.
Both bound revision `a17c9465911da41d59b7ada71733d36c39faa5ea` and exact
50-input snapshot
`46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67`.
The Windows manifest named the Linux manifest as its parent. The 2026-08-14
build and smoke evidence above predates this promotion; the later poisoned
build and e1000 smoke followed it. The pre-documentation artifact gate then
passed in 651.3 seconds and accepted all fourteen exact paths.

The integrated fully poisoned build first reached the exact-size gate with
three rebuilt kernel outputs. The artifact group passed all 46 tests in 4.160
seconds, with four expected Windows skips. After the pass-one ELF, final ELF,
and raw kernel policy rows were updated, the repeated build passed in 874.531
seconds with all fourteen artifacts accepted, existing FAT contents preserved,
and `hello.iso` staged. CupidASM's outputs
remained the 2,560-byte boot image with SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`
and the 4,096-byte SMP trampoline with SHA-256
`b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90`.

The integrated strong full private frontier smoke passed in 883.513 seconds
with e1000, four `max` vCPUs, SMP and frontier checks, and the private USB
fixture. The 640-by-480 framebuffer changed 89,630 pixels. AC97 produced
36,877,878 stereo 44.1 kHz frames with a peak of 25,600, and the PC speaker
produced 76,251 stereo 44.1 kHz frames with a peak of 29,912. The expected
direct-call, named-callback, typedef-callback, global-callback,
automatic-callback, and overall feature14 PASS markers each appeared once and
in order. The feature run then printed a clean JIT completion. The 161,418-byte
log has SHA-256
`bc30f5083b96a36362bec5975c0a88437c4f23515de329328bb03d8f6c3e9326`.
The source image was unchanged at SHA-256
`31b25b6881419b1bb8a04b2b3765323b21c5706ac114af1a07b514dcdcd07ea3`.
ADR 0318 records the seed identities.

## Active six-tool seed

The active Linux and Windows v2 manifests carry CupidASM as a producer and
CupidBuild as both a checked tool and the coordinator for two normal object
publications. Both list six images and bind revision
`43c747f0e683d0527984bae05bf944879e64a07b`, the 58-input snapshot
`4cd9d583933d8a9f1dbfb63425bc3665fe6c306db8ae76606f40a0ade49afe70`,
and their exact build plans.

The Linux plan has SHA-256
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`.
Its 6,602-byte manifest has SHA-256
`78d26d7ce3aa0393c8c27a33f2b1f2fad6fe5f6f6300267bf674b36ce51a4dd8`.
The Windows native plan has SHA-256
`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`.
Its 2,852-byte manifest has SHA-256
`019d6ddd54e183752bd6c579215d4c56bf91dbbef9db9cc0854cdce5f4017288`
and pairs to the exact Linux manifest bytes.

Linux and native Windows candidate proof passed. Linux covers 24 failure, six
help, and 31 success groups; Windows covers 13 failure, six help, and 18
success groups. Promoted-seed self-consumption also passed on both platforms,
with all six initial tool images equal to stage two. ADR 0353 records the v2
promotion, and ADR 0356 records the active refresh. The checked CupidBuild
images carry guarded raw commands; their normal Make recipe transfer and
Python-free coordination remain.
