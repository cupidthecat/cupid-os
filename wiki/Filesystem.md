# Filesystem

cupid-os uses a Linux-style **Virtual File System (VFS)** to expose one file API across several filesystem types. The directory tree contains RamFS boot files, DevFS devices, raw FAT16 at `/disk`, and homefs at `/home`. Homefs stores its logical tree in `/disk/HOMEFS.SYS`; direct FAT16 at `/home` is a fallback used only when the homefs mount fails.

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

Runtime mount ownership is:

```text
RamFS   -> /
DevFS   -> /dev
FAT16   -> /disk
homefs  -> /home  (backed by /disk/HOMEFS.SYS)
```

---

## VFS Layer

The VFS implementation is in `kernel/fs/vfs.c/h`. The shell, Notepad, and program loader access files through this API.

### Features

- **Mount table** - up to 16 simultaneous mount points
- **File descriptor table** - up to 64 open files
- **Path resolution** - longest-prefix match finds the correct filesystem
- **Current working directory** - shell tracks CWD for relative paths
- **Pluggable backends** - any filesystem implementing `vfs_fs_ops_t` can be mounted

### Mount Points

| Path | Filesystem | Purpose |
|------|-----------|---------|
| `/` | RamFS | Root filesystem, boot-time files |
| `/dev` | DevFS | Device special files |
| `/disk` | FAT16 | Raw files in the disk partition |
| `/home` | homefs | Persistent logical tree backed by `/disk/HOMEFS.SYS` |
| `/bin` | RamFS | System programs (subdirectory of root) |
| `/tmp` | RamFS | Temporary files (subdirectory of root) |

### Directory Hierarchy

```
/                        (RamFS root)
├── bin/                 (system programs - future)
├── tmp/                 (temporary files)
├── disk/                (raw FAT16 partition)
│   └── HOMEFS.SYS       (homefs backing container)
├── home/                (persistent homefs logical tree)
│   ├── HELLO.TXT
│   ├── SCRIPT.CUP
│   └── ...
└── dev/                 (DevFS - device special files)
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

1. **Longest prefix match** - scan mount table for best match
   - `/home` matches -> homefs
2. **Calculate relative path** - strip mount prefix
   - `/home/README.TXT` -> `README.TXT`
3. **Call filesystem driver** - `homefs_open(fs_data, "README.TXT", ...)`
4. **Allocate file descriptor** - wrap in VFS fd and return to caller

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

### VFS Helpers

The helpers in `kernel/fs/vfs_helpers.c/h` wrap common `vfs_open`, `vfs_read`, `vfs_write`, and `vfs_close` sequences in single calls. They are also available as CupidC bindings.

| Function | Description |
|----------|-------------|
| `vfs_read_all(path, buffer, max_size)` | Read entire file into buffer. Returns bytes read, or negative VFS error. |
| `vfs_write_all(path, buffer, size)` | Write buffer to file (creates or truncates). Returns bytes written, or negative VFS error. |
| `vfs_read_text(path, buffer, max_size)` | Read file as null-terminated string. Reserves 1 byte for `\0`. Returns string length. |
| `vfs_write_text(path, text)` | Write null-terminated string to file. Returns bytes written (excluding null). |

All helpers return `>= 0` on success and a negative VFS error code on failure, such as `VFS_ENOENT`, `VFS_EIO`, or `VFS_ENOSPC`. They close file descriptors on both success and error paths.

**Example (CupidC):**
```c
void main() {
    char buf[1024];

    // Read a text file
    int len = vfs_read_text("/home/notes.txt", buf, 1024);
    if (len >= 0) {
        print("Contents: ");
        println(buf);
    } else {
        println("Read failed");
    }

    // Write a text file
    int ret = vfs_write_text("/home/output.txt", "Hello from CupidC!");
    if (ret >= 0) {
        println("Written successfully");
    }
}
```

---

## RamFS

The RAM filesystem (`kernel/fs/ramfs.c/h`) provides an in-memory directory tree used for the root filesystem and temporary storage.

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

The device filesystem (`kernel/fs/devfs.c/h`) exposes hardware and pseudo-devices as regular files under `/dev`.

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

The FAT16 VFS wrapper (`kernel/fs/fat16_vfs.c/h`) adapts the existing FAT16 driver to the VFS interface, making raw disk files accessible at `/disk`. Homefs separately serializes `/home` into `/disk/HOMEFS.SYS`.

### How It Works

- `fat16_vfs_open()` wraps `fat16_open()` for reading
- `fat16_vfs_readdir()` uses `fat16_enumerate_root()` to list directory entries
- `fat16_vfs_unlink()` wraps `fat16_delete_file()`
- Read operations wrap `fat16_read()` with position tracking

### Limitations

| Feature | Status |
|---------|--------|
| Read files | Supported through VFS |
| List directory | Supported through VFS `readdir` |
| Delete files | Supported through VFS `unlink` |
| Write files | Falls back to `fat16_write_file()` |
| Subdirectories | Not supported; root directory only |
| Long filenames | Not supported; 8.3 format only |

### Filename Rules

- Maximum 8 characters for name, 3 for extension
- Case-insensitive (stored as uppercase)
- No spaces or special characters
- Examples: `HELLO.TXT`, `SCRIPT.CUP`, `DATA.BIN`

---

## Program Loader

The program loader (`kernel/lang/exec.c/h`) loads and runs executables from the VFS. It supports two binary formats with automatic detection based on the first 4 bytes of the file.

### Supported Formats

| Format | Magic Bytes | Description |
|--------|-------------|-------------|
| **ELF32** | `7F 45 4C 46` (`\x7FELF`) | Static i386 ELF executables, currently compiled with GCC/Clang and linked with CupidLD |
| **CUPD** | `43 55 50 44` (`CUPD`) | CupidOS flat binary format (legacy) |

### Format Detection

```
exec("/home/hello", "hello")
       │
       ▼
