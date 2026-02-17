// sysinfo.cc - Show system information

void main() {
    int ms = uptime_ms();
    print("System Information:\n");
    print("  Uptime: ");
    print_int(ms / 1000);
    putchar(46);
    print_int(ms % 1000);
    print("s\n");

    print("  CPU Freq: ");
    print_int(get_cpu_mhz());
    print(" MHz\n");

    print("  Timer Freq: ");
    print_int(timer_get_frequency());
    print(" Hz\n");

    int free_pg = pmm_free_pages();
    int total_pg = pmm_total_pages();
    print("  Memory: ");
    print_int(free_pg * 4);
    print(" KB free / ");
    print_int(total_pg * 4);
    print(" KB total\n");

    memstats();
}
