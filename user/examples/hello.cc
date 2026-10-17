/**
 * hello.cc - Small Cupid OS ELF program.
 *
 * user/Makefile compiles and links this source with the checked CupidC and
 * CupidLD seeds.
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
