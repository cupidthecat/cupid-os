//help: Count lines, words, and bytes in a file
//help: Usage: wc [-l|-w|-c] <file>
//help: Default prints all three counts. Flags select one:
//help:   -l  lines only
//help:   -w  words only
//help:   -c  bytes only

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
    char tok1[64];
    char tok2[256];
    int pos = 0;

    int l = parse_token(args, pos, tok1, 64);
    if (l == 0) {
        println("Usage: wc [-l|-w|-c] <file>");
        return;
    }
    pos = pos + l;

    char mode = 0;
    char *path_arg;
    if (tok1[0] == '-') {
        if (tok1[1] == 'l' && tok1[2] == 0) mode = 'l';
        else if (tok1[1] == 'w' && tok1[2] == 0) mode = 'w';
        else if (tok1[1] == 'c' && tok1[2] == 0) mode = 'c';
        else {
            print("wc: unknown flag: ");
            println(tok1);
            return;
        }
        l = parse_token(args, pos, tok2, 256);
        if (l == 0) {
            println("Usage: wc [-l|-w|-c] <file>");
            return;
        }
        path_arg = tok2;
    } else {
        path_arg = tok1;
    }

    char path[256];
    resolve_path(path_arg, path);

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        print("wc: cannot open: ");
        println(path_arg);
        return;
    }

    int lines = 0;
    int words = 0;
    int bytes = 0;
    int in_word = 0;

    char buf[256];
    int r = vfs_read(fd, buf, 256);
    while (r > 0) {
        int i = 0;
        while (i < r) {
            char c = buf[i];
            bytes = bytes + 1;
            if (c == '\n') lines = lines + 1;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else {
                if (!in_word) {
                    words = words + 1;
                    in_word = 1;
                }
            }
            i = i + 1;
        }
        r = vfs_read(fd, buf, 256);
    }
    vfs_close(fd);

    if (mode == 'l') {
        print_int(lines);
        print("\n");
    } else if (mode == 'w') {
        print_int(words);
        print("\n");
    } else if (mode == 'c') {
        print_int(bytes);
        print("\n");
    } else {
        print_int(lines);
        print(" ");
        print_int(words);
        print(" ");
        print_int(bytes);
        print(" ");
        println(path_arg);
    }
}
