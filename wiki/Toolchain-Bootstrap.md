# Toolchain Bootstrap

Cupid OS carries a checked static i386 Linux seed for its five hosted tools:
CupidC, CupidASM, CupidDis, CupidLD, and CupidObj. The seed starts a complete
toolchain rebuild without GCC, Clang, NASM, a host linker, `nm`, or `objcopy`.

The seed lives under `bootstrap/seeds/i386-linux/`. Its manifest records each
file's size and SHA-256 value, the static i386 Linux ABI, entry point, source
revision, producer lineage, all 19 C sources, startup, include arguments, and
the exact link order for every tool. Verification pins every source name,
path, position, and GNU-mode flag rather than trusting a manifest-supplied
digest alone. It also rejects unknown fields, dynamic images, an interpreter,
duplicate JSON keys, numeric and Boolean type substitutions, writable
executable load segments, entry points outside executable file bytes, unlisted
ELF files, and unexpected target metadata.

```sh
make verify-bootstrap-seed
```

This command validates the seed without executing it.

```sh
make bootstrap-from-seed
```

The full command captures the hashes of all 40 current source inputs: 19 C
sources, startup, 19 project headers, and `link.ld`. Checked CupidC compiles
stage two, checked CupidASM assembles its startup, and checked CupidLD links all
five tools. The stage-two producer trio repeats the build for stage three.

The gate compares all 19 C objects, both startup objects, and all five linked
images. It also runs five help checks, ten successful operations, and six
failure cases across compilation, assembly, disassembly, symbol inspection,
linking, wrapping, and flattening. A source edit during either stage stops the
build instead of publishing mixed evidence.

Before execution, the harness reads the manifest and each seed binary once. It
verifies those captured bytes, keeps the manifest hash, and runs private copies
of the five binaries. A later replacement of a checked-in file cannot change
that run.

The default output is `build/bootstrap/checked-seed/`. It contains both stages,
the behavior fixtures, and `bootstrap-report.json`. The report keeps the
historical seed source revision separate from the current source snapshot.

Linux runs private copies of the static tools directly. Windows stages each
copy in a mode-0700 WSL directory created by `mktemp`. Native Windows seed
executables are not available yet.

This seed makes the hosted static toolchain reproducible from a clean checkout.
It does not complete the normal OS build migration. Native contract runners,
hosted development commands, and the remaining normal C objects still use a
host C compiler.

The production boot source assembles to an exact 2,560-byte image with SHA-256
`9545d6a2f44404af85bb3fd568f1b2d7215b7cd1af2933f7ae5a877353dc95fc`.
CupidASM and the optional NASM oracle produce the same bytes for the current
`0x00F00000` boot-stack layout.

The checked seed includes the active CSPRNG assembly, operand-free
function assembly, per-CPU pointer output, integer atomics through fetch-or, and
width-aware port I/O. Its stage-three CupidC image is 2,000,636 bytes with
SHA-256
`2224337832dda113f27c70fb944188b48c0660324a652725feb83976461bc0ac`.
It came from stage three of the checked bootstrap at revision
`d2e0f8b876d96b9268666e16c26a9e16ab5249af`, not from the native compiler
candidate. With host compiler and linker commands poisoned, all five seed
images match stage two. All 19 stage-two C objects, startup, and five images
then match stage three, and both stages pass all 21 tool behavior cases.

The refreshed seed represents operand-free GNU assembly statements inside
functions and emits exact PAUSE, NOP, STI, HLT, CLI, CLD, SFENCE, and FNINIT
sequences. The normal build uses that path for e1000, the desktop shell,
the socket layer, and TCP. The earlier detached hybrid linked the same four
objects through both CupidLD passes and booted them under QEMU before the
ownership hand-off.

Compiler head also handles the exact per-CPU pointer output
`mov %%gs:0, %0` with one four-byte `=r` object or `void` pointer. The frontend
and IR preserve its pointer type and evaluate the destination once. The x86
model emits `65 A1 00 00 00 00`.

The checked compiler's atomic slice handles integer load, store, exchange,
and fetch-add builtins with constant orders. Its i386 path selects ordinary
loads and release stores, memory `XCHG`, and `LOCK XADD`. That brings
unchanged `acpi.c` and `mp_tables.c` through deterministic i386 ELF32 object
emission. The normal Make graph owns both through the checked seed. A
four-vCPU image boots every discovered CPU and completes the normal e1000,
desktop, terminal, and CupidC runtime smoke.

