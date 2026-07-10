/**
 * hello.c - Example ELF program for CupidOS
 *
 * Compile:
 *   gcc -m32 -fno-pie -nostdlib -static -ffreestanding -O2 \
 *       -Iuser -c user/examples/hello.c -o hello.o
 *   cupidld -m elf_i386 --text-address 0x00D00000 --entry _start \
 *       -o hello hello.o
*/

#include "../cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    print("Hello from an ELF program!\n");
    print("  PID: ");
    print_int(getpid());
    print("\n");
    print("  Uptime: ");
    print_int(uptime_ms());
    print(" ms\n");
    print("  CWD: ");
    print(shell_get_cwd());
    print("\n");

    exit();
}
