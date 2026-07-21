# CupidC Compiler

CupidC is a HolyC-inspired C compiler built into the cupid-os kernel. It compiles `.cc` source files to native x86 machine code. Programs run directly in ring 0 without a virtual machine or interpreter.

---

## Overview

| Feature | Details |
|---------|---------|
| Language | C-like (HolyC-inspired) |
| Target | x86 32-bit machine code |
| Compiler type | Single-pass recursive descent |
| Calling convention | cdecl |
| Execution modes | JIT (in-memory) and AOT (ELF32 binary) |
| Privilege level | Ring 0 - full system access |
| Source extension | `.cc` |
| Code/data limit | 1 MB code buffer, 8 MB data/string buffer |
| Max source file | 256 KB |
| Max functions | 1024 |
| Max symbols | 4096 |
| Max structs | 64 (up to 32 fields each) |

---

## Getting Started

### JIT Mode - Compile and Run Instantly

```
> cupidc program.cc
```

CupidC compiles the source into memory and executes it without saving a binary. This mode is useful for short edit-and-test cycles.

### AOT Mode - Compile to ELF Binary

```
> ccc program.cc -o program
> exec program
```

Compiles the source to a persistent ELF32 binary on the FAT16 disk. The binary can be re-executed without recompilation.

If `-o` is omitted, the output name is derived from the source file (e.g., `program.cc` -> `program`).

---

## Language Reference

### Types

| Type | Size | Description |
|------|------|-------------|
| `int` | 32-bit | Signed integer |
| `char` | 8-bit | Character / byte |
| `bool` | 32-bit | Boolean (alias for int) |
| `U0` | - | HolyC-style `void` spelling |
| `U8`, `I8` | 8-bit | Unsigned/signed byte spellings |
| `U16`, `I16` | 16-bit | Unsigned/signed word spellings |
| `U32`, `I32` | 32-bit | Unsigned/signed dword spellings |
| `U64`, `I64` | parsed | Accepted C/HolyC compatibility spellings; current codegen remains 32-bit |
| `long`, `short`, `signed`, `unsigned` | parsed | Accepted C compatibility spellings; width is normalized by the 32-bit codegen |
| `float`, `double` | 32/64-bit | SSE scalar floating point |
| `float4`, `double2` | 128-bit | SSE vector types |
| `void` | - | No value (functions only) |
| `int*` | 32-bit | Pointer to int |
| `char*` | 32-bit | Pointer to char |
| `struct` | varies | User-defined composite type |
| `struct*` | 32-bit | Pointer to struct |

### Arrays

Fixed-size arrays, both local (stack-allocated) and global (data section):

```c
// Global arrays - stored in data section
int scores[100];
char buffer[256];

void main() {
    // Local arrays - stack-allocated
    int arr[10];
    char buf[64];

    arr[0] = 42;
    buf[0] = 'A';
}
```

Array elements are accessed with `arr[i]` and can be assigned with `arr[i] = value`.

Compound assignment also works: `arr[i] += value`, `arr[i] -= value`, `arr[i] *= value`, `arr[i] /= value`.

Array bounds at file scope and inside structs accept constant integer
expressions, including enum values and simple arithmetic. That keeps
feature-test code like `int table[BASE + EXTRA];` source-compatible
with normal C examples without adding a full C preprocessor.

### Structs

User-defined composite types with named fields:

```c
struct Point {
    int x;
    int y;
};

struct Rect {
    struct Point origin;
    int width;
    int height;
};

void main() {
    struct Point p;
    p.x = 10;
    p.y = 20;
    print_int(p.x);

    // Heap-allocated structs via pointer
    struct Point *hp = kmalloc(sizeof(struct Point));
    hp->x = 100;
    hp->y = 200;
    print_int(hp->x);
    kfree(hp);
}
```

**Struct features:**
- Up to 64 named struct types, each with up to 32 fields
- Field types: `int`, `char`, `void*`, `int*`, `char*`, nested `struct`
- Stack-allocated structs (`struct Foo s;`) are zero-initialized
- Heap-allocated structs via `kmalloc(sizeof(struct Foo))`
- Member access with `.` (value) and `->` (pointer)
- Chained access: `rect.origin.x`, `ptr->origin.y`
- All fields are 4-byte aligned for x86 compatibility

### sizeof Operator

Compute the size of a type at compile time:

```c
int a = sizeof(int);           // 4
int b = sizeof(char);          // 1
int c = sizeof(struct Point);  // 8 (two ints)

struct Foo *p = kmalloc(sizeof(struct Foo));
```

### Enumerations

Define named integer constants:

```c
enum {
    RED,        // 0
    GREEN,      // 1
    BLUE        // 2
};

enum Colors {
    BLACK = 0,
    WHITE = 15,
    YELLOW = 14
};

void main() {
    int color = RED;       // 0
    int bg = WHITE;        // 15
    print_int(color);
}
```

Enum values are stored as global integers in the data section. Values auto-increment from 0, or can be set explicitly with `= value` (including negative values).

Enum initializers also accept constant integer expressions, so later
declarations can use enum-derived bounds or bit flags.

