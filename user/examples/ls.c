/**
 * ls.c — ELF 'ls' command for CupidOS
 *
 * Lists directory contents using the VFS.
 *
 * Compile:
 *   gcc -m32 -fno-pie -nostdlib -static -ffreestanding -O2 \
 *       -Iuser -c user/examples/ls.c -o ls.o
 *   ld -m elf_i386 -Ttext=0x00400000 --oformat=elf32-i386 \
 *       -o ls ls.o
 */

#include "../cupid.h"

static void print_size(cupid_syscall_table_t *sys, uint32_t size) {
    (void)sys;
    if (size < 1024) {
        print_int(size);
        print(" B");
    } else if (size < 1024 * 1024) {
        print_int(size / 1024);
        print(" KB");
    } else {
        print_int(size / (1024 * 1024));
        print(" MB");
    }
}

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    /* Use CWD as default path */
    const char *path = shell_get_cwd();

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("ls: cannot open ");
        print(path);
        print("\n");
        exit();
    }

    cupid_dirent_t ent;
    int count = 0;

    while (readdir(fd, &ent) > 0) {
        /* Type indicator */
        if (ent.type == VFS_TYPE_DIR) {
            print("[DIR]  ");
        } else if (ent.type == VFS_TYPE_DEV) {
            print("[DEV]  ");
        } else {
            print("       ");
        }

        /* Name */
        print(ent.name);

        /* Size for files */
        if (ent.type == VFS_TYPE_FILE) {
            print("  (");
            print_size(sys, ent.size);
            print(")");
        }

        print("\n");
        count++;
    }

    close(fd);

    if (count == 0) {
        print("(empty directory)\n");
    }

    exit();
}
