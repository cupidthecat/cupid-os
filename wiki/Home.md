# Cupid OS wiki

Cupid OS is a 32-bit x86 operating system with a desktop, shell and live
Cupid C and Cupid ASM development. Programs share the kernel address space
and run in ring 0.

## Start here

- [Getting Started](Getting-Started): dependencies, building and running in QEMU.
- [Real Hardware](Real-Hardware): BIOS/CSM boot and installation on physical disks.
- [Architecture](Architecture): boot sequence, memory, processes and the toolchain.

On Ubuntu or Debian:

```sh
sudo apt-get install python3 make qemu-system-x86
make
make run
```

Windows uses checked native Cupid tools with GNU Make and Python. WSL runs
the Linux Toolchain contract cohort. See Getting Started for the complete
Windows setup and optional comparison tools.

## Develop programs

| Guide | Use it for |
| --- | --- |
| [CupidC Compiler](CupidC-Compiler) | JIT/AOT commands and kernel bindings |
| [Cupid C language reference](CupidC-Language-Reference) | Language features and examples |
| [CupidASM Assembler](CupidASM-Assembler) | Assembly, includes and runtime demos |
| [CupidBuild](CupidBuild) | Checked build commands and publication |
| [Toolchain Bootstrap](Toolchain-Bootstrap) | Seeds, staged rebuilds and verification |
| [User Programs](User-Programs) | Source programs and deployment |
| [ELF Programs](ELF-Programs) | External executables and the syscall ABI |

## Use the system

- [Shell Commands](Shell-Commands), [CupidScript](CupidScript) and [Ed](Ed-Editor).
- [Desktop Environment](Desktop-Environment) and [CupidDoc CTXT](CupidDoc-CTXT).
- [2D graphics](CupidC-2D-Graphics-Library) and [floating point](Floating-Point).
- [Filesystems](Filesystem), [disk setup](Disk-Setup), [ISO9660](ISO9660) and [USB](USB).
- [Networking](Networking), [SMP](SMP), [processes](Process-Management) and [debugging](Debugging).

[Earlier bootstrap notes](Bootstrap-history) retain the preceding measurements
and verification history. Cupid OS is released under GPLv3. TempleOS is
reference material and is excluded from Cupid OS builds.
