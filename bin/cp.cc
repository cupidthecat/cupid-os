//help: Copy files
//help: Usage: cp <source> <dest>
//help: Copies a file to a new path. If <dest> is a directory,
//help: the source filename is copied into that directory.

char *cp_basename(char *path) {
    int i = strlen(path) - 1;
    while (i >= 0) {
        if (path[i] == '/') return path + i + 1;
        i = i - 1;
    }
    return path;
}

void cp_join_path(char *out, char *dir, char *name) {
    int i = 0;
    int di = 0;
    while (dir[di]) { out[i] = dir[di]; i = i + 1; di = di + 1; }
    if (i > 0 && out[i - 1] != '/') { out[i] = '/'; i = i + 1; }
    int ni = 0;
    while (name[ni]) { out[i] = name[ni]; i = i + 1; ni = ni + 1; }
    out[i] = 0;
}

int cp_parse_two(char *args, char *a, char *b) {
    int i = 0;
    int ai = 0;
    int bi = 0;

    while (args[i] == ' ' || args[i] == '\t') i = i + 1;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && ai < 255) {
        a[ai] = args[i]; ai = ai + 1; i = i + 1;
    }
    a[ai] = 0;

    while (args[i] == ' ' || args[i] == '\t') i = i + 1;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && bi < 255) {
        b[bi] = args[i]; bi = bi + 1; i = i + 1;
    }
    b[bi] = 0;

    if (a[0] == 0 || b[0] == 0) return 0;
    return 1;
}

void main() {
    char *args = (char*)get_args();
    char src_arg[256];
    char dst_arg[256];

    if (!cp_parse_two(args, src_arg, dst_arg)) {
        println("Usage: cp <source> <dest>");
        return;
    }

    char src[256];
    char dst[256];
    resolve_path(src_arg, src);
    resolve_path(dst_arg, dst);

    char final_dst[256];
    int i = 0;
    while (dst[i]) { final_dst[i] = dst[i]; i = i + 1; }
    final_dst[i] = 0;

    char st[8];
    if (vfs_stat(dst, st) >= 0 && st[4] == 1) {
        cp_join_path(final_dst, dst, cp_basename(src));
    }

    int r = vfs_copy_file(src, final_dst);
    if (r < 0) {
        print("cp: failed to copy '");
        print(src_arg);
        print("' to '");
        print(dst_arg);
        println("'");
    }
}
