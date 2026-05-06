//help: Print first lines of a file
//help: Usage: head [-n N] <file>
//help: Default N is 10. Output truncated at 64KB.

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
        println("Usage: head [-n N] <file>");
        return;
    }
    pos = pos + l;

    if (tok[0] == '-' && tok[1] == 'n' && tok[2] == 0) {
        l = parse_token(args, pos, tok, 256);
        if (l == 0) { println("head: -n needs argument"); return; }
        pos = pos + l;
        n = parse_int(tok);
        l = parse_token(args, pos, path_arg, 256);
        if (l == 0) { println("head: missing file"); return; }
    } else {
        int i = 0;
        while (tok[i]) { path_arg[i] = tok[i]; i = i + 1; }
        path_arg[i] = 0;
    }

    char path[256];
    resolve_path(path_arg, path);

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        print("head: cannot open: ");
        println(path_arg);
        return;
    }

    int printed_lines = 0;
    int total_bytes = 0;
    char buf[256];
    int r = vfs_read(fd, buf, 256);
    while (r > 0 && printed_lines < n) {
        int i = 0;
        while (i < r && printed_lines < n) {
            putchar(buf[i]);
            if (buf[i] == '\n') printed_lines = printed_lines + 1;
            i = i + 1;
            total_bytes = total_bytes + 1;
            if (total_bytes > 65536) {
                println("\n[head: output truncated at 64KB]");
                vfs_close(fd);
                return;
            }
        }
        if (printed_lines >= n) break;
        r = vfs_read(fd, buf, 256);
    }
    vfs_close(fd);
}
