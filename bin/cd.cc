//help: Change the current working directory
//help: Usage: cd [directory]
//help: Changes to the specified directory. With no arguments,
//help: changes to the root directory (/). Supports relative
//help: paths, "." (current) and ".." (parent).

void main() {
    char *args = (char*)get_args();
    char path[256];

    // Default to "/" if no args
    if (strlen(args) == 0) {
        path[0] = '/';
        path[1] = 0;
    } else {
        resolve_path(args, path);
    }

    // Verify the target exists and is a directory
    // vfs_stat_t: size(4) + type(1) = 5 bytes minimum
    // Actually in the kernel: uint32_t size (offset 0), uint8_t type (offset 4)
    char st[8];
    int r = vfs_stat(path, st);
    if (r < 0) {
        print("cd: no such directory: ");
        println(args);
        return;
    }

    int type = st[4];
    if (type != 1) {
        print("cd: not a directory: ");
        println(args);
        return;
    }

    set_cwd(path);
}
