//help: Print last lines of a file
//help: Usage: tail [-n N] <file>
//help: Default N is 10. Reads up to 64KB.

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

int parse_int(char *s) {
    int v = 0;
    int i = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        i = i + 1;
    }
    return v;
}

void main() {
    char *args = (char*)get_args();
    char tok[256];
    int pos = 0;

    int n = 10;
    char path_arg[256];
    path_arg[0] = 0;

    int l = parse_token(args, pos, tok, 256);
    if (l == 0) {
        println("Usage: tail [-n N] <file>");
        return;
    }
    pos = pos + l;

    if (tok[0] == '-' && tok[1] == 'n' && tok[2] == 0) {
        l = parse_token(args, pos, tok, 256);
        if (l == 0) { println("tail: -n needs argument"); return; }
        pos = pos + l;
        n = parse_int(tok);
        l = parse_token(args, pos, path_arg, 256);
        if (l == 0) { println("tail: missing file"); return; }
    } else {
        int i = 0;
        while (tok[i]) { path_arg[i] = tok[i]; i = i + 1; }
        path_arg[i] = 0;
    }

    char path[256];
    resolve_path(path_arg, path);

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        print("tail: cannot open: ");
        println(path_arg);
        return;
    }

    /* Slurp up to 64KB. Truncate if larger. */
    char *blob = (char*)kmalloc(65537);
    if (!blob) {
        println("tail: out of memory");
        vfs_close(fd);
        return;
    }

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
    blob[total] = 0;

    /* Count newlines, then walk backward to find start of last n lines. */
    int newlines = 0;
    int i = 0;
    while (i < total) {
        if (blob[i] == '\n') newlines = newlines + 1;
        i = i + 1;
    }

    int target_skip = newlines - n;
    if (target_skip < 0) target_skip = 0;

    int start = 0;
    int seen = 0;
    while (start < total && seen < target_skip) {
        if (blob[start] == '\n') seen = seen + 1;
        start = start + 1;
    }

    int j = start;
    while (j < total) {
        putchar(blob[j]);
        j = j + 1;
    }

    kfree(blob);
}