┌─────────────────────┐
│ Read first 4 bytes  │
└────┬────────────────┘
     │
     ├─ 0x7F 'E' 'L' 'F'  -> elf_exec()   (ELF32 loader)
     │
     ├─ 0x43555044 (CUPD)  -> cupd_exec()  (CUPD loader)
     │
     └─ anything else      -> VFS_EINVAL
```

### ELF32 Programs (Primary Format)

ELF is the primary binary format. The hosted examples are compiled to relocatable objects with GCC or Clang, linked with CupidLD, and receive a **syscall table** pointer as their first argument. See [ELF Programs](ELF-Programs) for compilation instructions, the API reference, and examples.

**Quick example:**

```c
#include "cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);
    print("Hello from ELF!\n");
    exit();
}
```

**Loading process (ELF):**

1. Stat and open the file, then validate its ELF32/i386/static header and table bounds
2. Validate every `PT_LOAD`, executable entry, alignment, file range, and accepted fixed arena
3. Read the complete zero-initialized image into bounded staging memory
4. Claim the exclusive external-arena lease when that profile is selected
5. Commit the staged bytes to their identity-mapped virtual addresses
6. Call `process_create_with_arg_image_ex()` to transfer image/lease ownership atomically with process publication
7. Process runs as a normal scheduled kernel thread; cleanup releases the lease, not the permanently reserved pages

### CUPD Programs (Legacy Format)

CUPD is the original CupidOS flat binary format with a simple 20-byte header.

```
┌──────────────────────────┐  Offset 0
│  Header (20 bytes)       │
│  ├─ magic: 0x43555044    │  "CUPD"
│  ├─ entry_offset         │  Entry point offset from code start                      
│  ├─ code_size            │  Size of code section
│  ├─ data_size            │  Size of initialized data
│  └─ bss_size             │  Size of uninitialized data
├──────────────────────────┤  Offset 20
│  Code Section            │  Executable instructions
├──────────────────────────┤
│  Data Section            │  Initialized global data
└──────────────────────────┘
```

**Loading process (CUPD):**

1. Open file via `vfs_open(path, O_RDONLY)`
2. Read and validate 20-byte header (check magic `0x43555044`)
3. Allocate memory via `kmalloc()` for code + data + BSS (max 256 KB)
4. Load code and data sections from file
5. Zero-fill BSS section
6. Create a new kernel process pointing to the entry function
7. Process runs as a normal scheduled thread

### Shell Usage

```
> exec /home/hello
> exec /home/ls
> exec /bin/myprog
```

---

## Legacy Disk I/O Stack

The VFS sits on top of the legacy disk I/O stack.

### Block Device Layer

The block device abstraction (`kernel/fs/blockdev.c`) provides a uniform interface between the filesystem and disk driver.

| Function | Description |
|----------|-------------|
| `blockdev_read(sector, buf)` | Read one 512-byte sector |
| `blockdev_write(sector, buf)` | Write one 512-byte sector |
| `blockdev_init()` | Initialize the block device |

### Block Cache

An LRU (Least Recently Used) write-back cache (`kernel/fs/blockcache.c`) sits between the block device layer and the ATA driver.

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
| PIO read | Supported |
| PIO write | Supported |
| Identify device | Supported |
| DMA | Not supported |
| ATAPI/CD | Not supported |
| Secondary channel | Not supported |

---

## Shell Commands

### VFS Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `cd` | `cd <path>` | Change current working directory |
| `pwd` | `pwd` | Print current working directory |
| `ls` | `ls [path]` | List files in current directory (or given path) |
| `cat` | `cat <file>` | Display file contents (supports VFS paths) |
| `mkdir` | `mkdir <dir...>` | Create one or more directories |
| `rm` | `rm <file...>` | Delete files |
| `touch` | `touch <file...>` | Create empty files or update timestamps |
| `cp` | `cp <source> <dest>` | Copy a file |
| `mv` | `mv <source> <dest>` | Move/rename a file |
| `find` | `find [path] [name]` | Search a directory tree |
| `grep` | `grep <pattern> <path...>` | Search file contents |
| `mount` | `mount` | Show all mounted filesystems |
| `umount` | `umount <target>` | Unmount a filesystem |
| `exec` | `exec <path>` | Load and run a CUPD executable |

### Legacy Disk Commands

| Command | Description |
|---------|-------------|
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
  /disk   fat16
  /home   homefs

/> mkdir /tmp/test
/> echo Hello World > /tmp/test/hello.txt
/> cat /tmp/test/hello.txt
Hello World
```

---

## Notepad Integration

The Notepad application uses VFS for its file dialog, open, and save operations:

- **File dialog** browses VFS directories starting at `/home`
- **Directory navigation** - click directories or press Enter to navigate into them; `..` goes up
- **Double-click** a file to open it, or a directory to enter it
- **Open** reads file contents via `vfs_open()` / `vfs_read()` / `vfs_close()`
- **Save** writes via `vfs_open(O_WRONLY | O_CREAT | O_TRUNC)` / `vfs_write()` / `vfs_close()`
- Saving to `/home` updates homefs, which persists through `/disk/HOMEFS.SYS`

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

- [Architecture](Architecture) - How the filesystem fits into the system
- [CupidScript](CupidScript) - Running `.cup` scripts from disk
- [Shell Commands](Shell-Commands) - Full command reference
- [Debugging](Debugging) - Serial logging from filesystem operations
