# Shell Commands

cupid-os provides 24 built-in shell commands. The shell features command history (arrow up/down), tab completion for commands and filenames, and prompt display.

---

## Command Reference

### System Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `help` | `help` | List all available commands with descriptions |
| `clear` | `clear` | Clear the screen (works in both text and GUI modes) |
| `echo` | `echo <text>` | Print text to the terminal |
| `time` | `time` | Show system uptime in seconds and milliseconds |
| `reboot` | `reboot` | Reboot the machine via keyboard controller reset |
| `history` | `history` | Show the last 16 commands entered |
| `sysinfo` | `sysinfo` | Show uptime, CPU frequency, timer frequency, and memory usage |

### Filesystem Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `ls` | `ls` | List files in the in-memory filesystem with sizes |
| `cat` | `cat <filename>` | Display contents of an in-memory file |
| `lsdisk` | `lsdisk` | List files on the FAT16 disk |
| `catdisk` | `catdisk <filename>` | Display contents of a file from FAT16 disk |
| `sync` | `sync` | Flush the block cache to disk |
| `cachestats` | `cachestats` | Show block cache hit/miss statistics |

### Editor & Scripting

| Command | Usage | Description |
|---------|-------|-------------|
| `ed` | `ed [filename]` | Launch the ed line editor (optionally opening a file) |
| `cupid` | `cupid <script.cup> [args...]` | Run a CupidScript file |

Scripts can also be run as:
- `./script.cup [args]` — prefix with `./`
- `script.cup [args]` — just type the `.cup` filename

### Process Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `ps` | `ps` | List all processes with PID, state, and name |
| `kill` | `kill <pid>` | Terminate a process by PID (cannot kill PID 1) |
| `spawn` | `spawn [n]` | Create 1–16 test counting processes |
| `yield` | `yield` | Voluntarily yield CPU to the next process |

### Debug Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `memdump` | `memdump <hex_addr> [length]` | Hex + ASCII dump of memory (default 64 bytes, max 512) |
| `memstats` | `memstats` | Show heap and physical memory statistics |
| `memleak` | `memleak [seconds]` | Detect allocations older than threshold (default 60s) |
| `memcheck` | `memcheck` | Walk all heap blocks and verify canary integrity |
| `stacktrace` | `stacktrace` | Print current call stack (EBP frame chain) |
| `registers` | `registers` | Dump all general-purpose CPU registers + EFLAGS |
| `loglevel` | `loglevel [level]` | Get/set serial log level (`debug`/`info`/`warn`/`error`/`panic`) |
| `logdump` | `logdump` | Print the in-memory circular log buffer |
| `crashtest` | `crashtest <type>` | Test crash handling (see below) |

### Crash Test Types

| Type | What it does |
|------|-------------|
| `panic` | Trigger a kernel panic with a test message |
| `nullptr` | Dereference a NULL pointer (page fault) |
| `divzero` | Divide by zero (exception) |
| `assert` | Trigger an assertion failure |
| `overflow` | Overflow a heap buffer (detected on free by canary check) |
| `stackoverflow` | Allocate 64KB on stack (page fault) |

---

## Shell Features

### Tab Completion
- Type partial command name + Tab → completes command
- `cat ` + Tab → completes filenames from in-memory filesystem
- Multiple matches → lists all possibilities

### Command History
- **Up arrow** — Previous command
- **Down arrow** — Next command (or clear line)
- Up to 16 commands stored

### Input Editing
- **Backspace** — Delete last character
- Characters are echoed as typed

---

## Examples

```bash
# System info
> sysinfo
> time

# Files
> ls
> cat MOTD.txt
> lsdisk
> catdisk README.TXT

# Create and run a script
> ed hello.cup
a
echo "Hello from CupidScript!"
.
w
q
> cupid hello.cup

# Process management
> ps
> spawn 3
> ps
> kill 4

# Debugging
> memstats
> memcheck
> memdump 0xB8000 32
> loglevel debug
> logdump
```

---

## See Also

- [CupidScript](CupidScript) — Scripting language reference
- [Ed Editor](Ed-Editor) — Editor usage guide
- [Debugging](Debugging) — In-depth debugging guide
