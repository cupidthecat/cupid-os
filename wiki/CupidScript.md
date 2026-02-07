# CupidScript

CupidScript is a bash-like scripting language built into cupid-os. Scripts use the `.cup` file extension and support variables, conditionals, loops, functions, arithmetic, and comments. Scripts can call any built-in shell command.

---

## Running Scripts

There are three ways to run a CupidScript file:

```bash
# Method 1: cupid command
cupid script.cup arg1 arg2

# Method 2: ./ prefix
./script.cup arg1 arg2

# Method 3: just the filename
script.cup arg1 arg2
```

Scripts can be created using the `ed` editor and saved to the FAT16 disk.

---

## Creating Your First Script

```bash
> ed hello.cup
a
#!/bin/cupid
NAME=CupidOS
echo "Hello from $NAME!"
.
w
q
> cupid hello.cup
Hello from CupidOS!
```

---

## Language Reference

### Variables

```bash
# Assignment (no spaces around =)
NAME=frank
COUNT=42
GREETING=hello

# Variable expansion with $
echo $NAME                    # frank
echo "Hello $NAME"            # Hello frank

# Empty/undefined variables expand to empty string
echo $UNDEFINED               # (prints nothing)
```

### Special Variables

| Variable | Meaning |
|----------|---------|
| `$?` | Exit status of the last command (0 = success) |
| `$#` | Number of arguments passed to the script |
| `$0` | Name of the script file |
| `$1` – `$9` | Positional arguments |
| `$!` | PID of the last background process |
| `$EPOCHSECONDS` | Seconds since Unix epoch (Jan 1, 1970) from the RTC |

```bash
#!/bin/cupid
echo "Script: $0"
echo "Args: $#"
echo "First: $1"
echo "Second: $2"
```

Running `cupid test.cup hello world` produces:
```
Script: test.cup
Args: 2
First: hello
Second: world
```

### Strings

```bash
# Double-quoted strings (variable expansion occurs)
echo "Hello $NAME"

# Single-quoted strings (no expansion, literal)
echo 'Hello $NAME'            # Hello $NAME

# Escape sequences in double quotes
echo "Line 1\nLine 2"
```

### Comments

```bash
# This is a comment
echo "Hello"   # Inline comment

#!/bin/cupid   # Shebang line (ignored during execution)
```

---

## Conditionals

### If / Then / Fi

```bash
X=10
if [ $X -eq 10 ]; then
    echo "X is ten"
fi
```

### If / Else

```bash
NAME=alice
if [ $NAME = "alice" ]; then
    echo "Welcome Alice"
else
    echo "Welcome stranger"
fi
```

### Test Operators

#### Numeric Comparisons

| Operator | Meaning |
|----------|---------|
| `-eq` | Equal |
| `-ne` | Not equal |
| `-lt` | Less than |
| `-gt` | Greater than |
| `-le` | Less than or equal |
| `-ge` | Greater than or equal |

#### String Comparisons

| Operator | Meaning |
|----------|---------|
| `=` | Strings are equal |
| `!=` | Strings are not equal |

#### String Tests

| Operator | Meaning |
|----------|---------|
| `-z` | String has zero length |
| `-n` | String has non-zero length |

```bash
# Numeric
if [ $COUNT -gt 5 ]; then
    echo "Count is greater than 5"
fi

# String
if [ $NAME = "frank" ]; then
    echo "Hello Frank"
fi

# Zero length test
if [ -z $EMPTY ]; then
    echo "Variable is empty"
fi
```

---

## Loops

### For Loop

Iterate over a word list:

```bash
for i in 1 2 3 4 5; do
    echo "Number: $i"
done
```

Output:
```
Number: 1
Number: 2
Number: 3
Number: 4
Number: 5
```

### While Loop

```bash
COUNT=0
while [ $COUNT -lt 5 ]; do
    echo "Count is $COUNT"
    COUNT=$((COUNT + 1))
done
```

Output:
```
Count is 0
Count is 1
Count is 2
Count is 3
Count is 4
```

> **Safety limit:** While loops are capped at 10,000 iterations to prevent infinite loops from hanging the system.

---

## Functions

### Defining and Calling

