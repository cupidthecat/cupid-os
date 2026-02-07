//help: Reset terminal colors
//help: Usage: resetcolor
//help: Resets terminal foreground and background colors
//help: to their defaults.

void main() {
    putchar(27);
    print("[0m");
}
