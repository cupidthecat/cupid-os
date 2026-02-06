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

---

## Technical Details

### Execution Pipeline

1. **Lexer** (`cupidscript_lex.c`) — Breaks source into tokens: keywords, words, strings, variables, operators, arithmetic expressions
2. **Parser** (`cupidscript_parse.c`) — Builds an Abstract Syntax Tree (AST) from the token stream
3. **Interpreter** (`cupidscript_exec.c`) — Walks the AST, executes commands, evaluates tests, manages control flow
4. **Runtime** (`cupidscript_runtime.c`) — Variable storage, function registry, `$VAR` expansion engine

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

### Current Limitations

- No pipes (`|`)
- No I/O redirection (`>`, `<`, `>>`)
- No command substitution (`` `cmd` `` or `$(cmd)`)
- No arrays or associative arrays
- No background jobs (`&`)
- No globbing (`*.txt` expansion)
- Arithmetic supports only two operands per expression
- No `case`, `until`, `break`, or `continue` statements

---

## See Also

- [Shell Commands](Shell-Commands) — All 24 shell commands
- [Ed Editor](Ed-Editor) — Create and edit `.cup` files
- [Architecture](Architecture) — CupidScript pipeline in context
