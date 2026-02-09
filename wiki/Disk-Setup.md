# Disk Setup

This guide explains how to create, partition, and format a FAT16 disk image for use with cupid-os in QEMU. A disk image is required for persistent file storage — anything saved to `/home` lives on this disk.

---

## Quick Setup (Copy-Paste)

If you just want a working disk image fast:

```bash
# Create 10MB FAT16 disk image with MBR
dd if=/dev/zero of=test-disk.img bs=1M count=10
echo -e "o\nn\np\n1\n\n\nt\n6\nw" | fdisk test-disk.img

# Format and populate
LOOP=$(sudo losetup --show -fP test-disk.img)
sudo mkfs.fat -F 16 -n "CUPIDOS" ${LOOP}p1
sudo mkdir -p /mnt/cupidos
sudo mount ${LOOP}p1 /mnt/cupidos
echo "Hello from CupidOS!" | sudo tee /mnt/cupidos/HELLO.TXT
sudo umount /mnt/cupidos
sudo losetup -d $LOOP

# Boot with disk attached
make run-disk
```

That's it. Read on for a detailed explanation of each step.

---

## Requirements

- **Linux** (or WSL) with root/sudo access
- **fdisk** — included in `util-linux` (pre-installed on most distros)
- **mkfs.fat** — from `dosfstools`
- **losetup** — from `util-linux`

Install if needed:
```bash
# Ubuntu / Debian / WSL
sudo apt-get install dosfstools util-linux

# Arch
sudo pacman -S dosfstools util-linux
```

---

## Step-by-Step Guide

### 1. Create a Blank Disk Image

```bash
dd if=/dev/zero of=test-disk.img bs=1M count=10
```

This creates a 10 MB file filled with zeros, representing a blank hard drive. You can use a larger size (e.g., `count=32` for 32 MB) but FAT16 requires the partition to be **at most 2 GB** and **at least ~1 MB**.

| Size | Use Case |
|------|----------|
| 10 MB | Simple testing, a few files |
| 32 MB | More files, CupidScript projects |
| 64 MB | Heavy use, many ELF binaries |

### 2. Create an MBR Partition Table

```bash
echo -e "o\nn\np\n1\n\n\nt\n6\nw" | fdisk test-disk.img
```

This runs `fdisk` non-interactively to:

| Command | What it does |
|---------|-------------|
| `o` | Create a new empty DOS (MBR) partition table |
| `n` | Add a new partition |
| `p` | Make it a primary partition |
| `1` | Partition number 1 |
| *(empty)* | Default first sector (2048) |
| *(empty)* | Default last sector (use all space) |
| `t` | Change partition type |
| `6` | Type `0x06` — FAT16 (CHS, ≥32 MB) or FAT16B |
| `w` | Write the table to disk and exit |

**Why MBR?** cupid-os reads the MBR partition table at boot to locate the FAT16 filesystem. The kernel's FAT16 driver (`fat16.c`) scans all four MBR partition entries looking for type `0x04`, `0x06`, or `0x0E`. Without a valid MBR partition table, the disk won't be recognized.

**Manual fdisk (interactive):** If you prefer to do it manually:
```bash
fdisk test-disk.img
```
Then type these commands one at a time:
```
Command: o          ← create new partition table
Command: n          ← new partition
  Type:  p          ← primary
  Number: 1
  First sector: (press Enter for default)
  Last sector:  (press Enter for default)
Command: t          ← change type
  Hex code: 6       ← FAT16
Command: w          ← write and exit
```

### 3. Set Up a Loop Device

To format the partition inside the image file, Linux needs to treat it as a block device:

```bash
LOOP=$(sudo losetup --show -fP test-disk.img)
echo "Loop device: $LOOP"
```

The `-fP` flags:
- `-f` — find the first available loop device
- `-P` — scan and create partition sub-devices (e.g., `/dev/loop0p1`)

After this, `${LOOP}p1` refers to the first partition inside the image.

