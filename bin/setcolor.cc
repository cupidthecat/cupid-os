//help: Set terminal color
//help: Usage: setcolor <fg 0-15> [bg 0-7]
//help: Sets the terminal foreground and optionally background
//help: color using ANSI escape codes.
//help: Colors: 0=black 1=red 2=green 3=yellow
//help:   4=blue 5=magenta 6=cyan 7=white
//help:   8-15=bright variants

void main() {
    char *args = (char*)get_args();
    if (strlen(args) == 0) {
        println("Usage: setcolor <fg 0-15> [bg 0-7]");
        return;
    }

    // Parse foreground color
    int i = 0;
    while (args[i] == ' ') i = i + 1;
    int fg = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        fg = fg * 10 + (args[i] - 48);
        i = i + 1;
    }

    // Skip spaces
    while (args[i] == ' ') i = i + 1;

    // Parse optional background color
    int bg = -1;
    if (args[i] >= '0' && args[i] <= '9') {
        bg = 0;
        while (args[i] >= '0' && args[i] <= '9') {
            bg = bg * 10 + (args[i] - 48);
            i = i + 1;
        }
    }

    // Build ANSI escape: ESC[<fg>m or ESC[<fg>;<bg>m
    putchar(27);
    putchar('[');
    if (fg >= 8) {
        putchar('9');
        putchar((char)(48 + (fg - 8)));
    } else {
        putchar('3');
        putchar((char)(48 + fg));
    }
    if (bg >= 0) {
        putchar(';');
        putchar('4');
        putchar((char)(48 + (bg & 7)));
    }
    putchar('m');
}
