# CupidC Language Reference

CupidC is a C-like language for cupid-os that compiles to x86 machine code via JIT compilation.

## String Escape Sequences

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

### Hexadecimal Escapes

The `\xNN` escape sequence allows specifying a byte value in hexadecimal:

```c
char esc = '\x1B';          // ESC character (27)
char *red = "\x1B[31m";     // ANSI red color code
print("\x48\x69");          // Prints "Hi" (0x48='H', 0x69='i')
```

Both uppercase and lowercase hex digits are supported: `\x1B`, `\x1b`, `\xFF`, `\xff`

## Control Flow

### Break Statement

The `break` statement exits the innermost loop or switch statement:

```c
while (1) {
    if (condition1) break;  // Exit loop
    if (condition2) break;  // Multiple breaks supported
}
```

**Implementation**: CupidC supports multiple `break` statements per loop. The compiler
maintains an array of break patch locations (up to 32 breaks per loop) and patches
them all when the loop ends.

## Available Functions

### Built-in Functions

#### I/O Functions
- `print(char *s)` - Print a string
- `putchar(char c)` - Print a single character
- `print_int(int n)` - Print an integer
- `print_hex(int n)` - Print integer in hexadecimal
- `getchar()` - Read a character from keyboard

#### Memory Functions
- `kmalloc(int size)` - Allocate memory
- `kfree(void *ptr)` - Free memory
- `memset(void *ptr, int val, int size)` - Set memory bytes
- `memcpy(void *dst, void *src, int size)` - Copy memory

#### String Functions
- `strlen(char *s)` - Get string length
- `strcmp(char *s1, char *s2)` - Compare strings
- `strncmp(char *s1, char *s2, int n)` - Compare first n characters

#### File System Functions
- `vfs_open(char *path, int flags)` - Open a file
- `vfs_close(int fd)` - Close a file
- `vfs_read(int fd, char *buf, int size)` - Read from file
- `vfs_write(int fd, char *buf, int size)` - Write to file
- `vfs_unlink(char *path)` - Delete a file
- `vfs_mkdir(char *path)` - Create a directory
- `resolve_path(char *rel, char *abs)` - Resolve relative path to absolute

#### Program Functions
- `get_args()` - Get command-line arguments as string
- `exit(int code)` - Exit program with code

## Limitations

- Maximum 32 `break` statements per loop
- Maximum 64 nested loops
- No `switch` statement with more than 64 cases
- No floating point support
- No preprocessor macros

## Common Patterns

### Parsing Command-Line Arguments

Programs receive arguments as a single string via `get_args()`. To parse multiple space-separated arguments:

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

### Error Handling with VFS

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

## Examples

### Hello World

```c
void main() {
    print("Hello, World!\n");
}
```

### Using ANSI Colors

```c
void main() {
    char *red = "\x1B[31m";
    char *reset = "\x1B[0m";

    print(red);
    print("This is red text");
    print(reset);
    print("\n");
}
```

### Loop with Multiple Breaks

```c
void main() {
    int i = 0;
    while (i < 100) {
        if (i == 10) break;      // Exit at 10
        if (i % 7 == 0) break;   // Or exit at first multiple of 7
        i = i + 1;
    }
    print_int(i);
}
```