### C Compatibility Tokens

CupidC is a small single-pass compiler that accepts several common C spellings
used by larger examples and demos:

- storage/qualifier spellings: `extern`, `inline`, `register`, `restrict`, `const`, `volatile`
- wide type spellings: `long`, `short`, `signed`, `unsigned`, `long long`
- attributes: `__attribute__((...))` is skipped when it appears on declarations
- labels and `goto` for simple local control-flow cases

Most of these are compatibility front-end features, not a promise of
full hosted C semantics. Generated code still targets the 32-bit flat
kernel ABI.

### Self-hosting compiler path

Cupid OS has a shared CupidC frontend, linear IR, and ELF32 emitter for the self-hosting migration. This path is separate from the in-kernel JIT and AOT compiler described elsewhere on this page. It assigns target-sized i386 stack storage to referenced fixed arrays and records with alignment up to four bytes. One-byte, two-byte, and four-byte integers work across locals, file objects, members, indexed access, conditions, conversions, assignment, mutation, and fixed or scalar variadic direct and indirect calls. Narrow loads produce canonical 32-bit values, while stores use the declared byte or word width. Represented cdecl scalar arguments keep four-byte stack slots, and callers and callees normalize narrow results.

The shared path lowers explicit casts to `void`. It evaluates the operand once, discards a represented integer, pointer, or supported structure result, and leaves a `void` operand off the abstract stack.

Automatic initializer lists work for fixed arrays and complete structures whose alignment does not exceed four bytes. The lowerer zero-initializes the full object once, then evaluates explicit integer, pointer, supported structure, and narrow character-array string leaves in source order. Scalar and structure values store through the selected array and member path. A string leaf copies the retained bytes with `REP MOVSB`, so unused tail elements keep their implicit zero value. Nested lists, direct designators, and omitted subobjects retain their frontend meaning. The i386 emitter performs the initial zeroing with `REP STOSB`.

The shared path carries compatible, complete structure values through lvalue conversion, automatic expression initialization, plain and chained assignment, matching conditional expressions, casts to `void` and other discarded expressions, fixed direct and indirect calls, parameters, and returns. Each lvalue conversion and structure call result receives its own frame snapshot. Copies cover the full target object size, so a supported structure may contain wide or floating members even though those members do not yet work as separate runtime values.

Structure arguments occupy inline cdecl stack storage in parameter order, and each argument is rounded up to four bytes. The caller clears ABI padding before it copies arguments. A structure-returning call passes a hidden destination pointer before the explicit arguments. The callee reads that pointer at `[ebp+8]`, reads its first explicit argument at `[ebp+12]`, copies the result into the destination, returns the pointer in `eax`, and uses `ret 4`. The caller cleans the explicit argument bytes.

Supported fixed and scalar variadic direct or indirect calls put ESP on a sixteen-byte boundary immediately before `call`. The emitter chooses zero, four, eight, or twelve bytes of padding from the function frame, live Linear IR stack, and outgoing structure area. Nested calls and structure-result calls follow the same rule.

For a scalar variadic call, the shared frontend applies lvalue, array, function, and integer default conversions to the ellipsis arguments. The call instruction retains the actual argument count, and the emitter uses it for cdecl order, the saved indirect callee, stack alignment, and caller cleanup. Direct and indirect calls can pass represented four-byte integers and pointers.

In GNU C mode, the shared frontend treats `__builtin_va_list` as a target `char *` cursor and retains typed start, argument, copy, and end operations. The i386 emitter starts the cursor after the final named cdecl parameter. It reads represented non-atomic four-byte integer and pointer slots and advances the stored cursor by four bytes after each read. The unchanged Doom compatibility header parses under its generated profile.

An empty identifier-list definition has zero parameters and keeps its non-prototype function type. Calls through a function type without a prototype apply default argument promotions to every argument, retain their actual count in Linear IR, and use the represented four-byte scalar cdecl path.

Block-scope `struct` and `union` tags follow lexical C scope. The shared frontend handles forward declarations, same-scope completion, ordinary references, nested shadowing, and restoration after a nested block ends. A record tag declared in a function definition's parameter list stays visible through the outer body, then expires with the definition. A tag-only declaration may use the represented `typedef`, `extern`, `static`, `auto`, or `register` spelling, or a represented type qualifier, when it introduces a tag, and has no runtime work. An empty declaration with storage or type qualification cannot merely repeat a visible tag. A `for` initializer may use a visible record type or an anonymous record definition for its object, but it cannot introduce a named tag or omit the object. An anonymous definition can supply the type for a local or block-static object, including Doom's unchanged block-static `packs` array.

A block-scope `extern` object keeps a lexical alias to one canonical linked object. Compatible repeats share identity, an incomplete array can be completed, and a visible file-scope `static` object keeps internal linkage. The declaration creates no automatic storage or runtime IR. Block typedefs also follow the ordinary identifier scope. They retain stable scalar, record, or function types, allow exact same-type repetition and nested shadowing, and create no runtime IR or ELF record.

