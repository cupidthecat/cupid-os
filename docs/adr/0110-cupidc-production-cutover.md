# CupidC production cutover for port I/O and USB controllers

- Status: Accepted
- Date: 2026-07-25

## Context

The checked CupidC seed already owned 26 normal-build kernel objects. The
remaining candidates use ordinary active-source requirements supported by
the seed: fixed-register port I/O, read/write string-I/O operands, and
the EHCI pending-port atomic fetch-or operation. The Make graph still sent
these sources through a host C compiler, so compiler capability alone did not
transfer normal-build ownership.

The new cohort contains `drivers/ata.c`, `drivers/keyboard.c`,
`drivers/mouse.c`, `drivers/pci.c`, `drivers/pit.c`, `drivers/rtc.c`,
`drivers/rtl8139.c`, `drivers/speaker.c`, `drivers/vga.c`,
`kernel/audio/ac97.c`, `kernel/core/syscall.c`, `kernel/lang/shell.c`,
`kernel/usb/ehci.c`, and `kernel/usb/uhci.c`. The sources remain unchanged to
fit CupidC. The compiler must carry their requirements.

## Decision

The checked kernel wrapper owns an explicit 40-source allowlist. It keeps the
existing 26 sources and adds the 14 sources named above. Each transferred Make
rule invokes the checked wrapper, names its exact recursive header closure,
and retains its existing kernel link position.

The wrapper verifies and freezes the checked seed before it compiles. It
accepts only an i386 ELF32 relocatable object, validates both deterministic
frontier passes, and replaces the requested target only after the full output
is ready. The frontier captures 328 inputs and has snapshot SHA-256
`3dedac2c0a5733f531871b6bc83ebb427b92e6dfa448edc93a7804ec28025032`.

Every transferred recipe has a poisoned-host test. The test makes a host C
compiler invocation fail, so it detects a Make graph regression that routes a
CupidC-owned object through Clang or GCC. Positive and negative wrapper tests
also cover the allowlist and the exact recursive dependency closures.

## Consequences and evidence

The 40-source frontier produces 675,340 byte-identical i386 ELF32 object
bytes on two independent compiles. The checked wrapper, deterministic
frontier, exact header closures, and poisoned-host recipes provide the current
production ownership evidence. The current audit assigns 40 transforms to
CupidC, 257 C transforms to the host compiler, 49 transforms to host Python,
and 205 root or user objects to host-built C. Its active-source digest is
`21750921084705c65abafda2d0a71bf88a18fd2d0d2683a21dde3a4a43d25275`,
and the JSON SHA-256 is
`bc1c5d8e1d34782d4db918d1d5399c51d42bd562af3d4a6d70ee34f649a241ad`.

Port-I/O users retain their source-declared accumulator widths, DX port width,
read/write pointer and count operands, and the INSW memory clobber. EHCI uses
the represented `__atomic_fetch_or` path, which emits a locked
compare-exchange retry loop and returns the old value without dropping a
competing update.

The enlarged cohort passes the QEMU runtime contract on four vCPUs with both
e1000 and RTL8139. Each run proves SMP discovery and startup, RDRAND and the
62 crypto checks, keyboard and mouse detach and reattach, ATA storage, AC'97
and PC speaker audio, six EHCI storage lifetimes, a zero-padded RTC
timestamp, and DHCP traffic through the selected NIC. Both runs reject the
established SMP, storage, crypto, exception, panic, corruption, and
illegal-instruction failure markers. All 44 USB tests and all 62 GUI gate
tests pass against the same ownership and lifetime contracts.

Most active C sources remain host-built. Native contract executables, hosted
development commands, Python orchestration, the Windows WSL bridge, and all
remaining host-owned root and user C objects are outside this decision. The
private in-kernel CupidC compiler continues to own embedded JIT and AOT work.