```bash
# Define a function
greet() {
    echo "Hello $1"
}

# Call it
greet World
greet Frank
greet CupidOS
```

Output:
```
Hello World
Hello Frank
Hello CupidOS
```

### Arguments in Functions

Inside a function, `$1`, `$2`, etc. refer to the function's arguments (not the script's). They are restored after the function returns.

```bash
show_args() {
    echo "Function got $# args"
    echo "First: $1"
    echo "Second: $2"
}

show_args apple banana
```

### Return Values

```bash
is_positive() {
    if [ $1 -gt 0 ]; then
        return 0
    else
        return 1
    fi
}

is_positive 5
echo "Exit status: $?"    # 0

is_positive -3
echo "Exit status: $?"    # 1
```

---

## Arithmetic

CupidScript supports integer arithmetic via `$((expression))`:

```bash
X=10
Y=3

SUM=$((X + Y))          # 13
DIFF=$((X - Y))         # 7
PROD=$((X * Y))         # 30
QUOT=$((X / Y))         # 3
MOD=$((X % Y))          # 1

echo "Sum: $SUM"
echo "Diff: $DIFF"
```

Arithmetic is used most commonly in while loops for counting:

```bash
I=0
while [ $I -lt 10 ]; do
    echo $I
    I=$((I + 1))
done
```

---

## Calling Shell Commands

Any built-in shell command can be called from a CupidScript:

```bash
#!/bin/cupid
echo "=== System Info ==="
sysinfo
echo ""
echo "=== Files ==="
ls
echo ""
echo "=== Disk Files ==="
lsdisk
echo ""
echo "=== Memory ==="
memstats
```

---

## Terminal Colors

CupidScript supports colored terminal output through both ANSI escape codes and built-in color functions.

### Color Constants

| Value | Color | Value | Color |
|-------|-------|-------|-------|
| 0 | Black | 8 | Dark Gray |
| 1 | Blue | 9 | Light Blue |
| 2 | Green | 10 | Light Green |
| 3 | Cyan | 11 | Light Cyan |
| 4 | Red | 12 | Light Red |
| 5 | Magenta | 13 | Light Magenta |
| 6 | Brown | 14 | Yellow |
| 7 | Light Gray | 15 | White |

### Built-in Color Commands

```bash
# Set foreground color (persists for subsequent output)
setcolor 4              # Red text
setcolor 15 4           # White text on red background

# Reset to default colors (light gray on black)
resetcolor

# Print colored text (auto-resets after)
printc 2 "Success!"     # Green text

# Echo with color flag (auto-resets after)
echo -c 4 "Error: file not found"    # Red text
```

### ANSI Escape Codes

CupidScript also processes raw ANSI escape sequences in output:

| Sequence | Effect |
|----------|--------|
| `\e[0m` | Reset to defaults |
| `\e[1m` | Bold (bright foreground) |
| `\e[30m` – `\e[37m` | Foreground color |
| `\e[40m` – `\e[47m` | Background color |
| `\e[90m` – `\e[97m` | Bright foreground |
| `\e[2J` | Clear screen |
| `\e[H` | Move cursor to home |

### Example: Colored Output

```bash
#!/bin/cupid
setcolor 2
echo "=== System Status ==="
resetcolor

echo -c 11 "CPU: OK"
echo -c 10 "Memory: OK"
printc 14 "Disk: Warning - 90% full"
echo -c 4 "Network: Error"

resetcolor
echo "Done."
```

---

## I/O Redirection & Pipes

### Pipes

Connect the output of one command to the input of another:

```bash
cat file.txt | grep error
ls | sort
```

### Output Redirection

```bash
echo "hello" > output.txt      # Write (overwrite)
echo "more" >> output.txt      # Append
```

### Input Redirection

```bash
sort < unsorted.txt
```

### Error Redirection

```bash
command 2> errors.log          # Redirect stderr to file
command 2>&1                   # Redirect stderr to stdout
```

---

## Command Substitution

Capture the output of a command into a variable:

```bash
# Modern $() syntax
FILES=$(ls /home)
echo "Files: $FILES"

# Traditional backtick syntax
COUNT=`wc -l file.txt`
echo "Lines: $COUNT"

# Nested substitution
RESULT=$(echo $(expr 1 + 2))
```

