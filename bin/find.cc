//help: Find files and directories recursively
//help: Usage: find [path] [name]
//help: Lists all paths under [path] (default: current directory).
//help: If [name] is provided, only paths containing that text are shown.

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

void find_walk(char *path, char *needle) {
    if (str_contains(path, needle)) {
        println(path);
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

        int type = ent[68];
        if (str_contains(child, needle)) {
            println(child);
        }

        if (type == 1) {
            find_walk(child, needle);
        }
    }

    vfs_close(fd);
}

void main() {
    char *args = (char*)get_args();
    char tok1[256];
    char tok2[256];
    tok1[0] = 0;
    tok2[0] = 0;

    int i = 0;
    while (args[i] == ' ' || args[i] == '\t') i = i + 1;
    int a = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && a < 255) {
        tok1[a] = args[i]; a = a + 1; i = i + 1;
    }
    tok1[a] = 0;

    while (args[i] == ' ' || args[i] == '\t') i = i + 1;
    int b = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && b < 255) {
        tok2[b] = args[i]; b = b + 1; i = i + 1;
    }
    tok2[b] = 0;

    char root[256];
    char needle[256];

    if (tok1[0] == 0) {
        resolve_path(".", root);
        needle[0] = 0;
    } else {
        resolve_path(tok1, root);
        int n = 0;
        while (tok2[n]) { needle[n] = tok2[n]; n = n + 1; }
        needle[n] = 0;
    }

    char st[8];
    if (vfs_stat(root, st) < 0) {
        print("find: cannot access ");
        println(root);
        return;
    }

    if (st[4] != 1) {
        if (str_contains(root, needle)) println(root);
        return;
    }

    find_walk(root, needle);
}
