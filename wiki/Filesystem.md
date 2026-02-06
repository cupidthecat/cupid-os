# Filesystem

cupid-os implements a Linux-style **Virtual File System (VFS)** that provides a unified file API across multiple filesystem types. The VFS enables a hierarchical directory structure (`/home`, `/dev`, `/tmp`, `/bin`) with three backend drivers: RamFS for in-memory storage, DevFS for device files, and a FAT16 wrapper for persistent disk storage.

---

## Architecture

```
┌──────────────────────────────────────────────────┐
│         Shell / Notepad / Applications           │
│   (cd, ls, cat, mount, vls, vcat, exec, ...)     │
├──────────────────────────────────────────────────┤
│           Virtual File System (VFS)              │
│   vfs_open · vfs_read · vfs_write · vfs_close    │
│   vfs_readdir · vfs_stat · vfs_mkdir · vfs_seek  │
│   Mount table │ FD table │ Path resolution       │
├────────────┬────────────┬────────────────────────┤
│   RamFS    │   DevFS    │   FAT16 VFS Wrapper    │
│ (ramfs.c)  │ (devfs.c)  │   (fat16_vfs.c)        │
│            │            ├────────────────────────┤
│ /          │ /dev       │   FAT16 Driver         │
│ /bin       │            │   (fat16.c)            │
│ /tmp       │            ├────────────────────────┤
│            │            │   Block Cache (LRU)    │
│            │            │   (blockcache.c)       │
│            │            ├────────────────────────┤
│            │            │   Block Device Layer   │
│            │            │   (blockdev.c)         │
│            │            ├────────────────────────┤
│            │            │   ATA/IDE PIO Driver   │
│            │            │   (ata.c)              │
└────────────┴────────────┴────────────────────────┘
```

---

## VFS Layer

The VFS (`kernel/vfs.c/h`) is the top-level abstraction providing a unified file API. All applications — shell, notepad, program loader — use VFS calls exclusively.

### Features

- **Mount table** — up to 16 simultaneous mount points
- **File descriptor table** — up to 64 open files
- **Path resolution** — longest-prefix match finds the correct filesystem
- **Current working directory** — shell tracks CWD for relative paths
- **Pluggable backends** — any filesystem implementing `vfs_fs_ops_t` can be mounted

### Mount Points

| Path | Filesystem | Purpose |
|------|-----------|---------|
| `/` | RamFS | Root filesystem, boot-time files |
| `/dev` | DevFS | Device special files |
| `/home` | FAT16 | User files on ATA disk |
| `/bin` | RamFS | System programs (subdirectory of root) |
| `/tmp` | RamFS | Temporary files (subdirectory of root) |

### Directory Hierarchy

```
/                        (RamFS root)
├── bin/                 (system programs — future)
├── tmp/                 (temporary files)
├── home/                (FAT16 — persistent user files on disk)
│   ├── HELLO.TXT
│   ├── SCRIPT.CUP
│   └── ...
└── dev/                 (DevFS — device special files)
    ├── null             (discard sink / EOF source)
    ├── zero             (infinite zero bytes)
    ├── random           (pseudo-random bytes)
    └── serial           (COM1 serial output)
```

### VFS API

| Function | Description |
|----------|-------------|
| `vfs_init()` | Initialize VFS mount and FD tables |
| `vfs_register_fs(name, ops)` | Register a filesystem type |
| `vfs_mount(source, target, type)` | Mount a filesystem at a path |
| `vfs_open(path, flags)` | Open a file, returns fd |
| `vfs_close(fd)` | Close a file descriptor |
| `vfs_read(fd, buf, count)` | Read bytes from a file |
| `vfs_write(fd, buf, count)` | Write bytes to a file |
| `vfs_seek(fd, offset, whence)` | Seek within a file |
| `vfs_stat(path, stat)` | Get file/directory information |
| `vfs_readdir(fd, dirent)` | Read next directory entry |
| `vfs_mkdir(path)` | Create a directory |
| `vfs_unlink(path)` | Delete a file |

### Open Flags

| Flag | Value | Description |
|------|-------|-------------|
| `O_RDONLY` | 0x0000 | Read only |
| `O_WRONLY` | 0x0001 | Write only |
| `O_RDWR` | 0x0002 | Read and write |
| `O_CREAT` | 0x0100 | Create file if it doesn't exist |
| `O_TRUNC` | 0x0200 | Truncate file to zero length |
| `O_APPEND` | 0x0400 | Append to end of file |

### Path Resolution

When `vfs_open("/home/README.TXT", O_RDONLY)` is called:

1. **Longest prefix match** — scan mount table for best match
   - `/home` matches → FAT16 filesystem
2. **Calculate relative path** — strip mount prefix
   - `/home/README.TXT` → `README.TXT`
3. **Call filesystem driver** — `fat16_vfs_open(fs_data, "README.TXT", ...)`
4. **Allocate file descriptor** — wrap in VFS fd and return to caller

### Error Codes