---

## Background Jobs

Run commands in the background without blocking:

```bash
long_task &                   # Run in background
echo "Task started with PID $!"

# List background jobs
jobs
jobs -l                       # Include PIDs
```

### Special Variable

| Variable | Meaning |
|----------|---------|
| `$!` | PID of the last background process |

---

## Date & Time

The `date` command is a CupidScript built-in that reads the hardware RTC.

### Formats

```bash
# Full date and time (default)
date
# Output: Friday, February 6, 2026  6:51:58 PM

# Short format
date +short
# Output: Feb 6  6:51 PM

# Unix epoch seconds
date +epoch
# Output: 1770393118
```

### Command Substitution

```bash
#!/bin/cupid
# Capture date output into a variable
TODAY=$(date +short)
echo "Today is: $TODAY"

# Use epoch for timestamps
START=$EPOCHSECONDS
echo "Script started at epoch: $START"
```

### `$EPOCHSECONDS` Variable

The special variable `$EPOCHSECONDS` expands to the current Unix timestamp (seconds since January 1, 1970) read from the hardware RTC. Unlike `$(date +epoch)`, it does not require command substitution — it expands inline like any other variable.

```bash
#!/bin/cupid
echo "Current epoch: $EPOCHSECONDS"

# Use in arithmetic
START=$EPOCHSECONDS
# ... do work ...
END=$EPOCHSECONDS
ELAPSED=$(( END - START ))
echo "Elapsed: $ELAPSED seconds"
```

---

## Arrays

### Regular Arrays

```bash
# Create and access
arr[0]="first"
arr[1]="second"
arr[2]="third"

echo ${arr[0]}                # first
echo ${arr[@]}                # All elements
echo ${#arr[@]}               # Length: 3
```

### Associative Arrays

```bash
# Declare and populate
declare -A config
config[host]="localhost"
config[port]="8080"

echo ${config[host]}          # localhost
echo ${config[port]}          # 8080
```

---

## Advanced String Operations

### String Length

```bash
name="hello"
echo ${#name}                 # 5
```

### Substring

```bash
text="hello world"
echo ${text:0:5}              # hello
echo ${text:6}                # world
```

### Suffix Removal

```bash
file="document.txt"
echo ${file%.txt}             # document
echo ${file%.*}               # document
```

### Prefix Removal

```bash
path="/home/user/file.txt"
echo ${path##*/}              # file.txt
echo ${path#*/}               # home/user/file.txt
```

### Replacement

```bash
text="hello world"
echo ${text/world/universe}   # hello universe
```

### Case Conversion

```bash
name="Hello World"
echo ${name^^}                # HELLO WORLD
echo ${name,,}                # hello world
echo ${name^}                 # Hello world (capitalize first)
```

---

## Complete Examples

### 1. Greeter Script (`greet.cup`)

```bash
#!/bin/cupid
# Greet multiple people
greet() {
    echo "Hello $1! Welcome to CupidOS."
}

for name in Alice Bob Charlie; do
    greet $name
done
```

### 2. Countdown (`countdown.cup`)

```bash
#!/bin/cupid
COUNT=10
echo "Countdown:"
while [ $COUNT -gt 0 ]; do
    echo "$COUNT..."
    COUNT=$((COUNT - 1))
done
echo "Liftoff!"
```

### 3. Argument Echo (`args.cup`)

```bash
#!/bin/cupid
echo "Script: $0"
echo "You passed $# arguments:"
I=1
for arg in $1 $2 $3 $4 $5; do
    if [ -n $arg ]; then
        echo "  Arg $I: $arg"
        I=$((I + 1))
    fi
done
```

### 4. FizzBuzz (`fizzbuzz.cup`)

```bash
#!/bin/cupid
I=1
while [ $I -le 20 ]; do
    MOD3=$((I % 3))
    MOD5=$((I % 5))
    if [ $MOD3 -eq 0 ]; then
        if [ $MOD5 -eq 0 ]; then
            echo "FizzBuzz"
        else
            echo "Fizz"
        fi
    else
        if [ $MOD5 -eq 0 ]; then
            echo "Buzz"
        else
            echo $I
        fi
    fi
    I=$((I + 1))
done
```

