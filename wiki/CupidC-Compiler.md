# CupidC Compiler

CupidC is a HolyC-inspired C compiler built directly into the cupid-os kernel. It compiles `.cc` source files to native x86 machine code and runs them with full ring-0 privileges — no VM, no interpreter, no restrictions. Inspired by TempleOS's HolyC, CupidC embodies cupid-os's philosophy of complete user empowerment.

---

## Overview

| Feature | Details |
|---------|---------|
| Language | C-like (HolyC-inspired) |
| Target | x86 32-bit machine code |
| Compiler type | Single-pass recursive descent |
| Calling convention | cdecl |
| Execution modes | JIT (in-memory) and AOT (ELF32 binary) |
| Privilege level | Ring 0 — full system access |
| Source extension | `.cc` |
| Code size limit | 64 KB code, 16 KB data |
| Max functions | 128 |
| Max symbols | 256 |

---

## Getting Started

### JIT Mode — Compile and Run Instantly

```
> cupidc program.cc
```

Compiles the source to memory and executes immediately. No binary is saved to disk. Perfect for rapid development and testing.

### AOT Mode — Compile to ELF Binary

```
> ccc program.cc -o program
> exec program
```

Compiles the source to a persistent ELF32 binary on the FAT16 disk. The binary can be re-executed without recompilation.

If `-o` is omitted, the output name is derived from the source file (e.g., `program.cc` → `program`).

---

## Language Reference

### Types

| Type | Size | Description |
|------|------|-------------|
| `int` | 32-bit | Signed integer |
| `char` | 8-bit | Character / byte |
| `void` | — | No value (functions only) |
| `int*` | 32-bit | Pointer to int |
| `char*` | 32-bit | Pointer to char |

### Arrays

Stack-allocated fixed-size arrays:

```c
int arr[10];
char buf[256];
```

Array elements are accessed with `arr[i]` and can be assigned with `arr[i] = value`.

### Operators

Full C operator precedence is supported:

| Precedence | Operators | Description |
|-----------|-----------|-------------|
| 10 | `*` `/` `%` | Multiplication, division, modulo |
| 9 | `+` `-` | Addition, subtraction |
| 8 | `<<` `>>` | Bitwise shift |
| 7 | `<` `>` `<=` `>=` | Relational comparison |
| 6 | `==` `!=` | Equality comparison |
| 5 | `&` | Bitwise AND |
| 4 | `^` | Bitwise XOR |
| 3 | `\|` | Bitwise OR |
| 2 | `&&` | Logical AND |
| 1 | `\|\|` | Logical OR |

**Unary operators:** `!` (logical NOT), `~` (bitwise NOT), `-` (negate), `*` (dereference), `&` (address-of), `++` / `--` (increment/decrement)

**Assignment operators:** `=`, `+=`, `-=`, `*=`, `/=`

**Type casts:** `(int)expr`, `(char*)ptr`

### Control Flow

```c
// If/else
if (x > 5) {
    print("big");
} else {
    print("small");
}

// While loop
while (i < 10) {
    i++;
}

// For loop
for (int i = 0; i < 10; i++) {
    print_int(i);
}
```

`break` and `continue` work inside `while` and `for` loops.

### Functions

Functions use the cdecl calling convention. Up to 8 parameters per function.

```c
int add(int a, int b) {
    return a + b;
}

void main() {
    int result = add(5, 10);
    print_int(result);
}
```

Every program must have a `main()` function — it is the entry point.

Forward references are supported: functions can call other functions that are defined later in the file. The compiler resolves these after parsing is complete.

### String and Character Literals

```c
char* msg = "Hello, CupidOS!\n";
char ch = 'A';
```

Escape sequences: `\n` (newline), `\t` (tab), `\r` (carriage return), `\\` (backslash), `\'` (single quote), `\"` (double quote), `\0` (null).

String literals are stored in the data section and their address is loaded into registers.

### Comments

```c
// Line comments

/* Block
   comments */
```

### Inline Assembly

Direct x86 assembly inside CupidC functions:

```c
void disable_interrupts() {
    asm {
        cli;
        hlt;
    }
}
```

**Supported instructions:**

| Category | Instructions |
|----------|-------------|
| No operand | `cli`, `sti`, `hlt`, `nop`, `ret`, `iret`, `pushad`, `popad`, `cdq` |
| Register | `push`, `pop`, `inc`, `dec` |
| Reg, Reg/Imm | `mov`, `add`, `sub`, `xor`, `cmp` |
| I/O | `in al, dx` / `out dx, al` |
| Control | `call`, `int` |

