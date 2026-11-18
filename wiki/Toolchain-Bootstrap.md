# Toolchain bootstrap

## Current production checkpoint

The normal OS and user builds use checked CupidC, CupidASM, CupidLD, CupidObj,
CupidDis and CupidBuild seeds. GNU Make schedules the build. Python still
coordinates final disk images and parts of toolchain verification. See
[CupidBuild](CupidBuild) for normal build commands and
[Getting Started](Getting-Started) for host setup.

Hosted toolchain rebuilds have reached a fixed point on Linux and Windows.
A fixed point means two consecutive stages produce identical checked objects
and tools. The [bootstrap records](https://github.com/cupidthecat/cupid-os/blob/bootstrap/cupid-self-hosting/docs/bootstrap/README.md)
list the remaining seed promotion, image publication and guest work.

## Rebuild from checked seeds

Run from the repository root.

| Check | Linux or WSL | Native Windows |
| --- | --- | --- |
| Verify the checked seed | `make verify-bootstrap-seed` | `make verify-windows-bootstrap-seed` |
| Rebuild the hosted toolchain | `make bootstrap-from-seed` | `make bootstrap-windows-from-seed` |
| Check the active build graph | `make check-bootstrap-audit` | `make check-bootstrap-audit` |

The [ownership audit](https://github.com/cupidthecat/cupid-os/blob/bootstrap/cupid-self-hosting/docs/bootstrap/ACTIVE-SOURCE-AUDIT.md)
shows which tool produces each part of the current build.

## Freestanding compiler engine

`make -C bootstrap/guest` builds an i386 ELF with the full CupidC compiler
pipeline and no host runtime. On Linux or WSL, the command below loads that
ELF on a two-MiB stack and has it rebuild its nine source files. The check
compares each object and the relinked ELF byte for byte.

```sh
make -j4 -C bootstrap/guest check-linux
```

`make -C bootstrap/guest check-load` runs the ELF checks without replaying
the compiler. `make -C bootstrap/guest source-iso` packages the engine and
its source tree as a read-only ISO that Cupid OS can mount.

`make -C bootstrap/guest guest-compiler` builds the standalone CupidC ELF
with a Cupid OS syscall adapter. On Linux or WSL,
`make -C bootstrap/guest check-guest-compiler` compares the emitted object
and checks rejected requests.

Cupid OS loads the worker into a dedicated arena with a two-MiB stack.
Mount the source ISO at `/src`, place the two-line request in
`/home/cupidc.job` and run `exec /disk/cupcc` after staging the executable
as `/cupcc` on the guest disk. The first QEMU run rebuilt `ctool.o`
byte for byte and released the worker lease.
[Engine build and evidence](https://github.com/cupidthecat/cupid-os/blob/bootstrap/cupid-self-hosting/docs/bootstrap/FREESTANDING-COMPILER-ENGINE.md)

## Guest linker

`make -C bootstrap/guest guest-linker` builds the CupidLD worker. Set up
`/home/cupidld.job` with the output path, text base, entry symbol and ordered
object paths, one per line. Stage the ELF as `/cupld` and run
`exec /disk/cupld`. The source ISO includes the job format in
`/src/bootstrap/guest/TOOLS.CTXT`.

The linker supports the fixed addresses for ordinary executables and compiler
workers. A four-vCPU run linked and executed `hello.elf`. The saved ELF
matched the checked host link. The Linux check exercises both base addresses
and rejected requests.

## History

The [bootstrap log](https://github.com/cupidthecat/cupid-os/blob/bootstrap/cupid-self-hosting/docs/bootstrap/LOG.md)
holds implementation notes and test results. Earlier wiki checkpoints remain
available in the [archived bootstrap page](https://github.com/cupidthecat/cupid-os/blob/c3943915af088cf17dd8d8cef43088d1f9984db4/wiki/Toolchain-Bootstrap.md).
