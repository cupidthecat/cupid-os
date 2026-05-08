//help: Sort lines of a file
//help: Usage: sort [-r] <file>
//help: Reads up to 64KB. Up to 4096 lines. -r reverses sort order.

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

/* Lex compare two NUL-terminated strings. Returns <0, 0, >0. */
int line_cmp(char *blob, int a, int b) {
    int i = 0;
    while (1) {
        char ca = blob[a + i];
        char cb = blob[b + i];
        if (ca == 0 && cb == 0) return 0;
        if (ca == 0) return -1;
        if (cb == 0) return 1;
        if (ca != cb) return (int)ca - (int)cb;
        i = i + 1;
    }
}

void main() {
    char *args = (char*)get_args();
    char tok[256];
    int pos = 0;

    int reverse = 0;
    char path_arg[256];
    path_arg[0] = 0;

    int l = parse_token(args, pos, tok, 256);
    if (l == 0) {
        println("Usage: sort [-r] <file>");
        return;
    }
    pos = pos + l;

    if (tok[0] == '-' && tok[1] == 'r' && tok[2] == 0) {
        reverse = 1;
        l = parse_token(args, pos, path_arg, 256);
        if (l == 0) { println("sort: missing file"); return; }
    } else {
        int i = 0;
        while (tok[i]) { path_arg[i] = tok[i]; i = i + 1; }
        path_arg[i] = 0;
    }

    char path[256];
    resolve_path(path_arg, path);

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        print("sort: cannot open: ");
        println(path_arg);
        return;
    }

    char *blob = (char*)kmalloc(65537);
    if (!blob) { println("sort: oom"); vfs_close(fd); return; }

    int total = 0;
    char buf[256];
    int r = vfs_read(fd, buf, 256);
    while (r > 0 && total < 65536) {
        int i = 0;
        while (i < r && total < 65536) {
            blob[total] = buf[i];
            total = total + 1;
            i = i + 1;
        }
        r = vfs_read(fd, buf, 256);
    }
    vfs_close(fd);

    /* In-place: replace each '\n' with NUL, build offsets[]. */
    int max_lines = 4096;
    int *offs = (int*)kmalloc(max_lines * 4);
    if (!offs) { println("sort: oom"); kfree(blob); return; }

    int nlines = 0;
    int start = 0;
    int i = 0;
    while (i < total && nlines < max_lines) {
        if (blob[i] == '\n') {
            blob[i] = 0;
            offs[nlines] = start;
            nlines = nlines + 1;
            start = i + 1;
        }
        i = i + 1;
    }
    if (start < total && nlines < max_lines) {
        blob[total] = 0;
        offs[nlines] = start;
        nlines = nlines + 1;
    }

    /* Insertion sort. */
    int a = 1;
    while (a < nlines) {
        int key = offs[a];
        int b = a - 1;
        while (b >= 0) {
            int c = line_cmp(blob, offs[b], key);
            if (reverse) c = -c;
            if (c <= 0) break;
            offs[b + 1] = offs[b];
            b = b - 1;
        }
        offs[b + 1] = key;
        a = a + 1;
    }

    int k = 0;
    while (k < nlines) {
        char *p = blob + offs[k];
        int j = 0;
        while (p[j]) {
            putchar(p[j]);
            j = j + 1;
        }
        putchar('\n');
        k = k + 1;
    }

    kfree(offs);
    kfree(blob);
}
