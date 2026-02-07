//help: List files and directories
//help: Usage: ls [path]
//help: Lists files and directories in the given path, or the
//help: current directory if no path is given. Directories are
//help: marked with [DIR], devices with [DEV], and file sizes
//help: are shown in bytes.

void main() {
    char *args = (char*)get_args();
    char path[256];
    resolve_path(args, path);

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        print("ls: cannot open ");
        println(path);
        return;
    }

    // vfs_dirent_t is 72 bytes (with padding): name[64], size(4 bytes at offset 64), type(1 byte at offset 68), 3 pad
    char ent[72];
    int count = 0;
    while (vfs_readdir(fd, ent) > 0) {
        int type = ent[68];
        if (type == 1) {
            print("[DIR]  ");
        } else if (type == 2) {
            print("[DEV]  ");
        } else {
            print("       ");
        }

        // Copy name from ent[0..63]
        char name[64];
        int i = 0;
        while (ent[i] && i < 63) {
            name[i] = ent[i];
            i = i + 1;
        }
        name[i] = 0;
        print(name);

        // Show file size for regular files
        if (type == 0) {
            // Read uint32_t size from offset 64 (little-endian)
            int sz = ent[64] | (ent[65] << 8) | (ent[66] << 16) | (ent[67] << 24);
            print("  ");
            print_int(sz);
            print(" bytes");
        }
        print("\n");
        count = count + 1;
    }

    vfs_close(fd);
    if (count == 0) {
        println("(empty directory)");
    }
}
