# Cupid OS

Cupid OS is a standalone x86 operating system with a graphical desktop,
shells, persistent filesystems, networking, and its own development tools.
The current kernel and executable format target 32-bit i386.

The normal build uses checked CupidC, CupidASM, CupidLD, CupidObj, CupidDis,
and CupidBuild seeds. Hosted toolchain fixed points have been verified on
Linux and Windows. Building and maintaining the complete system inside
Cupid OS remains unfinished. See the [bootstrap records](docs/bootstrap/README.md)
for current evidence and limitations.

## Build and run

Install Python 3, GNU Make, and QEMU (`qemu-system-i386`). Windows also uses
WSL for the remaining Linux contract paths. The
[getting started guide](wiki/Getting-Started.md) covers platform setup,
optional compiler oracles, disk images, and troubleshooting.

```sh
make -j2 all
make run
```

The build produces `cupidos.img`. Press `Ctrl+Alt+T` in the desktop to open
a terminal. See [shell commands](wiki/Shell-Commands.md) and
[user programs](wiki/User-Programs.md) for what to run.

## Documentation

- [User and developer wiki](wiki/Home.md)
- [Architecture](wiki/Architecture.md)
- [CupidC language reference](wiki/CupidC-Language-Reference.md)
- [CupidASM assembler](wiki/CupidASM-Assembler.md)
- [Bootstrap capabilities and validation](docs/bootstrap/README.md)
- [Active build ownership audit](docs/bootstrap/ACTIVE-SOURCE-AUDIT.md)
- [Architectural decisions](docs/adr/)
- [Detailed reference and historical checkpoints](docs/README-reference.md)

## License

[GNU General Public License v3.0](LICENSE).
Built in dedication to Terry A. Davis and TempleOS.
