// crashtest.cc — Test crash handling
// Usage: crashtest <type>
//   Types: panic, nullptr, divzero, overflow, stackoverflow

void main() {
    char *args = get_args();
    if (!args || !*args) {
        print("Usage: crashtest <type>\n");
        print("  Types: panic, nullptr, divzero, overflow, stackoverflow\n");
        return;
    }

    if (strcmp(args, "panic") == 0) {
        kernel_panic("Test panic from crashtest");
    } else if (strcmp(args, "nullptr") == 0) {
        print("Dereferencing NULL pointer...\n");
        crashtest_nullptr();
    } else if (strcmp(args, "divzero") == 0) {
        print("Dividing by zero...\n");
        crashtest_divzero();
    } else if (strcmp(args, "overflow") == 0) {
        print("Allocating and overflowing buffer...\n");
        crashtest_overflow();
    } else if (strcmp(args, "stackoverflow") == 0) {
        print("Triggering stack overflow...\n");
        crashtest_stackoverflow();
    } else {
        print("Unknown crash test: ");
        print(args);
        print("\n");
    }
}