### 5. System Report (`report.cup`)

```bash
#!/bin/cupid
echo "=============================="
echo "  CupidOS System Report"
echo "=============================="
echo ""
date
echo ""
time
echo ""
echo "--- Memory ---"
memstats
echo ""
echo "--- Processes ---"
ps
echo ""
echo "--- Disk Files ---"
lsdisk
echo ""
echo "--- Cache ---"
cachestats
echo "=============================="
echo "  Report complete."
echo "=============================="
```

### 6. Colored Dashboard (`dashboard.cup`)

```bash
#!/bin/cupid
setcolor 15
echo "╔══════════════════════════╗"
echo "║   CupidOS Dashboard     ║"
echo "╚══════════════════════════╝"
resetcolor

echo -c 10 "✓ Kernel loaded"
echo -c 10 "✓ Filesystem mounted"
echo -c 14 "⚠ Disk usage high"

setcolor 11
echo ""
echo "Uptime:"
resetcolor
time

echo ""
printc 6 "Process List:"
ps
```

### 7. String Processing (`strings.cup`)

```bash
#!/bin/cupid
FILE="document.backup.txt"

# Get the extension
echo "File: $FILE"
echo "Without extension: ${FILE%.*}"
echo "Extension only: ${FILE##*.}"

# Case conversion
NAME="hello world"
echo "Upper: ${NAME^^}"
echo "Lower: ${NAME,,}"
echo "Length: ${#NAME}"

# Substring
echo "First 5: ${NAME:0:5}"
```

---

## Technical Details

### Execution Pipeline

1. **Lexer** (`cupidscript_lex.c`) — Breaks source into tokens: keywords, words, strings, variables, operators, arithmetic expressions, pipes, redirections, background operators
2. **Parser** (`cupidscript_parse.c`) — Builds an Abstract Syntax Tree (AST) from the token stream
3. **Interpreter** (`cupidscript_exec.c`) — Walks the AST, executes commands, evaluates tests, manages control flow, handles pipelines and color builtins
4. **Runtime** (`cupidscript_runtime.c`) — Variable storage, function registry, `$VAR` expansion engine, `${}` advanced string operations, command substitution
5. **Streams** (`cupidscript_streams.c`) — File descriptor table, pipe creation, buffer I/O, stream redirection
6. **Display** (`terminal_ansi.c`, `shell.c`, `terminal_app.c`) — ANSI escape parsing, per-character color tracking, colored rendering

### Limits

| Limit | Value |
|-------|-------|
| Variables | 128 |
| Functions | 32 |
| Variable name length | 64 chars |
| Variable value length | 256 chars |
| Tokens per script | 2048 |
| Arguments per command | 32 |
| For loop word list | 64 items |
| While loop iterations | 10,000 |
| Script arguments | 16 |
| Expanded string length | 512 chars |
| Regular arrays | 32 |
| Array elements | 256 per array |
| Associative arrays | 16 |
| Assoc array entries | 128 per array |
| File descriptors | 16 per context |
| Background jobs | 32 |
| Pipeline commands | 8 |

### Current Limitations

- No globbing (`*.txt` expansion)
- Arithmetic supports only two operands per expression
- No `case`, `until`, `break`, or `continue` statements
- No heredocs (`<<EOF`)
- No process substitution (`<(cmd)`)

### Removed Limitations (Now Implemented)

The following features were previously listed as limitations and have since been implemented:

- ~~No pipes (`|`)~~ → Full pipeline support with `|` operator
- ~~No I/O redirection (`>`, `<`, `>>`)~~ → Output, input, append, and error redirection
- ~~No command substitution~~ → Both `$(cmd)` and `` `cmd` `` syntax
- ~~No arrays or associative arrays~~ → Regular arrays and `declare -A` associative arrays
- ~~No background jobs (`&`)~~ → Background execution with job table tracking

---

## See Also

- [Shell Commands](Shell-Commands) — All shell commands
- [Ed Editor](Ed-Editor) — Create and edit `.cup` files
- [Architecture](Architecture) — CupidScript pipeline in context