A block function declaration keeps a lexical alias and visible type alongside one canonical linked function. Plain and `extern` forms share compatible identity. A visible prior declaration contributes to the alias's composite type, but an expired sibling prototype does not change the type seen by a later old-style declaration. A visible file-scope `static` function keeps internal linkage, while a function introduced only in a block stays out of file lookup until a later file declaration publishes it. The declaration emits no runtime IR or storage. Direct calls use `R_386_PC32`, and function addresses use `R_386_32`. Active-source guards cover 27 declarations across nine files. The exact Doom profile still parses all of `kernel/doom/src/d_main.c`, including its local `forwardmove` and `sidemove` declarations.

Declaration-position block enums keep each enumerator in the ordinary lexical binding stream. A later enumerator can use an earlier value, nested tags and constants shadow their outer names, and scope exit restores those names. An anonymous enum can share a `for` scope with its declared object. Linear IR turns represented uses into integer constants, so the declaration needs no frame slot, symbol, relocation, or runtime instruction. This covers the cursor constants in `kernel/gui/desktop.c` and the REPL limits in `kernel/lang/shell.c` without moving them. Enum definitions nested in block record members or type names, enum definitions in function-definition parameter lists, block declaration attributes, nested function definitions, nonempty identifier lists, atomic variadic access, floating default promotions, non-scalar arguments without declared parameter types, and wider or aggregate variadic reads remain unfinished.

Block-static objects use static storage in the shared ELF32 path. The emitter places top-level `const` objects in `.rodata`, writable zero-filled objects in `.bss`, and other writable objects in `.data`. Each object receives a local symbol derived from its absolute block-binding index, so shadowed names remain distinct. `LOCAL_ADDRESS` reaches that symbol through an `R_386_32` relocation instead of an EBP-relative frame slot, and the declaration emits no runtime initialization code. Unused and unreachable block statics still receive storage.

Block-scope compound literals use one persistent unnamed automatic object per source site. Their initializer runs each time execution reaches the expression, and the resulting lvalue can flow through ordinary member access, indexing, address-taking, loads, stores, and calls. Repeated evaluation in one function invocation reuses the same object. Recursive calls receive a fresh object in their own frame. Aggregate lists are assembled in separate staging storage and committed only after every initializer read has finished. A narrow string root zeros and copies directly into its persistent character array.

Runtime narrow string expressions receive local `.rodata` symbols and `R_386_32` relocations. They can decay into pointers for initialization, arguments, indexing, and returns. Supported structure graphs have alignment no greater than four bytes and contain no stored `volatile` or `_Atomic` subobjects. Graphs containing a union or class remain unsupported. Static-duration and variable-length compound literals, the named-aggregate backward-jump alias case, explicit bit-field leaves, Boolean mutation, separate 64-bit and floating-point runtime values, wider or aggregate variadic values, non-scalar unprototyped arguments, wide strings, literal pooling, and block-static addresses in other block-static initializers also remain unfinished in the shared path.

Production ownership is unchanged. The normal OS build still uses a host C compiler for its C objects, and the private in-kernel CupidC compiler still handles embedded runtime compilation. The hosted shared path does not produce a normal Cupid OS object or change a boot or runtime artifact.

A visible enum tag can also be used in a block type name or record member without defining another enum.

### Global Variables

Variables declared outside functions are stored in the data section:

```c
// Scalar globals with optional initializers
int count = 0;
int max_size = 1024;
int error_code = -1;
char *greeting = "Hello";

// Global arrays
int data[256];
char name[64];

// Global struct variables
struct Point origin;

void main() {
    count = 42;
    data[0] = 100;
    origin.x = 10;
    origin.y = 20;
    print(greeting);
}
```

Global variables support:
- Integer and character literal initializers (including negative values)
- String literal initializers (pointer to data section string)
- Arrays of any supported element type
- Struct variables (zero-initialized)
- All operators: `=`, `+=`, `-=`, `*=`, `/=`, `++`, `--`

### Built-in Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `NULL` | 0 | Null pointer constant |
| `true` | 1 | Boolean true |
| `false` | 0 | Boolean false |

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

// Do-while loop
do {
    process();
    i++;
} while (i < 10);

// For loop
for (int i = 0; i < 10; i++) {
    print_int(i);
}

// Switch/case
switch (cmd) {
    case 'a':
        print("add");
        break;
    case 'd':
        print("delete");
        break;
    default:
        print("unknown");
        break;
}
```

`break` and `continue` work inside `while`, `for`, `do-while` loops, and `switch` statements.

CupidC supports up to 32 `break` statements per loop and 64 nested loop levels. The compiler stores the break patch locations and resolves them when the loop ends.

### Functions

Functions use the cdecl calling convention. Up to 16 parameters per function.

```c
int add(int a, int b) {
    return a + b;
}

