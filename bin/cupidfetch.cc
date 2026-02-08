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

    // Gather system information
    int ms       = uptime_ms();
    int cpu_mhz  = get_cpu_mhz();
    int free_pg  = pmm_free_pages();
    int total_pg = pmm_total_pages();
    int free_kb  = free_pg * 4;
    int total_kb = total_pg * 4;
    int used_kb  = total_kb - free_kb;
    int mem_pct  = total_kb ? (used_kb * 100) / total_kb : 0;
    int procs    = process_get_count();
    int mounts   = vfs_mount_count();
    int gui      = is_gui_mode();

    // Uptime components
    int secs = ms / 1000;
    int mins = secs / 60;  secs = secs % 60;
    int hrs  = mins / 60;  mins = mins % 60;
    int days = hrs  / 24;  hrs  = hrs  % 24;

    // Memory in MiB
    int used_mib  = used_kb  / 1024;
    int total_mib = total_kb / 1024;

    // OS
    print(c_label);  print("OS: ");
    print(c_val);    print("cupid-os x86\n");

    // Kernel
    print(c_label);  print("Kernel: ");
    print(c_val);    print("1.0.0\n");

    // Uptime
    print(c_label);  print("Uptime: ");
    print(c_val);
    if (days > 0) {
        print_int(days);
        print("d ");
    }
    if (hrs > 0) {
        print_int(hrs);
        print("h ");
    }
    print_int(mins);
    print("m\n");

    // Shell
    print(c_label);  print("Shell: ");
    print(c_val);    print("cupid shell\n");

    // Display
    print(c_label);  print("Display: ");
    print(c_val);
    if (gui) {
        print("640x480 32bpp\n");
    } else {
        print("80x25 16c\n");
    }

    // Terminal
    print(c_label);  print("Term: ");
    print(c_val);
    if (gui) {
        print("GUI\n");
    } else {
        print("VGA Text\n");
    }

    // CPU
    print(c_label);  print("CPU: ");
    print(c_val);    print("x86 @ ");
    print_int(cpu_mhz);
    print(" MHz\n");

    // Memory
    print(c_label);  print("Mem: ");
    print(c_val);
    print_int(used_mib);
    print("/");
    print_int(total_mib);
    print(" MiB (");
    print_int(mem_pct);
    print("%)\n");

    // Processes
    print(c_label);  print("Procs: ");
    print(c_val);
    print_int(procs);
    print(" running\n");

    // Mounts
    print(c_label);  print("Mounts: ");
    print(c_val);
    print_int(mounts);
    print(" fs\n");

    // Date and Time
    print(c_label);  print("Date: ");
    print(c_val);    print(date_full_string());
    print("\n");

    print(c_label);  print("Time: ");
    print(c_val);    print(time_string());
    print("\n");

    // Color palette bars (background colors + spaces, matching original)
    print("\n");
    int i = 0;
    while (i < 8) {
        print("\x1B[");
        if (i == 0) print("40");
        if (i == 1) print("41");
        if (i == 2) print("42");
        if (i == 3) print("43");
        if (i == 4) print("44");
        if (i == 5) print("45");
        if (i == 6) print("46");
        if (i == 7) print("47");
        print("m    ");
        i = i + 1;
    }
    print(c_rst);
    print("\n");

    i = 0;
    while (i < 8) {
        print("\x1B[");
        if (i == 0) print("100");
        if (i == 1) print("101");
        if (i == 2) print("102");
        if (i == 3) print("103");
        if (i == 4) print("104");
        if (i == 5) print("105");
        if (i == 6) print("106");
        if (i == 7) print("107");
        print("m    ");
        i = i + 1;
    }
    print(c_rst);
    print("\n");
}
