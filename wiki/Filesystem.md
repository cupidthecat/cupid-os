# Filesystem

cupid-os supports two filesystem layers: a built-in in-memory filesystem for bundled files, and a FAT16 driver for persistent disk storage via ATA/IDE.

---

## Architecture

```
┌─────────────────────────────────┐
│       Shell / Applications      │
├─────────────────────────────────┤
│   In-Memory FS  │  FAT16 Driver │
│   (fs.c/fs.h)   │ (fat16.c/h)   │
├──────────────────┼───────────────┤
│                  │  Block Cache   │
│                  │ (blockcache.c) │
│                  ├───────────────┤
│                  │  Block Device  │
│                  │  (blockdev.c)  │
│                  ├───────────────┤
│                  │   ATA Driver   │
│                  │   (ata.c/h)    │
└──────────────────┴───────────────┘
```

---

## In-Memory Filesystem

The in-memory FS (`kernel/fs.c`) provides a simple read-only file table compiled into the kernel.

### Features

- Fixed file table (static array of entries)
- Each entry has: filename, pointer to data, size
- Used for built-in scripts, default configuration, README
- Accessed via `fs_read()` and `fs_list()`

### API

| Function | Description |
|----------|-------------|
| `fs_read(name, buf, size)` | Read a file's contents into buffer |
| `fs_list()` | List all in-memory files |
| `fs_find(name)` | Find a file entry by name |

### Shell Integration

```
> ls          # Lists in-memory files
> cat file    # Reads from in-memory FS first, then FAT16
```

The shell checks the in-memory filesystem first, falling back to FAT16 for disk files.

---

## FAT16 Driver

The FAT16 driver (`kernel/fat16.c`) provides full read/write access to a FAT16-formatted disk partition.

### Supported Features

| Feature | Status |
|---------|--------|
| Read files | ✅ |
| Write files | ✅ |
| Delete files | ✅ |
| List directory | ✅ |
| Long filenames | ❌ (8.3 only) |
| Subdirectories | ❌ (root dir only) |
| Multiple partitions | ❌ (first partition) |

### Disk Layout

```
┌────────────────┐  Sector 0
│      MBR       │  Master Boot Record with partition table
├────────────────┤  Partition start
│  Boot Sector   │  BPB (BIOS Parameter Block)
├────────────────┤
│  FAT Table 1   │  File Allocation Table (primary)
├────────────────┤
│  FAT Table 2   │  File Allocation Table (backup)
├────────────────┤
│ Root Directory  │  Fixed-size root directory entries
├────────────────┤
│   Data Area    │  File data in clusters
└────────────────┘
```

### How It Works

1. **MBR Parsing**: Reads the partition table from sector 0 to find the FAT16 partition
2. **BPB Reading**: Parses the BIOS Parameter Block for filesystem geometry (sectors per cluster, FAT size, root directory entries, etc.)
3. **File Lookup**: Scans root directory entries for matching 8.3 filename
4. **Cluster Chain**: Follows the FAT to read all clusters belonging to a file
5. **Write Support**: Finds free clusters, updates FAT entries, writes directory entry

### API

| Function | Description |
|----------|-------------|
| `fat16_init()` | Initialize driver, parse MBR and BPB |
| `fat16_read(name, buf, max)` | Read file contents |
| `fat16_write(name, data, size)` | Write/create a file |
| `fat16_delete(name)` | Delete a file |
| `fat16_list()` | List root directory entries |
| `fat16_exists(name)` | Check if a file exists |

### Filename Rules

- Maximum 8 characters for name, 3 for extension
- Case-insensitive (stored as uppercase)
- No spaces or special characters
- Examples: `HELLO.TXT`, `SCRIPT.CUP`, `DATA.BIN`

---

## Block Device Layer

The block device abstraction (`kernel/blockdev.c`) provides a uniform interface between the filesystem and disk driver.

| Function | Description |
|----------|-------------|
| `blockdev_read(sector, buf)` | Read one 512-byte sector |
| `blockdev_write(sector, buf)` | Write one 512-byte sector |
| `blockdev_init()` | Initialize the block device |

---

## Block Cache

An LRU (Least Recently Used) write-back cache (`kernel/blockcache.c`) sits between the block device layer and the ATA driver.

### Configuration

| Parameter | Value |
|-----------|-------|
| Cache entries | 64 |
| Entry size | 512 bytes (1 sector) |
| Eviction policy | LRU |
| Write policy | Write-back (lazy) |

### How It Works

1. **Read hit**: Return cached sector immediately (no disk I/O)
2. **Read miss**: Read from disk, store in cache, evict LRU if full
3. **Write**: Mark cache entry as dirty, actual write deferred
4. **Sync**: Flush all dirty entries to disk
5. **Eviction**: When a dirty entry is evicted, it's written to disk first

### Cache Statistics

```
> cachestats
Block Cache Statistics:
  Entries: 64
  Hits:    1,247
  Misses:  83
  Hit rate: 93.7%
  Dirty:   4
```

---

## ATA/IDE Driver

The ATA driver (`drivers/ata.c`) implements PIO (Programmed I/O) mode for IDE disk access.

### Features

| Feature | Status |
|---------|--------|
| PIO read | ✅ |
| PIO write | ✅ |
| Identify device | ✅ |
| DMA | ❌ |
| ATAPI/CD | ❌ |
| Secondary channel | ❌ |

### I/O Ports

| Port | Register | Direction |
|------|----------|-----------|
| 0x1F0 | Data | R/W |
| 0x1F1 | Error / Features | R/W |
| 0x1F2 | Sector Count | W |
| 0x1F3 | LBA Low | W |
| 0x1F4 | LBA Mid | W |
| 0x1F5 | LBA High | W |
| 0x1F6 | Drive/Head | W |
| 0x1F7 | Status / Command | R/W |

### Read/Write Flow

1. Wait for drive not busy (poll status register)
2. Select drive and set LBA address
3. Send READ SECTORS (0x20) or WRITE SECTORS (0x30) command
4. Transfer 256 words (512 bytes) via port 0x1F0
5. For writes: send CACHE FLUSH (0xE7) command

---

## Disk Shell Commands

| Command | Description |
|---------|-------------|
| `lsdisk` | List files on FAT16 disk |
| `catdisk <file>` | Read and display a file from disk |
| `writedisk <file> <data>` | Write data to a file on disk |
| `deldisk <file>` | Delete a file from disk |
| `sync` | Flush block cache to disk |
| `cachestats` | Show block cache hit/miss statistics |

### Examples

```
> lsdisk
HELLO   .TXT    1234  2024-01-15 10:30
SCRIPT  .CUP     256  2024-01-15 11:00

> catdisk HELLO.TXT
Hello from the FAT16 filesystem!

> writedisk TEST.TXT This is a test file
Written 19 bytes to TEST.TXT

> sync
Cache synced: 2 dirty blocks written

> cachestats
Block Cache Statistics:
  Hits:    89
  Misses:  12
  Hit rate: 88.1%
```

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
