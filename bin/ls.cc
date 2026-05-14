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

    // vfs_dirent_t: name[128], size(uint32_t at offset 128), type(uint8_t at offset 132), 3 pad = 136 bytes total
    char ent[136];
    int count = 0;
    while (vfs_readdir(fd, ent) > 0) {
        int type = ent[132];
        if (type == 1) {
            print("[DIR]  ");
        } else if (type == 2) {
            print("[DEV]  ");
        } else {
            print("       ");
        }

        // Copy name from ent[0..127]
        char name[128];
        int i = 0;
        while (ent[i] && i < 127) {
            name[i] = ent[i];
            i = i + 1;
        }
        name[i] = 0;
        print(name);

        // Show file size for regular files
        if (type == 0) {
            // Read uint32_t size from offset 128 (little-endian)
            int sz = ent[128] | (ent[129] << 8) | (ent[130] << 16) | (ent[131] << 24);
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