Registers: `eax`, `ecx`, `edx`, `ebx`, `esp`, `ebp`, `esi`, `edi`, `al`, `cl`, `dl`, `bl`

---

## Kernel Bindings

CupidC programs can call kernel functions directly. These are pre-registered in the symbol table at compile time:

### Console Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `void print(char* s)` | Print string to screen |
| `println` | `void println(char* s)` | Print string + newline |
| `putchar` | `void putchar(char c)` | Print single character |
| `print_int` | `void print_int(int n)` | Print integer |
| `print_hex` | `void print_hex(int n)` | Print hex value |
| `clear_screen` | `void clear_screen()` | Clear the display |

### Memory Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `kmalloc` | `void* kmalloc(int size)` | Allocate memory (kernel heap) |
| `kfree` | `void kfree(void* ptr)` | Free allocated memory |

### String Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `strlen` | `int strlen(char* s)` | Get string length |
| `strcmp` | `int strcmp(char* a, char* b)` | Compare two strings |
| `strncmp` | `int strncmp(char* a, char* b, int n)` | Compare up to n characters |
| `memset` | `void* memset(void* p, int val, int n)` | Fill memory |
| `memcpy` | `void* memcpy(void* dst, void* src, int n)` | Copy memory |

### Port I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `outb` | `void outb(int port, int value)` | Write byte to I/O port |
| `inb` | `int inb(int port)` | Read byte from I/O port |

### File System

| Function | Signature | Description |
|----------|-----------|-------------|
| `vfs_open` | `int vfs_open(char* path, int flags)` | Open a file |
| `vfs_close` | `int vfs_close(int fd)` | Close a file |
| `vfs_read` | `int vfs_read(int fd, void* buf, int size)` | Read from file |
| `vfs_write` | `int vfs_write(int fd, void* buf, int size)` | Write to file |
| `vfs_seek` | `int vfs_seek(int fd, int offset, int whence)` | Seek within an open file |
| `vfs_stat` | `int vfs_stat(char* path, void* stat_buf)` | Get file info (size + type) |
| `vfs_readdir` | `int vfs_readdir(int fd, void* dirent)` | Read next directory entry |
| `vfs_mkdir` | `int vfs_mkdir(char* path)` | Create a directory |
| `vfs_unlink` | `int vfs_unlink(char* path)` | Delete a file |
| `vfs_rename` | `int vfs_rename(char* old, char* new)` | Move/rename a file (copy + delete) |

### Shell Integration

| Function | Signature | Description |
|----------|-----------|-------------|
| `get_args` | `char* get_args()` | Get command-line arguments from the shell |
| `get_cwd` | `char* get_cwd()` | Get current working directory |

### Process Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `yield` | `void yield()` | Yield CPU to scheduler |
| `exit` | `void exit()` | Terminate current process |
| `exec` | `int exec(char* path, char* args)` | Execute a program |

### Diagnostics

| Function | Signature | Description |
|----------|-----------|-------------|
| `memstats` | `void memstats()` | Print memory statistics |

---

## Examples

### Hello World

```c
void main() {
    println("Hello from CupidC!");
}
```

### Fibonacci

```c
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

void main() {
    for (int i = 0; i < 10; i++) {
        print_int(fib(i));
        print(" ");
    }
    print("\n");
}
```

### Working with Pointers

```c
void swap(int* a, int* b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

void main() {
    int x = 10;
    int y = 20;
    swap(&x, &y);
    print_int(x);   // 20
    print(" ");
    print_int(y);   // 10
    print("\n");
}
```

### Direct Hardware Access

```c
void main() {
    // Write 'A' to COM1 serial port
    outb(0x3F8, 65);

    // Read keyboard scancode
    int code = inb(0x60);
    print_hex(code);
    print("\n");
}
```

### Inline Assembly

```c
void main() {
    int result;

    asm {
        mov eax, 42;
    }

    // EAX now contains 42
    print("The answer is: ");
    print_int(42);
    print("\n");

    // Disable then re-enable interrupts
    asm {
        cli;
        sti;
    }
}
```

### Array Manipulation

```c
void main() {
    int arr[5];

    for (int i = 0; i < 5; i++) {
        arr[i] = i * i;
    }

    for (int i = 0; i < 5; i++) {
        print_int(arr[i]);
        print(" ");
    }
    print("\n");
}
```

