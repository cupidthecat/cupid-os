# Toolchain ownership migration matrix

`TempleOS/` is excluded: it is reference material, not a source cohort. Statuses describe ownership, not how much code exists.

| Source or artifact cohort | Current owner/path | Fixed-point owner/path | Status and next proof |
| --- | --- | --- | --- |
| `boot/boot.asm` | NASM flat binary | CupidASM flat binary | Host-owned; inventory directives, layout constraints, and byte/boot parity |
| `kernel/cpu/isr.asm` | NASM ELF32 object | CupidASM ELF32 `ET_REL` | Host-owned; requires shared object/relocation support and encoding parity |
| `kernel/core/context_switch.asm` | NASM ELF32 object | CupidASM ELF32 `ET_REL` | Host-owned; requires ABI-sensitive encoding parity and boot/runtime smoke |
| `kernel/smp/smp_trampoline.S` | NASM flat binary despite `.S` suffix | CupidASM flat binary | Host-owned; requires 16/32-bit mode/layout parity and SMP smoke |
| `demos/*.asm` | Host `objcopy` embeds source; in-OS CupidASM assembles on demand | CupidObj embeds source; CupidASM remains the language owner and can also be host-run | Partly Cupid-owned at runtime; active feature coverage and deterministic outputs need audit |
| Core kernel, drivers, filesystems, graphics, GUI, networking, crypto, TLS, audio, and utilities in `.c` | GCC/Clang freestanding compilation | CupidC C mode to ELF32 `ET_REL` | Host-owned; exact build graph and C/ABI feature inventory pending |
| `kernel/lang/cupidc*.c` | GCC/Clang, linked into kernel | CupidC builds host and in-OS CupidC variants | Host-owned; first requires deep shared core, host runtime seam, object writer, then staged self-build |
| `kernel/lang/as*.c` | GCC/Clang, linked into kernel | CupidC builds host and in-OS CupidASM variants | Host-owned; requires shared runtime/object/instruction seams |
| `kernel/lang/dis.c` | GCC/Clang, linked into kernel | CupidC builds host and in-OS CupidDis variants | Host-owned; requires object-inspector feature completion and shared instruction model |
| Doom port support and vendored `kernel/doom/src/*.c` | GCC/Clang with relaxed vendored flags and repository compatibility headers | CupidC C mode with equivalent source compatibility and optimization | Host-owned; migrate without pruning or weakening vendored behavior |
| `bin/*.cc` and browser `.cc` sources | Host `objcopy` embeds source; in-OS CupidC compiles on demand | CupidObj embeds source; CupidC remains the language owner and can also be host-run | Partly Cupid-owned at runtime; source feature audit and runtime behavior gates pending |
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
| Baseline | Clean, reproducible oracle build and recorded artifact/tool hashes on Windows and Linux | In progress; initial Windows build/smokes recorded, clean dual-host capture pending |
| Capability audit | Every active source and generated input mapped to required C, ASM, ABI, object, linker, and inspector features | Not started |
| Assembly migration | All four host-assembled OS sources produced by CupidASM with equivalent bytes/behavior | Not started |
| C migration | Every reachable kernel, tool, application, Doom, and vendored C cohort compiles and passes behavior gates with CupidC | Not started |
| Toolchain self-hosting | Checked seeds rebuild host tools; stage 2 and stage 3 outputs are byte-identical on Windows and Linux | Not started |
| Normal-build cutover | Make invokes CupidC, CupidASM, CupidLD, CupidObj, and CupidDis without GCC/Clang/NASM/LLVM/binutils | Not started |
