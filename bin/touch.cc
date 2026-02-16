//help: Create empty files
//help: Usage: touch <file1> [file2] ...
//help: Creates files if they do not exist. Existing files are left unchanged.

enum {
    VFS_WRONLY = 1,
    VFS_CREAT  = 256
};

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
    if (strlen(args) == 0) {
        println("Usage: touch <file1> [file2] ...");
        return;
    }

    int pos = 0;
    int created = 0;
    int errors = 0;
    char name[256];
    char path[256];

    while (1) {
        int len = parse_token(args, pos, name, 256);
        if (len == 0) break;
        pos = pos + len;

        resolve_path(name, path);
        int fd = vfs_open(path, VFS_WRONLY + VFS_CREAT);
        if (fd < 0) {
            print("touch: cannot touch '");
            print(name);
            println("'");
            errors = errors + 1;
            continue;
        }

        vfs_close(fd);
        created = created + 1;
    }

    if (created + errors > 1 && errors > 0) {
        print("touch: ");
        print_int(errors);
        println(" failed");
    }
}