| Code | Name | Meaning |
|------|------|---------|
| 0 | `VFS_OK` | Success |
| -2 | `VFS_ENOENT` | No such file or directory |
| -5 | `VFS_EIO` | I/O error |
| -12 | `VFS_ENOMEM` | Out of memory |
| -17 | `VFS_EEXIST` | File already exists |
| -20 | `VFS_ENOTDIR` | Not a directory |
| -22 | `VFS_EINVAL` | Invalid argument |
| -24 | `VFS_EMFILE` | Too many open files |
| -28 | `VFS_ENOSPC` | No space left |
| -38 | `VFS_ENOSYS` | Operation not supported |

---

## RamFS

The RAM filesystem (`kernel/ramfs.c/h`) provides an in-memory directory tree used for the root filesystem and temporary storage.

### Features

- Full directory tree with parent/children pointers
- Dynamic file content allocation (up to 64KB per file)
- Create, read, write, delete files and directories
- Auto-creates parent directories via `ramfs_mkdirs()`
- Pre-populated at boot with built-in files (LICENSE.txt, MOTD.txt)

### Data Structure

```c
ramfs_node_t {
    name[64]        // Filename
    data*           // File contents (NULL for directories)
    size            // Current data size
    capacity        // Allocated buffer size
    type            // FILE or DIR
    parent*         // Parent directory
    children*       // First child (directories only)
    next*           // Next sibling
}
```

### How It Works

- **Path lookup**: Split path on `/`, walk directory tree node by node
- **File creation**: Allocate `ramfs_node_t`, link into parent's children list
- **File write**: Grow data buffer if needed (allocate new, copy, free old)
- **Directory listing**: Iterate children via `next` pointer

---

## DevFS

The device filesystem (`kernel/devfs.c/h`) exposes hardware and pseudo-devices as regular files under `/dev`.

### Built-in Devices

| Device | Read behavior | Write behavior |
|--------|--------------|----------------|
| `/dev/null` | Returns EOF (0 bytes) | Discards all data |
| `/dev/zero` | Returns zero bytes | Discards all data |
| `/dev/random` | Returns pseudo-random bytes | Ignored |
| `/dev/serial` | Not supported | Writes to COM1 serial |

### Example Usage

```
> cat /dev/random     # Outputs random bytes (limited to 64KB)
> vwrite /dev/null "test"   # Writes are silently discarded
> vcat /dev/zero      # Outputs zero bytes (limited to 64KB)
```

**Note:** Reading from infinite devices (`/dev/zero`, `/dev/random`) is capped at 64KB to prevent system freezes.

---

## FAT16 VFS Wrapper

The FAT16 VFS wrapper (`kernel/fat16_vfs.c/h`) adapts the existing FAT16 driver to the VFS interface, making disk files accessible through the unified API.

### How It Works

- `fat16_vfs_open()` wraps `fat16_open()` for reading
- `fat16_vfs_readdir()` uses `fat16_enumerate_root()` to list directory entries
- `fat16_vfs_unlink()` wraps `fat16_delete_file()`
- Read operations wrap `fat16_read()` with position tracking

### Limitations

| Feature | Status |
|---------|--------|
| Read files | ✅ via VFS |
| List directory | ✅ via VFS readdir |
| Delete files | ✅ via VFS unlink |
| Write files | ⚠️ Fallback to `fat16_write_file()` |
| Subdirectories | ❌ (root directory only) |
| Long filenames | ❌ (8.3 format only) |

### Filename Rules

- Maximum 8 characters for name, 3 for extension
- Case-insensitive (stored as uppercase)
- No spaces or special characters
- Examples: `HELLO.TXT`, `SCRIPT.CUP`, `DATA.BIN`

---

## Program Loader

The program loader (`kernel/exec.c/h`) loads and runs CUPD-format executables from the VFS.

### CUPD Executable Format

```
┌──────────────────────────┐  Offset 0
│  Header (20 bytes)       │
│  ├─ magic: 0x43555044    │  "CUPD"
│  ├─ entry_offset         │  Entry point offset from image base
│  ├─ code_size            │  Size of code section
│  ├─ data_size            │  Size of initialized data
│  └─ bss_size             │  Size of uninitialized data
├──────────────────────────┤  Offset 20
│  Code Section            │  Executable instructions
├──────────────────────────┤
│  Data Section            │  Initialized global data
└──────────────────────────┘
```

### Loading Process

1. Open executable via `vfs_open(path, O_RDONLY)`
2. Read and validate 20-byte header (check magic `0x43555044`)
3. Allocate memory for code + data + BSS (max 256KB)
4. Load code and data sections from file
5. Zero-fill BSS section
6. Create a new kernel process pointing to the entry function
7. Process runs as a normal scheduled thread

### Shell Usage

```
> exec /home/hello.exe
> exec /bin/myprog
```

---

## Legacy Disk I/O Stack

The VFS sits on top of the existing disk I/O stack, which remains unchanged.

### Block Device Layer