Compiler head adds `__atomic_fetch_or` at the same one-, two-, and four-byte
integer widths. It emits a `LOCK CMPXCHG` retry loop because `LOCK OR` cannot
return the old value. Exact byte and execution checks cover a competing
update, signed narrow results, guard bytes, one-time operand evaluation, and
callee-saved EBX. The checked stage-three seed carries this operation and
compiles the active EHCI path.

Compiler head and the checked seed parse all eight helpers in unchanged
`kernel/core/ports.h`. It retains the 8-, 16-, and 32-bit accumulator lanes,
the 16-bit DX port, the read/write ESI or EDI buffer, the read/write ECX
count, and the INSW memory clobber. Scalar port I/O and the CLD plus REP word
forms emit through the shared x86 model. This brings the active non-Doom
header gate to 154/154 at compiler head.

The normal image has a 136-source checked CupidC production boundary. It
keeps the established 116-source cohort and adds 20 source-driven roots now
named `.cc`. The strict frontier compiles each approved source twice and
accepts 3,020,108 byte-identical i386 ELF32 bytes. It freezes 424 inputs with
SHA-256
`24fcfba4f006dad77a742e02b31edd889d3a62010adb352d6f57965377557cd1`.
Forced Make runs with the host compiler command poisoned cover every
production wrapper recipe. Each recipe lists its exact recursive header
closure. A valid data-only object can omit `.text` when its other sections
and symbols pass validation.

The checked seed also compiles three generated installation tables and the
three example external ELF programs. All six use `.cc` source names. The
generated tables keep the kernel profile, while `hello.cc`, `ls.cc`, and
`cat.cc` use the closed user profile and checked CupidLD link. Both wrappers
freeze their source and control inputs, validate every ELF result, and replace
an artifact only after the operation succeeds. Deterministic frontiers and
poisoned-host builds protect both paths.

The external-program runtime gate boots the checked hello, ls, and cat
executables separately through the ordinary loader. Serial events carry the
running PID. Print events record a byte count and FNV-1a fingerprint instead
of caller text, which keeps newline and marker-shaped file contents inside
one event. The checks cover hello's numeric writes, ls reading the shell root,
cat reading a fixed FAT fixture, and a PID-matched exit from each program.
Kernel and JIT printing remain on their existing path.

The refreshed checked seed emits weak symbols and arbitrary compatible named sections,
records `unused` declarations, preserves typed static null pointers, treats
known-true loops as non-fallthrough, and lowers comma expressions in source
order. It also keeps all target bits through represented function-pointer
casts and supports bounded output-only register and EFLAGS snapshots. Twenty
of the 38 strict roots left after the 116-source handoff now belong to the
production cohort and use `.cc` names. Eighteen strict checked-in roots
remain. Generated `kernel/cpu/ksyms_data.c` stays hosted until CupidC
represents its `used` attribute.

CupidDis accepts every one of the 428 active i386 ELF objects, including all
current symbols and relocations. Cupid-built objects, checked tool images, and
user executables have no unsupported instruction fallback. The remaining gap
is concentrated in host-built kernel and Doom objects. Padding NOPs,
packed-integer SSE2, and immediate three-operand `IMUL` account for 4,008 of
the 4,164 missing instruction starts. Those groups set the next decoder work.

The four-vCPU GUI runtime starts every discovered CPU, reaches e1000 traffic,
passes all 62 crypto checks, opens the desktop and terminal, and completes
embedded CupidC execution at `0x01100000`. The established e1000 and RTL8139
gates continue to cover audio, input reattachment, and six EHCI storage
lifetimes. A private-image smoke loads the same external ELF program twice at
`0x00F00000`; cleanup releases the first arena lease before the second load.
The gate rejects SMP, storage, crypto, exception, panic, corruption, and
illegal-instruction failure markers. The X.509 checks exercise parser,
hostname, chain state, and embedded-root lookup paths. They are not a full
trust-validation claim.

The current audit assigns 142 transforms to CupidC, 155 C transforms to the
host compiler, and 154 transforms to host Python. The host compiler still
produces 103 root objects.
