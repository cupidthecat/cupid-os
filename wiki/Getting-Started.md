# Getting Started

This guide covers building Cupid OS from source and running it in QEMU.

---

## Requirements

| Tool | Purpose |
|------|---------|
| **Checked Cupid toolchain** | The repository carries a verified static i386 Linux seed and a checked native PE32 Windows execution seed. CupidC, CupidASM, CupidObj, CupidLD, and CupidDis own the normal C, assembly, object, link, and inspection transforms |
| **Python 3** | Portable host-side image and code-generation helpers |
| **GNU Make** | Build system |
| **QEMU** (`qemu-system-i386`) | x86 emulator for testing |
| **WSL** (Windows only) | Runs the checked Linux seed for Linux fixed-point work, Toolchain contracts, the user ABI contract, and artifact-size policy; native Windows reconstruction does not use WSL |
| **mtools** (optional) | Manual FAT16 inspection/copying from Linux hosts |
| **GCC, Clang, and binutils** (optional) | Native development builds and comparison oracles; they do not produce normal OS artifacts |
| **NASM** (optional) | Comparison oracle used by `make nasm-assembly-oracle` when installed |

---

## Install Dependencies

### Ubuntu / Debian
```bash
sudo apt-get install python3 make qemu-system-x86
```

Install native compiler and comparison tools only when you plan to run the
optional oracle targets:

```bash
sudo apt-get install gcc gcc-multilib binutils nasm
```

### Arch Linux
```bash
sudo pacman -S python make qemu-full
```

The optional native oracles also use GCC, binutils, or NASM:

```bash
sudo pacman -S gcc binutils nasm
```

### Native Windows
Install GNU Make, Python 3, and QEMU, make sure they are on `PATH`, then
enable WSL with a Linux distribution.

```powershell
choco install make python qemu
wsl --install
```

Run `make` from PowerShell or another native Windows shell. Output-bearing
recipes run the checked PE32 Cupid tools directly. The Makefile still uses WSL
for Toolchain contracts, the user ABI contract, and artifact-size policy. The
native `bootstrap-windows` command pairs the PE execution seed with the
verified Linux plan and builds through stage four without WSL. An uncapped
stage-three to stage-four proof passes, but it began from uncommitted source.
Named clean-commit reproof and seed promotion remain pending, so the checked
execution seed is not yet a promoted native fixed point. Run
`make verify-windows-bootstrap-seed`, then `make bootstrap-windows-from-seed`; a
successful proof publishes under `build/bootstrap/checked-windows-seed`.
Install LLVM only for the explicit native
Toolchain contracts and native Windows comparison targets:

```powershell
choco install llvm
```

QEMU defaults to no host audio on Windows so booting does not depend on a
working DirectSound device; use `make QEMU_AUDIODEV=dsound,id=speaker run` to
enable DirectSound.

### WSL (Windows Subsystem for Linux)
When you build entirely inside WSL, install the Ubuntu or Debian requirements
in that distribution. Native Windows builds normally start QEMU outside WSL,
so they do not need an X server. Running QEMU inside WSL requires WSLg or an X
server such as VcXsrv.

---

## Building

```bash
git clone https://github.com/your-username/cupid-os.git
cd cupid-os
make
```

This produces:

- `boot/boot.bin`: 2,560-byte image with a 512-byte stage 1 and 2 KiB stage 2
- `kernel/kernel.bin`: flat binary kernel
- `cupidos.img`: bootable IDE HDD image, 200 MB by default, with an embedded FAT16 partition

### Choose HDD Size

```bash
make HDD_MB=100
make HDD_MB=200
```

Default is `HDD_MB=200`.

### Build Targets

