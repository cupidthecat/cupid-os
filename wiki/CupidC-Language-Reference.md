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

- `print(char *s)` - Print a string
- `putchar(char c)` - Print a single character
- `print_int(int n)` - Print an integer
- `getchar()` - Read a character from keyboard
- `kmalloc(int size)` - Allocate memory
- `kfree(void *ptr)` - Free memory
- `strlen(char *s)` - Get string length
- `strcmp(char *s1, char *s2)` - Compare strings
- `strcpy(char *dst, char *src)` - Copy string

## Limitations

- Maximum 32 `break` statements per loop
- Maximum 64 nested loops
- No `switch` statement with more than 64 cases
- No floating point support
- No preprocessor macros

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
