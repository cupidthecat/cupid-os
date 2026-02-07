//help: Show system uptime
//help: Usage: time
//help: Displays how long the system has been running
//help: in seconds and milliseconds.

void main() {
    int ms = uptime_ms();
    int seconds = ms / 1000;
    int remainder = ms % 1000;
    print("Uptime: ");
    print_int(seconds);
    putchar('.');
    putchar((char)(48 + (remainder / 100)));
    putchar((char)(48 + ((remainder / 10) % 10)));
    putchar((char)(48 + (remainder % 10)));
    print("s (");
    print_int(ms);
    println(" ms)");
}