### String Processing

```c
void main() {
    char* msg = "CupidOS";
    int len = strlen(msg);

    print("String: ");
    print(msg);
    print("\nLength: ");
    print_int(len);
    print("\n");
}
```

### File I/O

```c
void main() {
    int fd = vfs_open("/home/hello.txt", 0);
    if (fd < 0) {
        println("Cannot open file");
        return;
    }

    char buf[128];
    int n = vfs_read(fd, buf, 127);
    if (n > 0) {
        buf[n] = 0;
        println(buf);
    }

    vfs_close(fd);
}
```

---

## Compiler Architecture

### Pipeline

CupidC is a **single-pass** compiler — it lexes, parses, and emits x86 machine code in one pass through the source:

```
Source (.cc)
    │
    ▼
┌─────────────────┐
│  Lexer           │  cupidc_lex.c
│  (Tokenization)  │  Keywords, identifiers, literals, operators
└────────┬────────┘
         │ token stream
         ▼
┌─────────────────┐
│  Parser +        │  cupidc_parse.c
│  Code Generator  │  Recursive descent → x86 machine code
└────────┬────────┘
         │ raw bytes
         ▼
┌─────────────────┐
│  JIT: Execute    │  cupidc.c — copy to memory, jump to main()
│  AOT: Write ELF  │  cupidc_elf.c — emit ELF32 binary to disk
└─────────────────┘
```

### Source Files

| File | Lines | Role |
|------|-------|------|
| `cupidc.h` | ~214 | Header: token types, symbol table, compiler state, public API |
| `cupidc.c` | ~401 | Driver: JIT/AOT entry points, kernel bindings, state init |
| `cupidc_lex.c` | ~415 | Lexer: tokenizes source into keywords, literals, operators |
| `cupidc_parse.c` | ~1832 | Parser + x86 code generator: the core of the compiler |
| `cupidc_elf.c` | — | ELF32 binary writer for AOT mode |

### Lexer

The lexer (`cupidc_lex.c`) breaks source text into tokens:

- **Keywords:** `int`, `char`, `void`, `if`, `else`, `while`, `for`, `return`, `asm`, `break`, `continue`
- **Identifiers:** variable and function names
- **Literals:** decimal integers, hex integers (`0xFF`), strings (`"..."`), character literals (`'A'`)
- **Operators:** all arithmetic, comparison, logical, bitwise, assignment
- **Delimiters:** `( ) { } [ ] ; ,`

Skips whitespace and both `//` line comments and `/* */` block comments.

### Parser & Code Generator

The parser (`cupidc_parse.c`) is a **recursive descent** parser that directly emits x86 machine code bytes into a code buffer. There is no AST or intermediate representation.

**Key parsing functions:**

| Function | Purpose |
|----------|---------|
| `cc_parse_program()` | Top-level: parse functions and globals |
| `cc_parse_function()` | Function definition with prologue/epilogue |
| `cc_parse_statement()` | Statement dispatch (if, while, for, return, etc.) |
| `cc_parse_expression()` | Operator precedence climbing |
| `cc_parse_primary()` | Atoms: numbers, strings, identifiers, unary ops |
| `cc_parse_asm_block()` | Inline assembly parsing and encoding |

**Code generation pattern:**

- Expressions evaluate into `EAX`
- Binary operations: push left to stack, evaluate right into `EAX`, pop left into `EBX`, emit operation
- Function calls: push arguments, `call`, clean stack
- Locals accessed via `[EBP - offset]`, params via `[EBP + offset]`

### Symbol Table

Symbols are stored in a flat array (max 256 entries), searched linearly from the end so that locals shadow globals:

| Kind | Description | Storage |
|------|-------------|---------|
| `SYM_LOCAL` | Local variable | `[EBP - offset]` |
| `SYM_PARAM` | Function parameter | `[EBP + offset]` |
| `SYM_FUNC` | User-defined function | Code offset |
| `SYM_KERNEL` | Kernel binding | Absolute address |
| `SYM_GLOBAL` | Global variable | Data section address |

### Memory Layout

```
JIT Mode:
  0x00400000 - 0x004FFFFF  Code region (up to 64KB used)
  0x00500000 - 0x005FFFFF  Data region (strings, globals)

AOT Mode:
  0x00200000+               ELF load address
  Code and data packed into ELF segments
```

