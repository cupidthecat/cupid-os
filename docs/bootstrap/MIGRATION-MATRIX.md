# Toolchain ownership migration matrix

`TempleOS/` is excluded: it is reference material, not a source cohort. Statuses describe ownership, not how much code exists.

| Source or artifact cohort | Current owner/path | Fixed-point owner/path | Status and next proof |
| --- | --- | --- | --- |
| `boot/boot.asm` | NASM flat binary | CupidASM flat binary | Host-owned; inventory directives, layout constraints, and byte/boot parity |
| `kernel/cpu/isr.asm` | NASM ELF32 object | CupidASM ELF32 `ET_REL` | Host-owned; requires shared object/relocation support and encoding parity |
| `kernel/core/context_switch.asm` | NASM ELF32 object | CupidASM ELF32 `ET_REL` | Host-owned; requires ABI-sensitive encoding parity and boot/runtime smoke |
| `kernel/smp/smp_trampoline.S` | NASM flat binary despite `.S` suffix | CupidASM flat binary | Host-owned; requires 16/32-bit mode/layout parity and SMP smoke |
| 22 `demos/*.asm` inputs | Host `objcopy` embeds source; in-OS CupidASM assembles on demand | CupidObj embeds source; CupidASM remains the language owner and can also be host-run | Partly Cupid-owned at runtime; all are reachable and form the checked directive/instruction regression corpus |
| Shared `toolchain/ctool*` core, hosted adapter/contract, and `kernel/lang/ctool_kernel*` adapter | Host C compiler builds native contract objects and freestanding kernel objects from one core | CupidC builds the shared core and both adapters; checked seeds build the hosted contract | Interface established and tested; language/object frontends still need to consume it before ownership transfers |
| 153 checked-in core/driver/tool C files plus four generated C files | GCC/Clang freestanding compilation | CupidC C mode to ELF32 `ET_REL` | Host-owned; migrate strict foundational cohorts after the shared host/object/ABI seams |
| `kernel/lang/cupidc*.c` | GCC/Clang, linked into kernel | CupidC builds host and in-OS CupidC variants | Host-owned; first requires deep shared core, host runtime seam, object writer, then staged self-build |
| `kernel/lang/as*.c` | GCC/Clang, linked into kernel | CupidC builds host and in-OS CupidASM variants | Host-owned; requires shared runtime/object/instruction seams |
| `kernel/lang/dis.c` | GCC/Clang, linked into kernel | CupidC builds host and in-OS CupidDis variants | Host-owned; requires object-inspector feature completion and shared instruction model |
| 83 Doom port/vendored C files | GCC/Clang with relaxed vendored flags and repository compatibility headers | CupidC C mode with equivalent source compatibility and optimization | Host-owned; migrate after strict C semantics, preserving legacy declaration/callback behavior without pruning |
| 104 `bin/*.cc` roots and 22 browser `.cc` fragments | Host `objcopy` embeds source; in-OS CupidC compiles on demand | CupidObj embeds source; CupidC remains the language owner and can also be host-run | Partly Cupid-owned at runtime; production scale and extension fixtures are now inventoried |
| Three `user/examples/*.c` programs plus `user/cupid.h` | Separate `user/Makefile` hard-codes GCC/GNU `ld`; outputs are not staged by root `all` | CupidC/CupidLD build and a deliberate image-staging path | Host-owned separate root; make it native on Windows/Linux and preserve the syscall-table ABI |
| Generated C tables (`kernel/util/*_programs_gen.c`, CSS tables, kernel symbol data) | Python generates; GCC/Clang compiles | Python may generate; CupidC compiles | Generation can remain orchestration; generated source must be valid input to CupidC C mode |
| Kernel ELF layout and symbol resolution | GNU `ld`/`ld.lld` with `link.ld`, two passes | CupidLD with the used linker-script subset | Host-owned; deterministic object/link contract required first |
| Kernel symbol extraction | GNU/LLVM `nm`, Python, then host C compiler | CupidDis/CupidObj inspection, Python orchestration if still useful, then CupidC | Host-owned; preserve backtrace behavior and address stability |
| Source, documentation, font, image, and other binary wrapping | GNU/LLVM `objcopy`, with Python preprocessing for JPEG | CupidObj/shared object library | Host-owned; reproduce symbol naming, alignment, section flags, and bytes |
| Linked kernel ELF to raw kernel binary | GNU/LLVM `objcopy` | CupidObj | Host-owned; compare exact ranges and boot behavior |
| Disk/FAT image construction and fixture staging | Python `tools/hostbuild.py` | Python orchestration using Cupid-produced inputs | Accepted non-code-producing host dependency; keep cross-platform and deterministic where practical |
| Emulator verification | QEMU plus Python test harnesses | Same, augmented with staged bootstrap and tool parity tests | Retained test dependency; stabilize the observed GUI-terminal flake |

## Milestone gates

| Milestone | Ownership gate | Current state |
| --- | --- | --- |
| Baseline | Clean, reproducible oracle build and recorded artifact/tool hashes on Windows and Linux | Windows PASS at `7a8cf7a`: two clean 427-artifact builds matched and covered all 420 linked objects; Linux capture pending |
| Capability audit | Every active source and generated input mapped to required C, ASM, ABI, object, linker, and inspector features | Complete for root `all`, `user:all`, and `toolchain:all`: 649 active inputs, 248 feature IDs, 444 transforms, 35 accounted unreachable source-like files, and a checked 429-artifact/422-link-object drift and coverage gate |
| Assembly migration | All four host-assembled OS sources produced by CupidASM with equivalent bytes/behavior | Not started |
| C migration | Every reachable kernel, tool, application, Doom, and vendored C cohort compiles and passes behavior gates with CupidC | Not started |
| Toolchain self-hosting | Checked seeds rebuild host tools; stage 2 and stage 3 outputs are byte-identical on Windows and Linux | Not started |
| Normal-build cutover | Make invokes CupidC, CupidASM, CupidLD, CupidObj, and CupidDis without GCC/Clang/NASM/LLVM/binutils | Not started |
