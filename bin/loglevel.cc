// loglevel.cc — Get/set serial log level
// Usage: loglevel [debug|info|warn|error|panic]

void main() {
    char *args = get_args();
    if (!args || !*args) {
        print("Current log level: ");
        print(get_log_level_name());
        print("\nUsage: loglevel <debug|info|warn|error|panic>\n");
        return;
    }

    if (strcmp(args, "debug") == 0) {
        set_log_level(0);
    } else if (strcmp(args, "info") == 0) {
        set_log_level(1);
    } else if (strcmp(args, "warn") == 0) {
        set_log_level(2);
    } else if (strcmp(args, "error") == 0) {
        set_log_level(3);
    } else if (strcmp(args, "panic") == 0) {
        set_log_level(4);
    } else {
        print("Unknown level: ");
        print(args);
        print("\n");
        return;
    }
    print("Log level set to ");
    print(get_log_level_name());
    print("\n");
}
