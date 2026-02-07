# Ed Editor

cupid-os includes a faithful implementation of the classic Unix `ed(1)` line editor, written entirely in CupidC. Ed operates in-memory and can read/write files through the VFS (both ramfs and FAT16 disk).

## Implementation

Ed is implemented as a CupidC program at `bin/ed.cc`. It is automatically embedded into the kernel image at build time and is available as `/bin/ed.cc` in the in-memory filesystem. When you type `ed` at the shell prompt, the auto-discovery system finds `/bin/ed.cc` and JIT-compiles it.

Source: `bin/ed.cc` (~900 lines of CupidC)

---

## Starting Ed

```bash
> ed                    # Start with empty buffer
> ed LICENSE.txt        # Open file from in-memory filesystem
> ed README.TXT         # Open file from FAT16 disk
```

---

## Quick Start

### Create a File

```
> ed
a                       # Enter append mode
Hello, CupidOS!
This is my first file.
Line three.
.                       # End input mode (dot on its own line)
w myfile.txt            # Write to disk (prints byte count)
48
q                       # Quit
```

### Edit an Existing File

```
> ed myfile.txt
3                       # Print line count (shows "3")
1,3p                    # Print lines 1–3
Hello, CupidOS!
This is my first file.
Line three.
2s/first/second/        # Substitute on line 2
2p                      # Verify
This is my second file.
w                       # Save
q                       # Quit
```

---

## Modes

Ed has two modes:

1. **Command mode** (default) — Enter commands to navigate, edit, display, and save
2. **Input mode** — Enter text line by line; exit by typing `.` alone on a line

Commands that enter input mode: `a` (append), `i` (insert), `c` (change).

---

## Address Forms

Most commands accept an address or address range to specify which lines to operate on.

| Address | Meaning |
|---------|---------|
| `3` | Line 3 |
| `.` | Current line |
| `$` | Last line |
| `+` / `-` | Relative to current line |
| `+3` / `-2` | Offset from current line |
| `/pattern/` | Next line matching pattern (forward search) |
| `?pattern?` | Previous line matching pattern (backward search) |
| `'x` | Line marked with `kx` |
| `%` | All lines (shorthand for `1,$`) |
| `1,5` | Lines 1 through 5 |
| `3,` | Line 3 through current |
| `,7` | Current through line 7 |

---

## Commands

### Display

| Command | Description |
|---------|-------------|
| `p` | Print current line (or addressed lines) |
| `n` | Print with line numbers |
| `l` | Print with escape characters visible |
| `=` | Print number of lines in buffer |

### Input Mode

| Command | Description |
|---------|-------------|
| `a` | Append text after addressed line |
| `i` | Insert text before addressed line |
| `c` | Change (replace) addressed lines |

End input mode by typing `.` on a line by itself.

### Editing

| Command | Description |
|---------|-------------|
| `d` | Delete addressed lines |
| `j` | Join addressed lines into one |
| `m <addr>` | Move lines to after address |
| `t <addr>` | Copy lines to after address |
| `s/pat/repl/` | Substitute first match on addressed lines |
| `s/pat/repl/g` | Substitute all matches |
| `s/pat/repl/gp` | Substitute all and print |
| `u` | Undo last change (single level) |

### File Operations

| Command | Description |
|---------|-------------|
| `e <file>` | Edit file (replaces buffer, warns if unsaved) |
| `E <file>` | Edit file (no warning) |
| `r <file>` | Read file and append to buffer |
| `w [file]` | Write buffer to file (prints byte count) |
| `wq` | Write and quit |
| `W [file]` | Append buffer to file |
| `f [name]` | Get or set current filename |

Files are read and written through the VFS layer, supporting both the in-memory filesystem (ramfs) and FAT16 disk.

### Search

| Command | Description |
|---------|-------------|
| `/pattern/` | Search forward for pattern |
| `?pattern?` | Search backward for pattern |

### Global Commands

| Command | Description |
|---------|-------------|
| `g/RE/cmd` | Execute command on all lines matching regex |
| `v/RE/cmd` | Execute command on all lines NOT matching regex |

### Other

| Command | Description |
|---------|-------------|
| `k<x>` | Mark current line with letter x (a–z) |
| `H` | Toggle verbose error messages |
| `h` | Show last error message |
| `q` | Quit (warns if unsaved) |
| `Q` | Quit (no warning) |

---

## Regex

Ed supports basic regular expressions:

| Pattern | Meaning |
|---------|---------|
| `.` | Any single character |
| `*` | Zero or more of the preceding character |
| `^` | Anchor to start of line |
| `$` | Anchor to end of line |
| literal | Matches itself |

### Examples

```
/hello/           # Find next line containing "hello"
?error?           # Find previous line containing "error"
s/^/    /         # Indent current line (add 4 spaces)
s/world/CupidOS/  # Replace "world" with "CupidOS"
g/TODO/p          # Print all lines containing "TODO"
v/^#/d            # Delete all lines NOT starting with #
```

---

## Example Session: Creating a CupidScript

```
> ed
a
#!/bin/cupid
# A simple counter script
COUNT=0
while [ $COUNT -lt 5 ]; do
    echo "Count: $COUNT"
    COUNT=$((COUNT + 1))
done
echo "Done!"
.
1,9n
1	#!/bin/cupid
2	# A simple counter script
3	COUNT=0
4	while [ $COUNT -lt 5 ]; do
5	    echo "Count: $COUNT"
6	    COUNT=$((COUNT + 1))
7	done
8	echo "Done!"
w counter.cup
98
q
> cupid counter.cup
Count: 0
Count: 1
Count: 2
Count: 3
Count: 4
Done!
```

---

## Limits

| Limit | Value |
|-------|-------|
| Maximum lines | 1024 |
| Maximum line length | 256 characters |
| Undo levels | 1 (single undo) |
| Marks | 26 (a–z) |

---

## See Also

- [CupidScript](CupidScript) — Write scripts to run with `cupid`
- [Shell Commands](Shell-Commands) — All shell commands including `ed`
- [Filesystem](Filesystem) — File I/O and FAT16 details
