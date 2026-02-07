//help: Print text to the terminal
//help: Usage: echo <text>
//help: Prints its arguments followed by a newline.

void main() {
    char *args = (char*)get_args();
    if (strlen(args) > 0) {
        print(args);
    }
    print("\n");
}
