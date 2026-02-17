// memleak.cc - Detect memory leaks
// Usage: memleak [threshold_sec]

void main() {
    char *args = get_args();
    int threshold = 60000;
    if (args && *args) {
        int val = 0;
        while (*args >= 48 && *args <= 57) {
            val = val * 10 + (*args - 48);
            args = args + 1;
        }
        if (val > 0) {
            threshold = val * 1000;
        }
    }
    detect_memory_leaks(threshold);
}
