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
| **dosfstools** | Creating FAT16 test disk images |
| **Linux environment** | Ubuntu, Debian, WSL, or equivalent |

---

## Install Dependencies

### Ubuntu / Debian
```bash
sudo apt-get install nasm gcc make qemu-system-x86 dosfstools
```

### Arch Linux
```bash
sudo pacman -S nasm gcc make qemu-full dosfstools
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
- `boot/boot.bin` — 512-byte bootloader
- `kernel/kernel.bin` — Flat binary kernel
- `cupidos.img` — Bootable 1.44MB floppy image

### Build Targets

| Target | Description |
|--------|-------------|
| `make` | Build the OS image |
| `make clean` | Remove all build artifacts |
| `make run` | Build and run in QEMU (serial to stdout) |
| `make run-disk` | Build and run with FAT16 hard disk attached |
| `make run-log` | Build and run with serial output saved to `debug.log` |

---

## Running in QEMU

### Basic (no disk)
```bash
make run
```
Boots into the graphical desktop. Serial output appears in your terminal.

### With FAT16 Disk
```bash
make run-disk
```
Requires a `test-disk.img` file (see below). Enables `lsdisk`, `catdisk`, disk writes, and CupidScript files on disk.

### Serial Logging
```bash
make run-log
```
All serial output (debug logs, kernel messages) saved to `debug.log`.

---

## Creating a FAT16 Test Disk

The disk commands and CupidScript file execution require a FAT16 disk image with an MBR partition table:

```bash
# 1. Create a 10MB blank image
dd if=/dev/zero of=test-disk.img bs=1M count=10

# 2. Create MBR with a FAT16 partition (type 0x06)
echo -e 'o\nn\np\n1\n\n\nt\n6\nw' | fdisk test-disk.img

# 3. Set up loop device for the partition
sudo losetup -o $((2048*512)) --sizelimit $((18432*512)) /dev/loop0 test-disk.img

# 4. Format as FAT16
sudo mkfs.fat -F 16 -n "CUPIDOS" /dev/loop0

# 5. Mount and add files
sudo mkdir -p /tmp/testdisk
sudo mount /dev/loop0 /tmp/testdisk
echo "Hello from CupidOS!" | sudo tee /tmp/testdisk/README.TXT
echo -e '#!/bin/cupid\nNAME=world\necho "Hello $NAME"' | sudo tee /tmp/testdisk/HELLO.CUP

# 6. Clean up
sudo umount /tmp/testdisk
sudo losetup -d /dev/loop0
rmdir /tmp/testdisk
```

---

## First Boot

When cupid-os boots, you'll see:

1. **Bootloader** — Loads kernel at 0x1000, switches to protected mode
2. **Kernel init** — IDT, PIC, PIT, keyboard, memory, paging, serial
3. **Desktop** — VGA Mode 13h graphical desktop with pastel theme

### Exploring the Shell

Click the **Terminal** icon on the desktop (or boot in text mode). You'll see:

```
cupid-os shell
> help
```

Try these commands to get started:
```
help              # List all commands
sysinfo           # System information
ls                # List in-memory files
lsdisk            # List files on FAT16 disk
ed hello.cup      # Create a script with the editor
cupid hello.cup   # Run your script
```

---

## Next Steps

- **[Shell Commands](Shell-Commands)** — Full command reference
- **[CupidScript](CupidScript)** — Write and run scripts
- **[Ed Editor](Ed-Editor)** — Create and edit files
- **[Desktop Environment](Desktop-Environment)** — Using the GUI
