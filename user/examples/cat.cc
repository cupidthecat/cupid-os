/**
 * cat.cc - Cupid OS ELF file display program.
 *
 * The kernel does not pass command-line arguments yet, so this example reads
 * the fixed runtime fixture staged beside the checked executables.
 * user/Makefile compiles and links it with the checked CupidC and CupidLD
 * seeds.
 */

#include "../cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    const char *path = "/disk/catfix.txt";

    cupid_stat_t st;
    if (stat(path, &st) < 0) {
        print("cat: ");
        print(path);
        print(": no such file\n");
        exit();
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("cat: cannot open ");
        print(path);
        print("\n");
        exit();
    }

    char buf[512];
    int n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        print(buf);
    }

    close(fd);
    exit();
}
