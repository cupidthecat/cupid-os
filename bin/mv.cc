// mv.cc - move/rename files for CupidOS

void resolve_path(char *out, char *path) {
    int i = 0;
    if (path[0] == '/') {
        while (path[i]) { out[i] = path[i]; i = i + 1; }
        out[i] = 0;
        return;
    }
    char *cwd = (char*)get_cwd();
    int ci = 0;
    while (cwd[ci]) { out[i] = cwd[ci]; i = i + 1; ci = ci + 1; }
    if (i > 1) { out[i] = '/'; i = i + 1; }
    int pi = 0;
    while (path[pi]) { out[i] = path[pi]; i = i + 1; pi = pi + 1; }
    out[i] = 0;
}

char *basename(char *path) {
    int i = strlen(path) - 1;
    while (i >= 0) {
        if (path[i] == '/') return path + i + 1;
        i = i - 1;
    }
    return path;
}

void append_filename(char *dst, char *dir, char *fname) {
    int i = 0;
    int di = 0;
    while (dir[di]) { dst[i] = dir[di]; i = i + 1; di = di + 1; }
    if (i > 0 && dst[i - 1] != '/') { dst[i] = '/'; i = i + 1; }
    int fi = 0;
    while (fname[fi]) { dst[i] = fname[fi]; i = i + 1; fi = fi + 1; }
    dst[i] = 0;
}

void main() {
    char *args = (char*)get_args();
    if (strlen(args) == 0) {
        println("Usage: mv <source> <dest>");
        return;
    }
    char src_arg[256];
    int ai = 0;
    int si = 0;
    while (args[ai] == ' ') ai = ai + 1;
    while (args[ai] && args[ai] != ' ') { src_arg[si] = args[ai]; si = si + 1; ai = ai + 1; }
    src_arg[si] = 0;
    while (args[ai] == ' ') ai = ai + 1;
    char dst_arg[256];
    int di = 0;
    while (args[ai] && args[ai] != ' ') { dst_arg[di] = args[ai]; di = di + 1; ai = ai + 1; }
    dst_arg[di] = 0;
    if (strlen(src_arg) == 0 || strlen(dst_arg) == 0) {
        println("mv: missing operand");
        return;
    }
    char abs_src[256];
    char abs_dst[256];
    resolve_path(abs_src, src_arg);
    resolve_path(abs_dst, dst_arg);
    char stat_buf[8];
    int st = vfs_stat(abs_dst, stat_buf);
    if (st >= 0) {
        if (stat_buf[4] == 1) {
            char *fname = basename(abs_src);
            char full_dst[256];
            append_filename(full_dst, abs_dst, fname);
            int ci = 0;
            while (full_dst[ci]) { abs_dst[ci] = full_dst[ci]; ci = ci + 1; }
            abs_dst[ci] = 0;
        }
    }
    int result = vfs_rename(abs_src, abs_dst);
    if (result < 0) {
        print("mv: cannot move '");
        print(src_arg);
        print("' to '");
        print(dst_arg);
        println("': failed");
    }
}