**Alternative (manual offset):** If `-P` doesn't work on your system:
```bash
# The default first partition starts at sector 2048 (byte offset 1048576)
sudo losetup -o $((2048*512)) --sizelimit $((18432*512)) /dev/loop0 test-disk.img
# Then use /dev/loop0 instead of ${LOOP}p1 below
```

### 4. Format as FAT16

```bash
sudo mkfs.fat -F 16 -n "CUPIDOS" ${LOOP}p1
```

| Flag | Meaning |
|------|---------|
| `-F 16` | Force FAT16 format (not FAT12 or FAT32) |
| `-n "CUPIDOS"` | Volume label (up to 11 characters, optional) |

### 5. Mount and Add Files

```bash
sudo mkdir -p /mnt/cupidos
sudo mount ${LOOP}p1 /mnt/cupidos
```

Now copy files into it:

```bash
# Simple text file
echo "Hello from CupidOS!" | sudo tee /mnt/cupidos/HELLO.TXT

# A CupidScript program
cat <<'EOF' | sudo tee /mnt/cupidos/HELLO.CUP
#!/bin/cupid
NAME="World"
echo "Hello, $NAME!"
echo "Welcome to CupidOS"
EOF

# A CupidC source file
cat <<'EOF' | sudo tee /mnt/cupidos/DEMO.CC
void main() {
    println("Hello from CupidC on disk!");
    int x = 42;
    print("The answer is: ");
    print_int(x);
    println("");
}
EOF

# Copy any host files you want
# sudo cp ~/my-project/*.txt /mnt/cupidos/
```

**FAT16 filename rules:**
- Maximum **8 characters** for the name, **3** for the extension (8.3 format)
- Names are stored as **uppercase** — `hello.txt` becomes `HELLO.TXT`
- No spaces or special characters in filenames
- Only the **root directory** is supported (no subdirectories)

### 6. Unmount and Clean Up

```bash
sudo umount /mnt/cupidos
sudo losetup -d $LOOP
```

Your `test-disk.img` is now ready to use.

---

## Booting with the Disk

### Using make run-disk

The easiest way — the Makefile has a built-in target:

```bash
make run-disk
```

This runs:
```
qemu-system-i386 -boot a -fda cupidos.img -hda test-disk.img \
    -rtc base=localtime -serial stdio
```

The floppy (`-fda`) contains the boot image. The hard disk (`-hda`) is your FAT16 data disk, mounted at `/home` inside cupid-os.

### Manual QEMU Command

```bash
qemu-system-i386 \
    -boot a \
    -fda cupidos.img \
    -hda test-disk.img \
    -rtc base=localtime \
    -audiodev none,id=speaker \
    -machine pcspk-audiodev=speaker \
    -serial stdio
```

| Flag | Purpose |
|------|---------|
| `-boot a` | Boot from floppy (drive A) |
| `-fda cupidos.img` | Floppy drive — contains bootloader + kernel |
| `-hda test-disk.img` | Hard disk — your FAT16 data disk |
| `-rtc base=localtime` | Set RTC to host's local time |
| `-serial stdio` | Route serial debug output to your terminal |

---

## Using the Disk in cupid-os

Once booted with `make run-disk`, the FAT16 partition is automatically mounted at `/home`:

```
/> mount
Mounted filesystems:
  /       ramfs
  /dev    devfs
  /home   fat16

/> cd /home
/home> ls
HELLO   .TXT      21
HELLO   .CUP      64
DEMO    .CC       110

/home> cat HELLO.TXT
Hello from CupidOS!
```

### Reading and Writing Files

```bash
# Read a file
cat /home/HELLO.TXT

# Write a new file
vwrite /home/NOTES.TXT "My notes here"

# Delete a file
rm /home/OLD.TXT

# Open in Notepad (GUI)
# Click File → Open, navigate to /home, double-click a file
```

### Running Scripts from Disk

```bash
# Run a CupidScript file
cupid /home/HELLO.CUP

# Compile and run a CupidC file
cupidc /home/DEMO.CC
```

### Syncing to Disk

Writes go through a **block cache** and may not be flushed to disk immediately. The cache auto-flushes every 5 seconds, but you can force it:

```bash
sync
```

Always `sync` before shutting down QEMU to avoid data loss.

---

## Modifying the Disk After Creation

You can re-mount the image on your host to add, remove, or edit files:

```bash
# Re-attach
LOOP=$(sudo losetup --show -fP test-disk.img)
sudo mount ${LOOP}p1 /mnt/cupidos

# Make changes
sudo cp new-file.txt /mnt/cupidos/NEWFILE.TXT
sudo rm /mnt/cupidos/OLD.TXT

# Clean up
sudo umount /mnt/cupidos
sudo losetup -d $LOOP
```

---

## Troubleshooting

### "Disk error!" on boot
- The **floppy image** (`cupidos.img`) is corrupt or missing. Run `make` to rebuild it.
- This is unrelated to the hard disk image — the bootloader loads from floppy only.

### No files visible in /home
- Check that `test-disk.img` exists in the project root directory.
- Verify the MBR partition table: `fdisk -l test-disk.img`
- Ensure the partition type is `0x04`, `0x06`, or `0x0E` (FAT16).
- Check serial output for `FAT16 mounted at /disk` or error messages.

### "mount" doesn't show /home as fat16
- QEMU wasn't started with `-hda test-disk.img`. Use `make run-disk`.
- The ATA driver didn't detect the disk — check serial log for ATA init messages.

### Files not persisting after reboot
- Run `sync` before closing QEMU to flush the block cache.
- The auto-flush runs every 5 seconds, but closing QEMU abruptly can lose unflushed writes.

### Filenames appear truncated or garbled
- FAT16 uses **8.3 format** — names longer than 8 characters are truncated.
- Use only uppercase letters, digits, and underscores.
- No long filename (LFN) support.

### losetup -P doesn't create partition devices
- Some older kernels don't support `-P`. Use the manual offset method:
  ```bash
  # Check partition offset
  fdisk -l test-disk.img
  # Look for "Start" column — usually 2048
  sudo losetup -o $((2048*512)) /dev/loop0 test-disk.img
  # Use /dev/loop0 instead of ${LOOP}p1
  ```

### Permission denied
- Loop device and mount operations require `sudo`.
- If using WSL, ensure you're running in a WSL terminal (not PowerShell directly).

---

## Helper Script

Save this as `mkdisk.sh` in your project root for convenience:

```bash
#!/bin/bash
# mkdisk.sh — Create a FAT16 test disk for cupid-os
set -e

IMG="${1:-test-disk.img}"
SIZE="${2:-10}"

echo "Creating ${SIZE}MB disk image: $IMG"
dd if=/dev/zero of="$IMG" bs=1M count="$SIZE" 2>/dev/null
echo -e "o\nn\np\n1\n\n\nt\n6\nw" | fdisk "$IMG" > /dev/null 2>&1

LOOP=$(sudo losetup --show -fP "$IMG")
sudo mkfs.fat -F 16 -n "CUPIDOS" "${LOOP}p1" > /dev/null

sudo mkdir -p /mnt/cupidos
sudo mount "${LOOP}p1" /mnt/cupidos

echo "Hello from CupidOS!" | sudo tee /mnt/cupidos/HELLO.TXT > /dev/null
cat <<'SCRIPT' | sudo tee /mnt/cupidos/HELLO.CUP > /dev/null
#!/bin/cupid
echo "Hello from CupidScript on disk!"
SCRIPT

sudo umount /mnt/cupidos
sudo losetup -d "$LOOP"

echo "Done! Run with: make run-disk"
```

Usage:
```bash
chmod +x mkdisk.sh
./mkdisk.sh                  # Creates test-disk.img (10 MB)
./mkdisk.sh my-disk.img 32   # Creates my-disk.img (32 MB)
```

---

## See Also

- [Getting Started](Getting-Started) — Build and run cupid-os
- [Filesystem](Filesystem) — VFS architecture, RamFS, DevFS, FAT16 internals
- [Shell Commands](Shell-Commands) — Full command reference
- [CupidScript](CupidScript) — Writing scripts to run from disk
