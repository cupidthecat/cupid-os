//help: AC97 master volume - print or set 0..100
//help: Usage: volume [0..100]

int parse_int(char *s) {
    int v = 0;
    int i = 0;
    if (!s) return -1;
    if (s[0] == 0) return -1;
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        i = i + 1;
    }
    if (s[i] != 0) return -1;
    return v;
}

void main() {
    char *args = (char*)get_args();

    if (ac97_is_present_int() == 0) {
        println("volume: no AC97 device");
        return;
    }

    if (strlen(args) == 0) {
        int cur = ac97_get_master_volume();
        print("volume: ");
        print_int(cur);
        println("");
        return;
    }

    int n = parse_int(args);
    if (n < 0 || n > 100) {
        println("volume: argument must be 0..100");
        return;
    }

    ac97_set_master_volume((uint8_t)n);
    print("volume set to ");
    print_int(n);
    println("");
}

main();
