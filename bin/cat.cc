//help: Display file contents
//help: Usage: cat <filename>
//help: Prints the contents of a file to the terminal.
//help: Output is truncated at 64KB for safety.

void main() {
    char *args = (char*)get_args();
    if (strlen(args) == 0) {
        println("Usage: cat <filename>");
        return;
    }

    char path[256];
    resolve_path(args, path);

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        print("cat: file not found: ");
        println(args);
        return;
    }

    char buf[256];
    int total = 0;
    int r = vfs_read(fd, buf, 255);
    while (r > 0) {
        int i = 0;
        while (i < r) {
            putchar(buf[i]);
            i = i + 1;
        }
        total = total + r;
        if (total > 65536) {
            println("\n[cat: output truncated at 64KB]");
            break;
        }
        r = vfs_read(fd, buf, 255);
    }
    print("\n");
    vfs_close(fd);
}
