# Disk Setup

This guide explains the current disk model and how to work with the FAT16 partition inside `cupidos.img`. Persistent file storage in `/home` lives in this partition.

---

## Quick Setup (Copy-Paste)

If you just want a working HDD image fast:

```bash
# Build (or reuse) cupidos.img
make HDD_MB=200

# Copy host file into FAT root (maps to /home in CupidOS)
mcopy -o -i cupidos.img@@2097152 cupid.bmp ::/cupid.bmp

# Boot
make run
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

### 1. Build (or Reuse) the HDD Image

```bash
make
```
This creates `cupidos.img` (or reuses the existing image) with:
- MBR boot code + partition table
- Stage 2 + kernel area
- FAT16 partition mounted at `/home`

Choose image size with `HDD_MB`:

```bash
make HDD_MB=100
make HDD_MB=200
```

Default is `HDD_MB=200`.

### 2. Copy Files from Host (Recommended: mtools)

Install `mtools` if needed:

```bash
sudo apt-get update
sudo apt-get install -y mtools
```

Copy files into FAT root:

```bash
mcopy -o -i cupidos.img@@2097152 cupid.bmp ::/cupid.bmp
```

List files:

```bash
mdir -i cupidos.img@@2097152 ::/
```

Mapping:
- FAT `::/` corresponds to CupidOS `/home`
- `::/cupid.bmp` becomes `/home/cupid.bmp` in CupidOS

If `FAT_START_LBA` is customized, use offset:

```bash
offset_bytes=$((FAT_START_LBA*512))
mcopy -o -i cupidos.img@@${offset_bytes} file.txt ::/file.txt
```

### 3. Alternate Linux Loop-Mount Method

If you prefer mounting from Linux directly:

```bash
sudo mount -o loop,offset=$((4096*512)) cupidos.img /mnt/cupidos
```

This mounts the FAT16 partition directly from `cupidos.img`.

### 4. Add Files (Loop-Mount Path)

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

### 5. Unmount

```bash
sudo umount /mnt/cupidos
```

Your `cupidos.img` now contains the updated `/home` data.

---

## Booting

Use:

```bash
make run
```

### Manual QEMU Command

```bash
qemu-system-i386 \
  -boot c \
  -hda cupidos.img \
    -rtc base=localtime \
    -audiodev none,id=speaker \
    -machine pcspk-audiodev=speaker \
    -serial stdio
```

| Flag | Purpose |
|------|---------|
| `-boot c` | Boot from hard disk |
| `-hda cupidos.img` | HDD image with bootloader, kernel, and FAT16 `/home` |
| `-rtc base=localtime` | Set RTC to host's local time |
| `-serial stdio` | Route serial debug output to your terminal |

---

## Using the Disk in cupid-os

Once booted with `make run`, the FAT16 partition is automatically mounted at `/home`:

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

## Persistence and Cleaning

- `make` and `make run` keep existing `cupidos.img`
- `make clean` keeps `cupidos.img`
- `make clean-image` removes only `cupidos.img`
- `make distclean` removes build artifacts and `cupidos.img`

---

## Troubleshooting

### "Disk error!" on boot
- `cupidos.img` may be missing or corrupted. Rebuild with `make`.

### No files visible in /home
- Verify files were copied to FAT root `::/` (not `::/home`).
- Check FAT listing from host: `mdir -i cupidos.img@@2097152 ::/`.
- Check serial output for FAT16 mount messages.

### "mount" doesn't show /home as fat16
- QEMU may not be using `cupidos.img`; use `make run`.
- The ATA driver didn't detect the disk — check serial log for ATA init messages.

### Files not persisting after reboot
- Run `sync` before closing QEMU to flush the block cache.
- The auto-flush runs every 5 seconds, but closing QEMU abruptly can lose unflushed writes.

### Filenames appear truncated or garbled
- FAT16 uses **8.3 format** — names longer than 8 characters are truncated.
- Use only uppercase letters, digits, and underscores.
- No long filename (LFN) support.

### Wrong offset in mtools/loop mount
- Default FAT offset is `2097152` bytes (`4096 * 512`).
- If `FAT_START_LBA` changed, recompute offset as `FAT_START_LBA * 512`.

### Permission denied
- Loop device and mount operations require `sudo`.
- If using WSL, ensure you're running in a WSL terminal (not PowerShell directly).

---

## See Also

- [Getting Started](Getting-Started) — Build and run cupid-os
- [Filesystem](Filesystem) — VFS architecture, RamFS, DevFS, FAT16 internals
- [Shell Commands](Shell-Commands) — Full command reference
- [CupidScript](CupidScript) — Writing scripts to run from disk
