/**
 * cat.c — ELF 'cat' command for CupidOS
 *
 * Reads and displays file contents.
 * Note: This is a standalone ELF program. Since the kernel doesn't
 * pass command-line arguments yet, it reads from a hardcoded path
 * or can be extended to parse args from the syscall table.
 *
 * Compile:
 *   gcc -m32 -fno-pie -nostdlib -static -ffreestanding -O2 \
 *       -Iuser -c user/examples/cat.c -o cat.o
 *   ld -m elf_i386 -Ttext=0x00400000 --oformat=elf32-i386 \
 *       -o cat cat.o
 */

#include "../cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    /* Demo: read and display /home/readme.txt if it exists */
    const char *path = "/home/readme.txt";

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
