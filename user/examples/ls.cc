/**
 * ls.cc - Cupid OS ELF directory listing program.
 *
 * user/Makefile compiles and links this source with the checked CupidC and
 * CupidLD seeds.
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

    /* Use the shell's current directory until user programs receive argv. */
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
