//help: Display system information with ASCII art
//help: Usage: cupidfetch
//help: Shows a neofetch-style system information display with a cat mascot.

// CupidC implementation of cupidfetch
// Displays system information in a colorful, neofetch-style format

void main() {
    // ANSI color codes
    char *c_cat   = "\x1B[95m";  // bright magenta
    char *c_hdr   = "\x1B[93m";  // bright yellow
    char *c_label = "\x1B[96m";  // bright cyan
    char *c_val   = "\x1B[97m";  // bright white
    char *c_rst   = "\x1B[0m";   // reset

    // ASCII cat art
    print(c_cat);
    print("   /\\_/\\   \n");
    print("  ( o.o )  ");
    print(c_hdr);
    print("cupid-os\n");
    print(c_cat);
    print("   > ^ <   ");
    print(c_hdr);
    print("-----------\n");
    print(c_cat);
    print("  /|   |\\  \n");
    print(" (_|   |_) \n");
    print(c_rst);

    // System information
    print(c_label);  print("OS: ");
    print(c_val);    print("cupid-os x86\n");

    print(c_label);  print("Kernel: ");
    print(c_val);    print("1.0.0\n");

    print(c_label);  print("Shell: ");
    print(c_val);    print("cupid shell\n");

    print(c_label);  print("Display: ");
    print(c_val);    print("320x200 256c\n");

    print(c_label);  print("Term: ");
    print(c_val);    print("GUI\n");

    // Color palette
    print(c_rst);
    print("\n");

    // Standard colors
    int i = 0;
    while (i < 8) {
        print("\x1B[");
        if (i == 0) print("30");
        if (i == 1) print("31");
        if (i == 2) print("32");
        if (i == 3) print("33");
        if (i == 4) print("34");
        if (i == 5) print("35");
        if (i == 6) print("36");
        if (i == 7) print("37");
        print("m██");
        i = i + 1;
    }
    print(c_rst);
    print("\n");

    // Bright colors
    i = 0;
    while (i < 8) {
        print("\x1B[");
        if (i == 0) print("90");
        if (i == 1) print("91");
        if (i == 2) print("92");
        if (i == 3) print("93");
        if (i == 4) print("94");
        if (i == 5) print("95");
        if (i == 6) print("96");
        if (i == 7) print("97");
        print("m██");
        i = i + 1;
    }
    print(c_rst);
    print("\n");
}
