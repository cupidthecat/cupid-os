//help: List available commands or show help for a command
//help: Usage: help [command]
//help: With no arguments, lists all programs and their
//help: one-line descriptions. With a command name, shows
//help: the full help text for that command.

void strip_cc(char *name) {
    int len = strlen(name);
    if (len > 3) {
        if (name[len - 3] == '.' && name[len - 2] == 'c' && name[len - 1] == 'c') {
            name[len - 3] = 0;
        }
    }
}

// Read //help: lines from a .cc file and print them.
// If summary_only, print just the first line (no "//help: " prefix).
// Returns 1 if any help lines found, 0 otherwise.
int show_help_lines(char *path, int summary_only) {
    int fd = vfs_open(path, 0);
    if (fd < 0) return 0;
    char buf[4096];
    int n = vfs_read(fd, buf, 4095);
    vfs_close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;

    int found = 0;
    int i = 0;
    while (i < n) {
        // Check if line starts with //help:
        if (buf[i] == '/' && buf[i+1] == '/' && buf[i+2] == 'h'
            && buf[i+3] == 'e' && buf[i+4] == 'l' && buf[i+5] == 'p'
            && buf[i+6] == ':') {
            int start = i + 7;
            // Skip one space after colon
            if (buf[start] == ' ') start = start + 1;
            // Find end of line
            int end = start;
            while (end < n && buf[end] != 10 && buf[end] != 0) {
                end = end + 1;
            }
            // Print this help line
            char line[256];
            int li = 0;
            int si = start;
            while (si < end && li < 255) {
                line[li] = buf[si];
                li = li + 1;
                si = si + 1;
            }
            line[li] = 0;

            if (summary_only) {
                print(line);
                return 1;
            }
            println(line);
            found = 1;
        }
        // Skip to next line
        while (i < n && buf[i] != 10 && buf[i] != 0) i = i + 1;
        if (i < n) i = i + 1;
        // Stop scanning after first non-help line if we already found some
        if (found) {
            if (i < n && !(buf[i] == '/' && buf[i+1] == '/' && buf[i+2] == 'h')) {
                return found;
            }
        }
        // Stop if we hit a non-comment line and haven't found help yet
        if (!found && i < n && buf[i] != '/' && buf[i] != 10) {
            return 0;
        }
    }
    return found;
}

void show_cmd_help(char *cmd) {
    // Build path: /bin/<cmd>.cc
    char path[128];
    int pi = 0;
    char *pfx = "/bin/";
    int xi = 0;
    while (pfx[xi]) { path[pi] = pfx[xi]; pi = pi + 1; xi = xi + 1; }
    int ci = 0;
    while (cmd[ci]) { path[pi] = cmd[ci]; pi = pi + 1; ci = ci + 1; }
    // Add .cc if not already there
    int len = strlen(cmd);
    if (len < 3 || cmd[len-3] != '.' || cmd[len-2] != 'c' || cmd[len-1] != 'c') {
        path[pi] = '.'; pi = pi + 1;
        path[pi] = 'c'; pi = pi + 1;
        path[pi] = 'c'; pi = pi + 1;
    }
    path[pi] = 0;

    if (!show_help_lines(path, 0)) {
        // Try /home/bin/<cmd>.cc
        pi = 0;
        char *pfx2 = "/home/bin/";
        xi = 0;
        while (pfx2[xi]) { path[pi] = pfx2[xi]; pi = pi + 1; xi = xi + 1; }
        ci = 0;
        while (cmd[ci]) { path[pi] = cmd[ci]; pi = pi + 1; ci = ci + 1; }
        if (len < 3 || cmd[len-3] != '.' || cmd[len-2] != 'c' || cmd[len-1] != 'c') {
            path[pi] = '.'; pi = pi + 1;
            path[pi] = 'c'; pi = pi + 1;
            path[pi] = 'c'; pi = pi + 1;
        }
        path[pi] = 0;
        if (!show_help_lines(path, 0)) {
            print("No help for '");
            print(cmd);
            println("'");
        }
    }
}

void list_dir_programs(char *dir, char *label) {
    int fd = vfs_open(dir, 0);
    if (fd < 0) return;
    char ent[72];
    int found = 0;
    while (vfs_readdir(fd, ent) > 0) {
        if (ent[68] == 0) {
            if (!found) { println(label); found = 1; }
            char name[64];
            int i = 0;
            while (ent[i] && i < 63) { name[i] = ent[i]; i = i + 1; }
            name[i] = 0;
            // Build full path for help lookup
            char fpath[128];
            int fp = 0;
            int di = 0;
            while (dir[di]) { fpath[fp] = dir[di]; fp = fp + 1; di = di + 1; }
            fpath[fp] = '/'; fp = fp + 1;
            int ni = 0;
            while (name[ni]) { fpath[fp] = name[ni]; fp = fp + 1; ni = ni + 1; }
            fpath[fp] = 0;

            strip_cc(name);
            print("  ");
            print(name);

            // Pad to 12 chars
            int nlen = strlen(name);
            while (nlen < 12) { print(" "); nlen = nlen + 1; }
            print("- ");
            if (!show_help_lines(fpath, 1)) {
                print("(no description)");
            }
            print("\n");
        }
    }
    vfs_close(fd);
}

void main() {
    char *args = (char*)get_args();
    if (strlen(args) > 0) {
        show_cmd_help(args);
        return;
    }

    println("CupidOS Commands");
    println("================");
    println("");
    list_dir_programs("/bin", "Programs (/bin):");
    println("");
    list_dir_programs("/home/bin", "User programs (/home/bin):");
    println("");
    println("Shell built-ins: jobs");
    println("Scripting: cupid <file.cup>");
    println("Compiler: cupidc <file.cc>, ccc <file.cc> -o <out>");
    println("");
    println("Type 'help <command>' for detailed help.");
}
