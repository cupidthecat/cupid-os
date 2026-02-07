// memdump.cc - Dump memory region in hex
// Usage: memdump ADDR [length]

int parse_hex_char(int c) {
    if (c >= 48 && c <= 57) return c - 48;
    if (c >= 97 && c <= 102) return c - 97 + 10;
    if (c >= 65 && c <= 70) return c - 65 + 10;
    return -1;
}

int parse_hex_str(char *s) {
    int val = 0;
    if (s[0] == 48 && (s[1] == 120 || s[1] == 88)) {
        s = s + 2;
    }
    while (*s) {
        int d = parse_hex_char(*s);
        if (d < 0) return val;
        val = (val * 16) + d;
        s = s + 1;
    }
    return val;
}

int parse_dec_str(char *s) {
    int val = 0;
    while (*s >= 48 && *s <= 57) {
        val = val * 10 + (*s - 48);
        s = s + 1;
    }
    return val;
}

void main() {
    char *args = get_args();
    if (!args || !*args) {
        print("Usage: memdump ADDR [length]\n");
        return;
    }

    int addr = parse_hex_str(args);

    while (*args && *args != 32) args = args + 1;
    while (*args == 32) args = args + 1;

    int len = 64;
    if (*args) {
        len = parse_dec_str(args);
    }
    if (len > 512) len = 512;

    int i = 0;
    while (i < len) {
        print_hex(addr + i);
        print(": ");
        int j = 0;
        while (j < 16 && (i + j) < len) {
            print_hex_byte(peek_byte(addr + i + j));
            putchar(32);
            j = j + 1;
        }
        print(" ");
        j = 0;
        while (j < 16 && (i + j) < len) {
            int b = peek_byte(addr + i + j);
            if (b >= 32 && b < 127) {
                putchar(b);
            } else {
                putchar(46);
            }
            j = j + 1;
        }
        print("\n");
        i = i + 16;
    }
}