void main() {
    int result = add(5, 10);
    print_int(result);
}
```

Every program must have a `main()` function - it is the entry point.

Forward references are supported: functions can call other functions that are defined later in the file. The compiler resolves these after parsing is complete.

### String and Character Literals

```c
char* msg = "Hello, CupidOS!\n";
char ch = 'A';
char esc = '\x1B';  // ESC character for ANSI codes
```

#### Escape Sequences

CupidC supports the following escape sequences in string and character literals:

| Escape | Meaning | ASCII Value |
|--------|---------|-------------|
| `\n` | Newline (LF) | 10 |
| `\t` | Tab | 9 |
| `\r` | Carriage return (CR) | 13 |
| `\b` | Backspace | 8 |
| `\\` | Backslash | 92 |
| `\'` | Single quote | 39 |
| `\"` | Double quote | 34 |
| `\0` | Null terminator | 0 |
| `\xNN` | Hexadecimal byte (00-FF) | Variable |

**Hexadecimal Escapes**: The `\xNN` escape sequence allows specifying a byte value in hexadecimal. Both uppercase and lowercase hex digits are supported:

```c
char *red = "\x1B[31m";     // ANSI red color code
print("\x48\x69");          // Prints "Hi" (0x48='H', 0x69='i')
```

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

### Console Input

| Function | Signature | Description |
|----------|-----------|-------------|
| `getchar` | `int getchar()` | Read a single character from the keyboard (blocking) |

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
| `strcpy` | `char* strcpy(char* dst, char* src)` | Copy string (including null terminator) |
| `strncpy` | `char* strncpy(char* dst, char* src, int n)` | Copy up to n characters (pads with nulls) |
| `strcat` | `char* strcat(char* dst, char* src)` | Concatenate src onto end of dst |
| `strchr` | `char* strchr(char* s, int c)` | Find first occurrence of character c in s |
| `strstr` | `char* strstr(char* haystack, char* needle)` | Find first occurrence of substring |
| `memset` | `void* memset(void* p, int val, int n)` | Fill memory |
| `memcpy` | `void* memcpy(void* dst, void* src, int n)` | Copy memory |
| `memcmp` | `int memcmp(void* a, void* b, int n)` | Compare n bytes of memory |

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
| `set_cwd` | `void set_cwd(char* path)` | Set the shell's current working directory |
| `resolve_path` | `void resolve_path(char* input, char* out)` | Resolve a relative path against CWD (out must be 256 bytes) |
| `get_history_count` | `int get_history_count()` | Get the number of entries in shell history |
| `get_history_entry` | `char* get_history_entry(int index)` | Get history entry by offset from newest (0 = most recent) |

### Timer

| Function | Signature | Description |
|----------|-----------|-------------|
| `uptime_ms` | `int uptime_ms()` | Get system uptime in milliseconds |

### RTC (Real-Time Clock)

| Function | Signature | Description |
|----------|-----------|-------------|
| `rtc_hour` | `int rtc_hour()` | Current hour (0-23) |
| `rtc_minute` | `int rtc_minute()` | Current minute (0-59) |
| `rtc_second` | `int rtc_second()` | Current second (0-59) |
| `rtc_day` | `int rtc_day()` | Current day of month (1-31) |
| `rtc_month` | `int rtc_month()` | Current month (1-12) |
| `rtc_year` | `int rtc_year()` | Current year (e.g. 2026) |
| `rtc_weekday` | `int rtc_weekday()` | Day of week (0=Sunday, 6=Saturday) |
| `rtc_epoch` | `int rtc_epoch()` | Seconds since Unix epoch (Jan 1, 1970) |
| `date_full_string` | `char* date_full_string()` | Formatted date: "Thursday, February 6, 2026" |
| `date_short_string` | `char* date_short_string()` | Formatted date: "Feb 6, 2026" |
| `time_string` | `char* time_string()` | Formatted time: "6:32:15 PM" |
| `time_short_string` | `char* time_short_string()` | Formatted time: "6:32 PM" |

### Process Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `yield` | `void yield()` | Yield CPU to scheduler |
| `exit` | `void exit()` | Terminate current process |
| `exec` | `int exec(char* path, char* args)` | Execute a program |
| `process_list` | `void process_list()` | Print all processes (PID, state, name) |
| `process_kill` | `void process_kill(int pid)` | Terminate a process by PID |
| `spawn_test` | `int spawn_test(int count)` | Spawn N test counting processes (max 16), returns count spawned |

### Mount Info

| Function | Signature | Description |
|----------|-----------|-------------|
| `mount_count` | `int mount_count()` | Get the number of mounted filesystems |
| `mount_name` | `char* mount_name(int index)` | Get the filesystem name for mount at index |
| `mount_path` | `char* mount_path(int index)` | Get the mount path for mount at index |

### Diagnostics

| Function | Signature | Description |
|----------|-----------|-------------|
| `memstats` | `void memstats()` | Print heap and physical memory statistics |
| `detect_memory_leaks` | `void detect_memory_leaks(int ms)` | Report allocations older than `ms` milliseconds |
| `heap_check_integrity` | `void heap_check_integrity()` | Walk heap blocks and verify canary values |
| `pmm_free_pages` | `int pmm_free_pages()` | Number of free physical 4 KB pages |
| `pmm_total_pages` | `int pmm_total_pages()` | Total number of physical 4 KB pages |
| `dump_stack_trace` | `void dump_stack_trace()` | Print current call stack (EBP frame chain) |
| `dump_registers` | `void dump_registers()` | Print all CPU registers + EFLAGS |
| `peek_byte` | `int peek_byte(int addr)` | Read one byte from a memory address |
| `print_hex_byte` | `void print_hex_byte(int val)` | Print a byte as 2 hex digits |
| `get_cpu_mhz` | `int get_cpu_mhz()` | CPU frequency in MHz |
| `timer_get_frequency` | `int timer_get_frequency()` | Timer interrupt rate in Hz |
| `process_get_count` | `int process_get_count()` | Number of running processes |