### Forward References

When the parser encounters a call to an undefined function, it emits a placeholder `call` instruction and records a **patch entry**. After the entire program is parsed, `cc_parse_program()` resolves all forward references by patching the `call` targets with the correct addresses.

---

## Limitations

- **64 KB code limit** and **16 KB data limit** per program
- **256 symbols** maximum (functions + variables + kernel bindings)
- **8 parameters** maximum per function
- **No structs or unions** — only primitive types and pointers
- **No preprocessor** (`#include`, `#define` not supported)
- **No multi-file compilation** — single source file only
- **No floating point** — integer arithmetic only
- **No standard library** — only kernel bindings are available
- **Global initializers** are not yet fully implemented
- **Limited optimization** — single-pass compilation with no optimization passes

---

## Shell Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `cupidc` | `cupidc <file.cc>` | JIT compile and execute a CupidC source file |
| `ccc` | `ccc <file.cc> -o <output>` | AOT compile to ELF32 binary |

---

## User Programs (TempleOS-style)

CupidC programs can be placed in `/bin/` or `/home/bin/` and the shell discovers them automatically — no kernel recompile needed. Just type the program name and it runs.

### How It Works

When you type a command the shell doesn't recognize, it searches in order:

1. `/bin/<cmd>` — ELF/CUPD binary in ramfs
2. `/bin/<cmd>.cc` — CupidC source in ramfs (JIT compiled)
3. `/home/bin/<cmd>` — ELF binary on disk
4. `/home/bin/<cmd>.cc` — CupidC source on disk (JIT compiled)

CupidC `.cc` files are JIT-compiled and run instantly. No build step.

### Example: `mv` (Move/Rename Files)

The `mv` command is a CupidC user program at `/bin/mv.cc`:

```
> mv old.txt new.txt        # rename a file
> mv report.txt /tmp/       # move into a directory
> mv /home/a.txt /home/b/   # absolute paths
```

Source (`bin/mv.cc`):

```c
// mv.cc - move/rename files for CupidOS

void resolve_path(char *out, char *path) {
    int i = 0;
    if (path[0] == '/') {
        while (path[i]) { out[i] = path[i]; i = i + 1; }
        out[i] = 0;
        return;
    }
    char *cwd = (char*)get_cwd();
    int ci = 0;
    while (cwd[ci]) { out[i] = cwd[ci]; i = i + 1; ci = ci + 1; }
    if (i > 1) { out[i] = '/'; i = i + 1; }
    int pi = 0;
    while (path[pi]) { out[i] = path[pi]; i = i + 1; pi = pi + 1; }
    out[i] = 0;
}

void main() {
    char *args = (char*)get_args();
    // ... parse source and dest, resolve paths,
    // check if dest is a directory, call vfs_rename()
}
```

Key patterns used:
- `get_args()` — retrieve shell arguments
- `get_cwd()` — resolve relative paths
- `vfs_stat()` — check if destination is a directory
- `vfs_rename()` — perform the move

### Writing Your Own Program

See the [User Programs](User-Programs) page for a complete guide.

---

## Workflow

1. Write a `.cc` source file using the `ed` editor:
   ```
   > ed /home/hello.cc
   a
   void main() {
       println("Hello from CupidC!");
   }
   .
   w
   q
   ```

2. Run it with JIT compilation:
   ```
   > cupidc /home/hello.cc
   Hello from CupidC!
   ```

3. Or compile to a persistent binary:
   ```
   > ccc /home/hello.cc -o /home/hello
   Compiled: 42 bytes code, 20 bytes data
   Written to /home/hello
   > exec /home/hello
   Hello from CupidC!
   ```

---

## Comparison with HolyC

CupidC draws direct inspiration from TempleOS's HolyC:

| Feature | HolyC (TempleOS) | CupidC (cupid-os) |
|---------|-------------------|--------------------|
| Execution | JIT compiled | JIT + AOT (ELF) |
| Architecture | x86-64 | x86-32 |
| Privilege | Ring 0 | Ring 0 |
| Types | Full C types + classes | int, char, void, pointers, arrays |
| Inline ASM | Yes | Yes |
| Port I/O | Direct access | `inb()`/`outb()` builtins |
| Hardware access | Full | Full |
| Structs | Yes | Not yet |
| Classes | Yes | No |
| Preprocessor | `#include` | No |