The block device abstraction (`kernel/blockdev.c`) provides a uniform interface between the filesystem and disk driver.

| Function | Description |
|----------|-------------|
| `blockdev_read(sector, buf)` | Read one 512-byte sector |
| `blockdev_write(sector, buf)` | Write one 512-byte sector |
| `blockdev_init()` | Initialize the block device |

### Block Cache

An LRU (Least Recently Used) write-back cache (`kernel/blockcache.c`) sits between the block device layer and the ATA driver.

| Parameter | Value |
|-----------|-------|
| Cache entries | 64 |
| Entry size | 512 bytes (1 sector) |
| Eviction policy | LRU |
| Write policy | Write-back (lazy) |

How it works:

1. **Read hit**: Return cached sector immediately (no disk I/O)
2. **Read miss**: Read from disk, store in cache, evict LRU if full
3. **Write**: Mark cache entry as dirty, actual write deferred
4. **Sync**: Flush all dirty entries to disk
5. **Eviction**: When a dirty entry is evicted, it's written to disk first

### ATA/IDE Driver

The ATA driver (`drivers/ata.c`) implements PIO (Programmed I/O) mode for IDE disk access.

| Feature | Status |
|---------|--------|
| PIO read | ✅ |
| PIO write | ✅ |
| Identify device | ✅ |
| DMA | ❌ |
| ATAPI/CD | ❌ |
| Secondary channel | ❌ |

---

## Shell Commands

### VFS Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `cd` | `cd <path>` | Change current working directory |
| `pwd` | `pwd` | Print current working directory |
| `ls` | `ls [path]` | List files in current directory (or given path) |
| `cat` | `cat <file>` | Display file contents (supports VFS paths) |
| `mount` | `mount` | Show all mounted filesystems |
| `vls` | `vls [path]` | List files at an absolute VFS path |
| `vcat` | `vcat <path>` | Display file at an absolute VFS path |
| `vstat` | `vstat <path>` | Show file/directory info (type, size) |
| `vmkdir` | `vmkdir <path>` | Create a directory |
| `vrm` | `vrm <path>` | Delete a file |
| `vwrite` | `vwrite <path> <text>` | Write text to a file |
| `exec` | `exec <path>` | Load and run a CUPD executable |

### Legacy Disk Commands

| Command | Description |
|---------|-------------|
| `lsdisk` | List files on FAT16 disk directly |
| `catdisk <file>` | Read a file from FAT16 disk directly |
| `sync` | Flush block cache to disk |
| `cachestats` | Show block cache hit/miss statistics |

### Example Session

```
/home> ls
HELLO   .TXT    1234
SCRIPT  .CUP     256

/home> cat HELLO.TXT
Hello from CupidOS disk!

/home> cd /dev
/dev> ls
null
zero
random
serial

/dev> cd /
/> ls
bin/
tmp/
home/
dev/
LICENSE.txt
MOTD.txt

/> mount
Mounted filesystems:
  /       ramfs
  /dev    devfs
  /home   fat16

/> vmkdir /tmp/test
/> vwrite /tmp/test/hello.txt Hello World
/> vcat /tmp/test/hello.txt
Hello World
```

---

## Notepad Integration

The Notepad application uses VFS for its file dialog, open, and save operations:

- **File dialog** browses VFS directories starting at `/home`
- **Directory navigation** — click directories or press Enter to navigate into them; `..` goes up
- **Double-click** a file to open it, or a directory to enter it
- **Open** reads file contents via `vfs_open()` / `vfs_read()` / `vfs_close()`
- **Save** writes via `vfs_open(O_WRONLY | O_CREAT | O_TRUNC)` / `vfs_write()` / `vfs_close()`
- Saving to `/home` persists data to the ATA disk through the FAT16 VFS wrapper

---

## Creating a Test Disk

To create a FAT16 disk image for testing with QEMU:

```bash
# Create a 32MB disk image
dd if=/dev/zero of=disk.img bs=1M count=32

# Partition with fdisk (create one primary partition, type 0x06 = FAT16)
echo -e "o\nn\np\n1\n\n\nt\n6\nw" | fdisk disk.img

# Set up a loop device with offset
LOOP=$(sudo losetup --show -fP disk.img)

# Format the partition as FAT16
sudo mkfs.fat -F 16 ${LOOP}p1

# Mount and add files
sudo mount ${LOOP}p1 /mnt
echo "Hello World" | sudo tee /mnt/HELLO.TXT
echo -e '#!/cupid\necho "Hello from CupidScript!"' | sudo tee /mnt/TEST.CUP
sudo umount /mnt

# Clean up
sudo losetup -d $LOOP
```

Then attach it to QEMU:

```bash
qemu-system-i386 -hda disk.img -serial stdio ...
```

---

## See Also

- [Architecture](Architecture) — How the filesystem fits into the system
- [CupidScript](CupidScript) — Running `.cup` scripts from disk
- [Shell Commands](Shell-Commands) — Full command reference
- [Debugging](Debugging) — Serial logging from filesystem operations