### BMP Image Encoding/Decoding

| Function | Signature | Description |
|----------|-----------|-------------|
| `bmp_get_info` | `int bmp_get_info(char* path, void* info)` | Get BMP dimensions without loading pixels (fills `bmp_info_t`) |
| `bmp_decode` | `int bmp_decode(char* path, int* buf, int size)` | Decode 24-bit BMP to 32bpp XRGB buffer |
| `bmp_encode` | `int bmp_encode(char* path, int* buf, int w, int h)` | Encode 32bpp XRGB buffer to 24-bit BMP file |
| `bmp_decode_to_fb` | `int bmp_decode_to_fb(char* path, int x, int y)` | Decode BMP directly to framebuffer at position |

All BMP functions return `0` on success. Error codes: `-1` (invalid), `-2` (unsupported format), `-3` (I/O error), `-4` (buffer too small).

### gfx2d Image Pool (BMP / PNG / JPEG)

| Function | Signature | Description |
|----------|-----------|-------------|
| `gfx2d_image_load` | `int gfx2d_image_load(char* path)` | Load image from VFS path. Format auto-detected by signature: BMP, PNG, JPEG. Returns image handle (>= 0) or -1. |
| `gfx2d_image_load_mem` | `int gfx2d_image_load_mem(char* buf, int len)` | Decode a PNG or JPEG byte buffer directly (no VFS round-trip). Used by the browser to display network-fetched images. Returns handle or -1. |
| `gfx2d_image_free` | `void gfx2d_image_free(int handle)` | Release an image handle. |
| `gfx2d_image_draw` | `void gfx2d_image_draw(int handle, int x, int y)` | Blit image at (x, y), unscaled. |
| `gfx2d_image_draw_scaled` | `void gfx2d_image_draw_scaled(int handle, int x, int y, int w, int h)` | Blit scaled to (w, h). |
| `gfx2d_image_width` | `int gfx2d_image_width(int handle)` | Image width in pixels. |
| `gfx2d_image_height` | `int gfx2d_image_height(int handle)` | Image height in pixels. |

PNG decoder handles 8-bit color types 0/2/3/6 (gray, RGB, palette, RGBA), filters None/Sub/Up/Average/Paeth, non-interlaced.  JPEG decoder handles SOF0/SOF1 baseline at 8-bit precision, 1- or 3-component images, sub-samplings 1x1/2x1/1x2/2x2, restart markers.  Progressive JPEG, arithmetic coding, 12-bit, and CMYK are rejected.

