//help: Search for text in files
//help: Usage: grep <pattern> <path> [path] ...
//help: Recursively searches files under directories and prints matches
//help: as path:line:content

int str_contains(char *s, char *needle) {
    if (!needle || needle[0] == 0) return 1;

    int i = 0;
    while (s[i]) {
        int j = 0;
        while (needle[j] && s[i + j] && s[i + j] == needle[j]) j = j + 1;
        if (needle[j] == 0) return 1;
        i = i + 1;
    }
    return 0;
}

int is_dot_name(char *name) {
    if (name[0] == '.' && name[1] == 0) return 1;
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) return 1;
    return 0;
}

void join_path(char *out, char *a, char *b) {
    int i = 0;
    int ai = 0;
    while (a[ai]) { out[i] = a[ai]; i = i + 1; ai = ai + 1; }
    if (i > 1 && out[i - 1] != '/') { out[i] = '/'; i = i + 1; }
    int bi = 0;
    while (b[bi]) { out[i] = b[bi]; i = i + 1; bi = bi + 1; }
    out[i] = 0;
}

void grep_file(char *path, char *pattern) {
    int fd = vfs_open(path, 0);
    if (fd < 0) return;

    char buf[256];
    char line[256];
    int li = 0;
    int line_no = 1;

    int r = vfs_read(fd, buf, 255);
    while (r > 0) {
        int i = 0;
        while (i < r) {
            char c = buf[i];
            i = i + 1;

            if (c == '\r') continue;

            if (c == '\n') {
                line[li] = 0;
                if (str_contains(line, pattern)) {
                    print(path);
                    print(":");
                    print_int(line_no);
                    print(":");
                    println(line);
                }
                li = 0;
                line_no = line_no + 1;
                continue;
            }

            if (li < 255) {
                line[li] = c;
                li = li + 1;
            }
        }

        r = vfs_read(fd, buf, 255);
    }

    if (li > 0) {
        line[li] = 0;
        if (str_contains(line, pattern)) {
            print(path);
            print(":");
            print_int(line_no);
            print(":");
            println(line);
        }
    }

    vfs_close(fd);
}

void grep_walk(char *path, char *pattern) {
    char st[8];
    if (vfs_stat(path, st) < 0) return;

    if (st[4] != 1) {
        grep_file(path, pattern);
        return;
    }

    int fd = vfs_open(path, 0);
    if (fd < 0) return;

    char ent[72];
    while (vfs_readdir(fd, ent) > 0) {
        char name[64];
        int ni = 0;
        while (ent[ni] && ni < 63) { name[ni] = ent[ni]; ni = ni + 1; }
        name[ni] = 0;

        if (is_dot_name(name)) continue;

        char child[256];
        join_path(child, path, name);

        if (ent[68] == 1) {
            grep_walk(child, pattern);
        } else if (ent[68] == 0) {
            grep_file(child, pattern);
        }
    }

    vfs_close(fd);
}

int parse_token(char *str, int start, char *out, int maxlen) {
    int i = start;
    while (str[i] == ' ' || str[i] == '\t') i = i + 1;
    if (str[i] == 0) { out[0] = 0; return 0; }

    int j = 0;
    while (str[i] != 0 && str[i] != ' ' && str[i] != '\t' && j < maxlen - 1) {
        out[j] = str[i];
        i = i + 1;
        j = j + 1;
    }
    out[j] = 0;
    return i - start;
}

void main() {
    char *args = (char*)get_args();
    char pattern[128];
    int pos = 0;

    int len = parse_token(args, pos, pattern, 128);
    if (len == 0) {
        println("Usage: grep <pattern> <path> [path] ...");
        return;
    }
    pos = pos + len;

    int had_path = 0;
    while (1) {
        char path_arg[256];
        int l = parse_token(args, pos, path_arg, 256);
        if (l == 0) break;
        pos = pos + l;

        char path[256];
        resolve_path(path_arg, path);
        grep_walk(path, pattern);
        had_path = 1;
    }

    if (!had_path) {
        println("Usage: grep <pattern> <path> [path] ...");
    }
}