| Target | Description |
|--------|-------------|
| `make` | Build the OS image |
| `make clean` | Remove build artifacts (keeps `cupidos.img`) |
| `make clean-image` | Remove only `cupidos.img` |
| `make distclean` | Remove build artifacts + `cupidos.img` |
| `make run` | Build and run in QEMU (serial to stdout) |
| `make run-log` | Build and run with serial output saved to `debug.log` |
| `make run-headless` | Build and boot a serial-only shell for scripts/tests |
| `make run-usb` | Boot with UHCI/EHCI and a FAT16 USB mass-storage image |
| `make run-net` | Boot with RTL8139 networking and host port 8080 forwarded to guest port 80 |
| `make run-ssh` | Boot with RTL8139 networking and host port 2222 forwarded to guest port 22 |
| `make test-net` | Run the headless networking integration harness |
| `make stage-wads` | Copy Freedoom WAD files into `/disk/wads/` |
| `make sync-demos` | Stage local `demos/*.asm` into `/home/demos` in `cupidos.img` |

---

## Running in QEMU

### Basic Boot
```bash
make run
```
Boots into the graphical desktop from HDD (`-boot c -hda cupidos.img`).
Serial output appears in your terminal.

### Serial Logging
```bash
make run-log
```
All serial output (debug logs, kernel messages) saved to `debug.log`.

### Networking and SSH Server

```bash
make run-ssh
```

Inside CupidOS, run `sshd`. From the host:

```bash
ssh -p 2222 root@127.0.0.1
```

The default login is `root` / `cupid`; change it in the guest with
`sshd passwd <new-password>`.

---

## HDD Image Notes

`make` creates one HDD image, `cupidos.img`, containing:

- MBR boot sector + Stage 2 + kernel area
- FAT16 partition mounted as `/disk`
- persistent `/home` data stored in `HOMEFS.SYS` on FAT16

By default, FAT starts at LBA 20480 (offset `10485760` bytes).

The build does not use a separate `test-disk.img`.

---

## Copy Host Files Into the Disk Image

Use the portable host helper against the FAT partition inside `cupidos.img`:

```bash
# Host file -> OS /disk/cupid.bmp
python3 tools/hostbuild.py stage --image cupidos.img --fat-start-lba 20480 cupid.bmp:/cupid.bmp
```

On Windows, use `python` instead of `python3`.

If you prefer `mtools`, use the FAT byte offset:

```bash
mcopy -o -i cupidos.img@@10485760 cupid.bmp ::/cupid.bmp

# Verify the FAT root (visible in CupidOS as /disk)
mdir -i cupidos.img@@10485760 ::/
```

If `FAT_START_LBA` changes, recompute offset:
`offset_bytes = FAT_START_LBA * 512`.

---

## First Boot

When cupid-os boots, you'll see:

1. **Bootloader** - Loads kernel from HDD LBA sectors to 0x100000, switches to protected mode
2. **Kernel init** - IDT, PIC, PIT, keyboard, memory, paging, serial
3. **Desktop** - VBE 640x480 32bpp graphical desktop with pastel theme

### Live Docs

CupidOS includes a TempleOS-inspired set of DolDoc-like manuals in the OS.

Open **Notepad**, browse to `/docs/00INDEX.ctxt`, and press `F2` to switch
between raw source and rendered view.

In rendered view, `.ctxt` manuals can contain:
- runnable CupidC code blocks
- clickable `open:`, `shell:`, and `repl:` links
- buttons, tree widgets, and inline BMP sprites

The shipped manuals also embed `/docs/image.bmp`, which is used by the DolDoc
examples and widget demos.

### Exploring the Shell

Click the **Terminal** icon on the desktop (or use `make run-headless`). You'll see:

```
cupid-os shell
> help
```

Try these commands to get started:
```
help              # List all commands
sysinfo           # System information
ls                # List files in the current directory
ls /docs          # List embedded manuals
ls /disk          # List the FAT16 disk mount
ls /home          # List persistent homefs
ed hello.cup      # Create a script with the editor
cupid hello.cup   # Run your script
browser http://example.com/
ssh user@host
telnet telehack.com
```

---

## Next Steps

- **[Shell Commands](Shell-Commands)** - Full command reference
- **[CupidScript](CupidScript)** - Write and run scripts
- **[Ed Editor](Ed-Editor)** - Create and edit files
- **[Disk Setup](Disk-Setup)** - Work with the FAT16 partition inside `cupidos.img`
- **[Desktop Environment](Desktop-Environment)** - Using the GUI
