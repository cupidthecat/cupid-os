# Shell Commands

cupid-os provides shell commands through a mix of built-in functions and auto-discovered CupidC programs in `/bin/`. The shell features command history (arrow up/down), tab completion for commands and filenames, current working directory tracking, prompt display showing the CWD, and ANSI terminal color support.

Commands marked with _(CupidC)_ are user programs in `/bin/` that are JIT-compiled when invoked. See [User Programs](User-Programs) for how to write your own.

---

## Command Reference

### System Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `help` | `help` | List all available commands _(CupidC)_ |
| `clear` | `clear` | Clear the screen _(CupidC)_ |
| `echo` | `echo <text>` | Print text to the terminal _(CupidC)_ |
| `echo` | `echo -c <color> <text>` | Print colored text (auto-resets) |
| `time` | `time` | Show system uptime _(CupidC)_ |
| `reboot` | `reboot` | Reboot the machine _(CupidC)_ |
| `history` | `history` | Show the last 16 commands entered _(CupidC)_ |
| `sysinfo` | `sysinfo` | Show uptime, CPU frequency, timer frequency, and memory usage _(CupidC)_ |
| `date` | `date [+epoch\|+short]` | Show current date and time from the RTC _(CupidC)_ |
| `cupidfetch` | `cupidfetch` | Show system info with ASCII art (includes date/time) |

### Filesystem Commands (VFS)

| Command | Usage | Description |
|---------|-------|-------------|
| `cd` | `cd [path]` | Change current working directory (supports `.` and `..`) _(CupidC)_ |
| `pwd` | `pwd` | Print current working directory _(CupidC)_ |
| `ls` | `ls [path]` | List files in the current directory or given path _(CupidC)_ |
| `cat` | `cat <file>` | Display file contents (relative to CWD or absolute) _(CupidC)_ |
| `mount` | `mount` | Show all mounted filesystems _(CupidC)_ |
| `mv` | `mv <source> <dest>` | Move/rename a file _(CupidC)_ |
| `exec` | `exec <path>` | Load and run a CUPD executable |

### Legacy Disk Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `sync` | `sync` | Flush the block cache to disk _(CupidC)_ |
| `cachestats` | `cachestats` | Show block cache hit/miss statistics _(CupidC)_ |

### Editor & Scripting

| Command | Usage | Description |
|---------|-------|-------------|
| `ed` | `ed [filename]` | Launch the ed line editor — CupidC program ([details](Ed-Editor)) |
| `cupid` | `cupid <script.cup> [args...]` | Run a CupidScript file |

Scripts can also be run as:
- `./script.cup [args]` — prefix with `./`
- `script.cup [args]` — just type the `.cup` filename

### Color Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `setcolor` | `setcolor <fg> [bg]` | Set terminal foreground (and optional background) color _(CupidC)_ |
| `resetcolor` | `resetcolor` | Reset colors to defaults (light gray on black) _(CupidC)_ |
| `printc` | `printc <fg> <text>` | Print text in a specific color (auto-resets) _(CupidC)_ |

Color values: 0=Black, 1=Blue, 2=Green, 3=Cyan, 4=Red, 5=Magenta, 6=Brown, 7=Light Gray, 8=Dark Gray, 9=Light Blue, 10=Light Green, 11=Light Cyan, 12=Light Red, 13=Light Magenta, 14=Yellow, 15=White

### Job Control Commands (CupidScript Builtins)

| Command | Usage | Description |
|---------|-------|-------------|
| `jobs` | `jobs [-l]` | List background jobs (with `-l` to show PIDs) |
| `declare` | `declare -A <name>` | Create an associative array |

### Process Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `ps` | `ps` | List all processes with PID, state, and name _(CupidC)_ |
| `kill` | `kill <pid>` | Terminate a process by PID (cannot kill PID 1) _(CupidC)_ |
| `spawn` | `spawn [n]` | Create 1–16 test counting processes _(CupidC)_ |
| `yield` | `yield` | Voluntarily yield CPU to the next process _(CupidC)_ |

