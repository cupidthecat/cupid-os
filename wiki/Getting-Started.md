# Getting Started

This guide walks you through building cupid-os from source and running it in QEMU.

---

## Requirements

| Tool | Purpose |
|------|---------|
| **NASM** | Assembler for bootloader and context switch |
| **GCC** (32-bit support) | C compiler for kernel and drivers |
| **GNU Make** | Build system |
| **QEMU** (`qemu-system-i386`) | x86 emulator for testing |
| **mtools** | Copying files into the FAT16 partition inside `cupidos.img` |
| **Linux environment** | Ubuntu, Debian, WSL, or equivalent |

---

## Install Dependencies

### Ubuntu / Debian
```bash
sudo apt-get install nasm gcc gcc-multilib make qemu-system-x86 mtools
```

### Arch Linux
```bash
sudo pacman -S nasm gcc make qemu-full mtools
```

### WSL (Windows Subsystem for Linux)
Same as Ubuntu/Debian. For display, ensure an X server (VcXsrv, WSLg) is available for QEMU's graphical output.

---

## Building

```bash
git clone https://github.com/your-username/cupid-os.git
cd cupid-os
make
```

This produces:
- `boot/boot.bin` - 512-byte bootloader
- `kernel/kernel.bin` - Flat binary kernel
- `cupidos.img` - Bootable IDE HDD image (default 200MB) with embedded FAT16 partition

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
| `make sync-demos` | Copy local `demos/*.asm` into `/home/demos` in `cupidos.img` |

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

`make` now creates a single HDD image (`cupidos.img`) that already contains:
- MBR boot sector + Stage 2 + kernel area
- FAT16 partition mounted as `/home`

By default, FAT starts at LBA 4096 (offset `2097152` bytes).

You no longer need a separate `test-disk.img`.

---

## Copy Host Files Into `/home`

Use `mtools` against the FAT partition inside `cupidos.img`:

```bash
# Host file -> OS /home/cupid.bmp
mcopy -o -i cupidos.img@@2097152 cupid.bmp ::/cupid.bmp

# Verify (FAT root == OS /home)
mdir -i cupidos.img@@2097152 ::/
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

CupidOS ships a TempleOS-inspired DolDoc-like manual set inside the OS.

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