### File Dialogs

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_dialog_open` | `int file_dialog_open(char* start, char* result, char* ext)` | Show modal file open dialog; returns 1 if selected, 0 if cancelled |
| `file_dialog_save` | `int file_dialog_save(char* start, char* name, char* result, char* ext)` | Show modal file save dialog; returns 1 if confirmed, 0 if cancelled |

The `result` buffer must be 128 bytes. Pass `0` for `ext` to show all files.

### VFS Helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `vfs_read_all` | `int vfs_read_all(char* path, void* buf, int max)` | Read entire file into buffer; returns bytes read or negative error |
| `vfs_write_all` | `int vfs_write_all(char* path, void* buf, int size)` | Write buffer to file (creates/truncates); returns bytes written or negative error |
| `vfs_read_text` | `int vfs_read_text(char* path, char* buf, int max)` | Read text file as null-terminated string; returns string length |
| `vfs_write_text` | `int vfs_write_text(char* path, char* text)` | Write null-terminated string to file; returns bytes written |

### Block Cache

| Function | Signature | Description |
|----------|-----------|-------------|
| `blockcache_sync` | `void blockcache_sync()` | Flush all dirty cache blocks to disk |
| `blockcache_stats` | `void blockcache_stats()` | Print cache hit/miss statistics |

### Serial Log Control

| Function | Signature | Description |
|----------|-----------|-------------|
| `set_log_level` | `void set_log_level(int level)` | Set log level (0=debug, 1=info, 2=warn, 3=error, 4=panic) |
| `get_log_level_name` | `char* get_log_level_name()` | Current log level as a string |
| `print_log_buffer` | `void print_log_buffer()` | Print the circular log buffer contents |

### Crash Testing

| Function | Signature | Description |
|----------|-----------|-------------|
| `kernel_panic` | `void kernel_panic(char* msg)` | Trigger a kernel panic with message |
| `crashtest_nullptr` | `void crashtest_nullptr()` | Dereference NULL pointer |
| `crashtest_divzero` | `void crashtest_divzero()` | Divide by zero |
| `crashtest_overflow` | `void crashtest_overflow()` | Overflow heap buffer (canary detection) |
| `crashtest_stackoverflow` | `void crashtest_stackoverflow()` | Allocate 64 KB on stack (page fault) |

### Networking - BSD sockets

Ports passed to / returned from these calls are network byte order - wrap
literals in `htons()`. See [Networking](Networking) for full protocol
details.

| Function | Signature | Description |
|----------|-----------|-------------|
| `socket` | `int socket(int type)` | `2`=TCP, `1`=UDP. Returns fd or negative error |
| `bind` | `int bind(int fd, U32 ip, U16 port)` | `ip=0` for INADDR_ANY |
| `listen` | `int listen(int fd, int backlog)` | Mark TCP socket passive |
| `accept` | `int accept(int fd, U32 *peer_ip, U16 *peer_port)` | Block until incoming SYN completes 3-way handshake |
| `connect` | `int connect(int fd, U32 ip, U16 port)` | Block until ESTABLISHED, refused, or 30 s timeout |
| `send` / `recv` | `int send(int fd, void *buf, U32 len)` / `recv(...)` | Stream I/O on TCP socket |
| `sendto` / `recvfrom` | `int sendto(int fd, void *buf, U32 len, U32 ip, U16 port)` / `recvfrom(...)` | UDP datagram I/O |
| `close` | `int close(int fd)` | Tear down socket (FIN handshake for TCP) |
| `dns_resolve` | `int dns_resolve(char *name, U32 *ip_out)` | UDP/53 A-record lookup, 16-entry cache |
| `htons` / `ntohs` | `U16 htons(U16)` / `U16 ntohs(U16)` | 16-bit byte swap |
| `htonl` / `ntohl` | `U32 htonl(U32)` / `U32 ntohl(U32)` | 32-bit byte swap |
| `IP_PROTO_ICMP` / `IP_PROTO_UDP` / `IP_PROTO_TCP` | `U32 IP_PROTO_TCP()` | Constants exposed as 0-arg getters |

### Networking - interface info & raw protocol

| Function | Signature | Description |
|----------|-----------|-------------|
| `net_get_ip` | `U32 net_get_ip()` | Local IPv4 of the primary NIC |
| `net_get_gateway` | `U32 net_get_gateway()` | Default gateway IPv4 |
| `net_get_dns` | `U32 net_get_dns()` | DNS server IPv4 |
| `net_get_mask` | `U32 net_get_mask()` | Subnet mask |
| `net_get_mac` | `void net_get_mac(U8 *out)` | Fills 6-byte MAC |
| `net_link_up` | `U32 net_link_up()` | 1 if link up, else 0 |
| `net_rx_packets` / `net_tx_packets` | `U32` | Counters since boot |
| `net_rx_drops` / `net_tx_errors` | `U32` | Error counters |
| `ip_parse` | `int ip_parse(char *s, U32 *out)` | `"a.b.c.d"` -> uint32 |
| `ipv4_send` | `int ipv4_send(U32 dst, U8 proto, U8 *payload, U32 plen)` | Build + send raw IPv4 (auto-fragments) |
| `arp_resolve` | `int arp_resolve(U32 ip, U8 *mac_out)` | Blocking resolve, 500 ms timeout |
| `arp_dump` | `void arp_dump()` | Print cache to serial |
| `arp_get_entries` | `int arp_get_entries(U32 *ips, U8 macs[][6], int max)` | Bulk read |
| `icmp_send_echo` | `int icmp_send_echo(U32 dst, U16 id, U16 seq, U32 paylen)` | Send echo request |
| `icmp_wait_reply` | `int icmp_wait_reply(U32 src, U16 id, U16 seq, U32 timeout_ms)` | Block for matching reply |
| `udp_send_raw` | `int udp_send_raw(U32 dst, U16 sport, U16 dport, U8 *data, U32 len)` | One-shot UDP datagram |

### Block devices (ATA / loopdev / USB-MSC)

| Function | Signature | Description |
|----------|-----------|-------------|
| `blkdev_count` | `int blkdev_count()` | Number of registered block devices |
| `blkdev_read` | `int blkdev_read(int idx, U32 lba, U32 count, void *buf)` | Read N sectors from blkdev[idx] |
| `blkdev_write` | `int blkdev_write(int idx, U32 lba, U32 count, void *buf)` | Write N sectors |
| `ata_read_sectors` | `int ata_read_sectors(U8 drive, U32 lba, U8 count, void *buf)` | Direct ATA read (drive 0 = master) |
| `ata_write_sectors` | `int ata_write_sectors(U8 drive, U32 lba, U8 count, void *buf)` | Direct ATA write |

### Keyboard, serial, PIT - direct driver access

| Function | Signature | Description |
|----------|-----------|-------------|
| `keyboard_read_event` | `bool keyboard_read_event(key_event_t *out)` | Pop one event (returns false if queue empty) |
| `keyboard_inject_scancode` | `void keyboard_inject_scancode(U8 sc)` | Synthesize a make/break scancode |
| `keyboard_get_shift` / `_ctrl` / `_alt` / `_caps_lock` | `bool` | Modifier-key state |
| `serial_read_char` | `int serial_read_char()` | Non-blocking COM1 RX, returns -1 if empty |
| `serial_write_char` | `void serial_write_char(char c)` | One byte to COM1 |
| `serial_write_string` | `void serial_write_string(char *s)` | NUL-terminated string |
| `serial_has_rx` | `int serial_has_rx()` | 1 if a byte is pending |
| `pit_set_frequency` | `void pit_set_frequency(U32 channel, U32 hz)` | Reprogram PIT channel |
| `timer_delay_us` | `void timer_delay_us(U32 us)` | TSC-based busy delay |

### PCI introspection (by index)

`idx` ranges from 0 to `pci_device_count()-1`. The kernel hides the
opaque `pci_device_t *` behind these index-based getters.

| Function | Returns |
|---|---|
| `pci_device_count()` | Number of PCI devices found at boot |
| `pci_get_vendor(idx)` | 16-bit vendor ID |
| `pci_get_device_id(idx)` | 16-bit device ID |
| `pci_get_class(idx)` | Packed `class<<16 | sub<<8 | prog_if` |
| `pci_get_irq(idx)` | IRQ line from PCI config space |
| `pci_get_bar(idx, bar)` | BAR value, `bar` = 0..5 |
| `pci_bar_is_mmio(idx, bar)` | 1 if MMIO, 0 if I/O port |
| `pci_enable_bus_master(idx)` | Set bus-master bit in command register |

### SMP / LAPIC / paging / PMM

> Misusing these functions can deadlock or corrupt the kernel. Use
> `bkl_lock` and `bkl_unlock` when the operation must be atomic with respect to
> other CPUs.

| Function | Signature | Description |
|----------|-----------|-------------|
| `lapic_get_id` | `U32 lapic_get_id()` | Local APIC ID of the calling CPU |
| `lapic_eoi` | `void lapic_eoi()` | End-of-interrupt (only call from a real ISR) |
| `bkl_lock` / `bkl_unlock` | `void bkl_lock()` / `bkl_unlock()` | Big kernel lock - recursive ticket spinlock, IRQ-save |
| `paging_map_mmio` | `void paging_map_mmio(U32 phys, U32 size)` | Identity-map a physical region with PWT|PCD bits |
| `pmm_alloc_page` | `void *pmm_alloc_page()` | Allocate one 4 KB physical page |
| `pmm_free_page` | `void pmm_free_page(void *page)` | Return a page to the PMM |

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

## Common Patterns

### Parsing Command-Line Arguments

Programs receive arguments as a single string via `get_args()`. To parse multiple space-separated arguments, use a token parsing function:

```c
// Parse a single token from a string
int parse_token(char *str, int start, char *out, int maxlen) {
    int i = start;

    // Skip leading spaces
    while (str[i] == ' ' || str[i] == '\t') {
        i = i + 1;
    }

    // Check if end of string
    if (str[i] == 0) {
        out[0] = 0;
        return 0;
    }

    // Copy token until space or end
    int j = 0;
    while (str[i] != 0 && str[i] != ' ' && str[i] != '\t' && j < maxlen - 1) {
        out[j] = str[i];
        i = i + 1;
        j = j + 1;
    }
    out[j] = 0;

    return i - start;
}