### Debug Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `memdump` | `memdump <hex_addr> [length]` | Hex + ASCII dump of memory (default 64 bytes, max 512) _(CupidC)_ |
| `memstats` | `memstats` | Show heap and physical memory statistics _(CupidC)_ |
| `memleak` | `memleak [seconds]` | Detect allocations older than threshold (default 60s) _(CupidC)_ |
| `memcheck` | `memcheck` | Walk all heap blocks and verify canary integrity _(CupidC)_ |
| `stacktrace` | `stacktrace` | Print current call stack (EBP frame chain) _(CupidC)_ |
| `registers` | `registers` | Dump all general-purpose CPU registers + EFLAGS _(CupidC)_ |
| `loglevel` | `loglevel [level]` | Get/set serial log level (`debug`/`info`/`warn`/`error`/`panic`) _(CupidC)_ |
| `logdump` | `logdump` | Print the in-memory circular log buffer _(CupidC)_ |
| `crashtest` | `crashtest <type>` | Test crash handling (see below) _(CupidC)_ |

### Crash Test Types

| Type | What it does |
|------|-------------|
| `panic` | Trigger a kernel panic with a test message |
| `nullptr` | Dereference a NULL pointer (page fault) |
| `divzero` | Divide by zero (exception) |
| `assert` | Trigger an assertion failure |
| `overflow` | Overflow a heap buffer (detected on free by canary check) |
| `stackoverflow` | Allocate 64KB on stack (page fault) |

### Date & Time Commands

The `date` command reads the hardware Real-Time Clock (RTC) and displays the current date and time.

| Usage | Output |
|-------|--------|
| `date` | Full format: `Friday, February 6, 2026  6:51:58 PM` |
| `date +short` | Short format: `Feb 6  6:51 PM` |
| `date +epoch` | Seconds since Unix epoch (Jan 1, 1970): `1770393118` |

The `date` command is also available as a CupidScript built-in, so `$(date +epoch)` works for command substitution in scripts.

---

## Shell Features

### Terminal Colors

The terminal supports ANSI escape sequences for colored output. Colors work in both CupidScript scripts and interactive shell:

```bash
# Using built-in commands
setcolor 2            # Green text
echo "Success!"
resetcolor

# Colored echo
echo -c 4 "Error: something went wrong"

# Print with auto-reset
printc 14 "Warning: disk nearly full"
```

### I/O Redirection & Pipes

```bash
# Pipe output between commands
cat file.txt | grep error

# Redirect output to file
ls > filelist.txt
echo "append this" >> filelist.txt

# Redirect input from file
sort < unsorted.txt

# Redirect stderr
command 2> errors.log
command 2>&1            # Merge stderr into stdout
```

### Command Substitution

```bash
# Capture command output
FILES=$(ls /home)
echo "Files: $FILES"

# Backtick syntax
COUNT=`wc -l file.txt`
```

### Background Jobs

```bash
long_task &             # Run in background
echo "PID: $!"          # Last background PID
jobs                    # List background jobs
jobs -l                 # List with PIDs
```

### Tab Completion
- Type partial command name + Tab → completes command
- `cat ` + Tab → completes filenames from current directory
- Multiple matches → lists all possibilities

### Command History
- **Up arrow** — Previous command
- **Down arrow** — Next command (or clear line)
- Up to 16 commands stored

### Input Editing
- **Backspace** — Delete last character
- Characters are echoed as typed

### Prompt

The shell prompt shows the current working directory:

```
/home> _
```

Navigate with `cd` to change the prompt:

```
/home> cd /dev
/dev> cd /
/> _
```

---

## Examples

```bash
# System info
> sysinfo
> time

# Terminal colors
> setcolor 2
> echo "This text is green!"
> resetcolor
> echo -c 4 "This is red"
> printc 14 "This is yellow"

# Navigate the VFS
/> cd /home
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

# Show mount points
> mount
Mounted filesystems:
  /       ramfs
  /dev    devfs
  /home   fat16

# Create and write files
> ed /tmp/test/hello.txt
> cat /tmp/test/hello.txt

# Legacy disk access
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
