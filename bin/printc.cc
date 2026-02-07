//help: Print colored text
//help: Usage: printc <fg 0-15> <text>
//help: Prints text in the specified foreground color,
//help: then resets to default colors.
//help: Colors: 0=black 1=red 2=green 3=yellow
//help:   4=blue 5=magenta 6=cyan 7=white
//help:   8-15=bright variants

void main() {
    char *args = (char*)get_args();
    if (strlen(args) == 0) {
        println("Usage: printc <fg 0-15> <text>");
        return;
    }

    // Parse color number
    int i = 0;
    while (args[i] == ' ') i = i + 1;
    int color = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        color = color * 10 + (args[i] - 48);
        i = i + 1;
    }

    // Skip spaces to get text
    while (args[i] == ' ') i = i + 1;

    if (args[i] == 0) {
        println("Usage: printc <fg 0-15> <text>");
        return;
    }

    // Emit color escape
    putchar(27);
    putchar('[');
    if (color >= 8) {
        putchar('9');
        putchar((char)(48 + (color - 8)));
    } else {
        putchar('3');
        putchar((char)(48 + color));
    }
    putchar('m');

    // Print text starting at args[i]
    while (args[i]) {
        putchar(args[i]);
        i = i + 1;
    }
    print("\n");

    // Reset colors
    putchar(27);
    print("[0m");
}