// Usage example
void main() {
    char *args = (char*)get_args();
    char token[256];
    int pos = 0;

    while (1) {
        int len = parse_token(args, pos, token, 256);
        if (len == 0) break;  // No more tokens

        print("Token: ");
        print(token);
        print("\n");

        pos = pos + len;
    }
}
```

This pattern is used in commands like `rm` to handle multiple file arguments.

### Error Handling with VFS

VFS functions return negative error codes on failure. Check return values and report a specific error:

```c
int fd = vfs_open(path, 0);
if (fd < 0) {
    print("Error opening file: ");
    print(path);
    print("\n");
    return;
}

// Use file...
vfs_close(fd);
```

Common VFS error codes:
- `-2`: No such file or directory (ENOENT)
- `-13`: Permission denied (EACCES)
- `-21`: Is a directory (EISDIR)

### Using ANSI Colors

Use hexadecimal escape sequences to embed ANSI color codes:

```c
void main() {
    char *red = "\x1B[31m";
    char *green = "\x1B[32m";
    char *reset = "\x1B[0m";

    print(red);
    print("Error: ");
    print(reset);
    print("Something went wrong\n");

    print(green);
    print("Success!\n");
    print(reset);
}
```

Common ANSI codes:
- `\x1B[0m` - Reset all attributes
- `\x1B[31m` - Red text
- `\x1B[32m` - Green text
- `\x1B[33m` - Yellow text
- `\x1B[34m` - Blue text
- `\x1B[35m` - Magenta text
- `\x1B[36m` - Cyan text
- `\x1B[95m` - Bright magenta

---

## Compiler Architecture

### Pipeline

CupidC is a **single-pass** compiler: it lexes, parses, and emits x86 machine code in one pass through the source.

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
│  Code Generator  │  Recursive descent -> x86 machine code
└────────┬────────┘
         │ raw bytes
         ▼
┌─────────────────┐
│  JIT: Execute    │  cupidc.c - copy to memory, jump to main()
│  AOT: Write ELF  │  cupidc_elf.c - emit ELF32 binary to disk
└─────────────────┘
```

### Source Files

| File | Lines | Role |
|------|-------|------|
| `cupidc.h` | ~250 | Header: token types, symbol table, compiler state, public API |
| `cupidc.c` | ~790 | Driver: JIT/AOT entry points, kernel bindings, state init |
| `cupidc_lex.c` | ~445 | Lexer: tokenizes source into keywords, literals, operators |
| `cupidc_parse.c` | ~2485 | Parser + x86 code generator: the core of the compiler |
| `cupidc_elf.c` | - | ELF32 binary writer for AOT mode |

### Lexer

The lexer (`cupidc_lex.c`) breaks source text into tokens:

- **Keywords:** `int`, `char`, `void`, `bool`, `if`, `else`, `while`, `for`, `do`, `return`, `asm`, `break`, `continue`, `struct`, `sizeof`, `switch`, `case`, `default`, `enum`
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
  0x01000000 - 0x010FFFFF  Code region (1 MB)
  0x01100000 - 0x018FFFFF  Data region (8 MB strings/globals)

AOT Mode:
  0x00200000+               ELF load address
  Code and data packed into ELF segments
```

### Forward References

When the parser encounters a call to an undefined function, it emits a placeholder `call` instruction and records a **patch entry**. After the entire program is parsed, `cc_parse_program()` resolves all forward references by patching the `call` targets with the correct addresses.

---

## Limitations

- **1 MB code limit** and **8 MB data/string limit** per program
- **4096 symbols** maximum (functions + variables + kernel bindings)
- **1024 functions**, **64 structs**, and **32 fields per struct**
- **16 parameters** maximum per function
- **32 struct definitions** with up to 16 fields each
- **No preprocessor** (`#include`, `#define` not supported) - use `enum` for constants
- **No multi-file compilation** - single source file only
- **No floating point** - integer arithmetic only
- **No standard library** - only kernel bindings are available
- **No function pointers** - cannot store/call through function pointer variables
- **No ternary operator** (`?:`) - use `if/else` instead
- **No variadic definitions in the in-kernel JIT/AOT compiler** - it still uses a fixed parameter count; the hosted self-hosting path has the scalar caller and callee subset described above
- **Limited optimization** - single-pass compilation with no optimization passes

---

## Shell Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `cupidc` | `cupidc <file.cc>` | JIT compile and execute a CupidC source file |
| `ccc` | `ccc <file.cc> -o <output>` | AOT compile to ELF32 binary |

---

## User Programs (TempleOS-style)

The shell discovers CupidC programs in `/bin/` and `/home/bin/`. Programs added to `/home/bin/` can run without rebuilding the kernel.

### How It Works

When you type a command the shell doesn't recognize, it searches in order:

1. `/bin/<cmd>` - ELF/CUPD binary in ramfs
2. `/bin/<cmd>.cc` - CupidC source in ramfs (JIT compiled)
3. `/home/bin/<cmd>` - ELF binary on disk
4. `/home/bin/<cmd>.cc` - CupidC source on disk (JIT compiled)

The shell JIT-compiles a discovered `.cc` file before running it.

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
- `get_args()` - retrieve shell arguments
- `get_cwd()` - resolve relative paths
- `vfs_stat()` - check if destination is a directory
- `vfs_rename()` - perform the move

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
| Types | Full C types + classes | int, char, void, bool, pointers, arrays, structs |
| Enums | Yes | Yes |
| Inline ASM | Yes | Yes |
| Port I/O | Direct access | `inb()`/`outb()` builtins |
| Hardware access | Full | Full |
| Structs | Yes | Yes |
| Classes | Yes | No |
| Preprocessor | `#include` | No |

## Shell REPL

The normal shell prompt includes a TempleOS-style CupidC REPL.
The shell tries to compile each line as CupidC first and only falls back to normal
command dispatch when REPL parsing or compilation fails. TempleOS's
`ExeCmdLine()`, `LexStmt2Bin()`, and `CmdLinePmt()` are the design references.

### Prompt behavior

- End statements and expressions with semicolons, HolyC-style.
- Zero-argument REPL functions can be invoked as `Foo;`.
- Multi-line blocks stay open until braces balance.
- The next prompt shows the elapsed execution time.
- Expression results are surfaced as TempleOS-style `ans`.
- Forward-call fixups persist across prompt entries, so later function
  definitions can resolve earlier REPL-defined callers.

Example:

```c
/home> U32 x = 7;
/home> x + 5;
0.001s ans=0x0000000C=12
/home> ans;
0.000s ans=0x0000000C=12
/home> U32 Add(U32 a, U32 b) {
..>   return a + b;
..> }
/home> Add(2, 3);
0.000s ans=0x00000005=5
```

### Fallback behavior

These inputs resolve as shell commands because they are not valid CupidC REPL input:

```text
help
ls /home
```

### Resetting REPL state

Use:

```text
reset
```

This clears persistent REPL variables, functions, structs, typedefs, and `ans`.
