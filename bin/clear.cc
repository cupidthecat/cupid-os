//help: Clear the terminal screen
//help: Usage: clear
//help: Sends ANSI escape codes to erase the display
//help: and reset the cursor to the top-left corner.

void main() {
    // ANSI escape: clear screen + move cursor home
    char esc[2];
    esc[0] = 27;
    esc[1] = 0;
    print(esc);
    print("[2J");
    print(esc);
    print("[H");
}
