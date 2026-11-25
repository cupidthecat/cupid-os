/**
 * @file dglibc_selftest.inc
 * @brief Exercise the Doom libc shim against Cupid OS storage and process APIs.
 */

extern int M_FileExists(char *filename);
extern int M_ConfigFilesystemTest(void);
extern void I_AtExit(void (*func)(void), int run_if_error);
extern void I_Quit(void) __attribute__((noreturn));
extern void I_Error(char *error, ...) __attribute__((noreturn));
extern void I_ResetExitState(void);
extern long strtol(const char *s, char **endp, int base);
extern unsigned long strtoul(const char *s, char **endp, int base);
extern int sscanf(const char *s, const char *fmt, ...);

static int dg_lifecycle_trace[8];
static int dg_lifecycle_count;
static int dg_lifecycle_base;

static void dg_lifecycle_first(void) {
    if (dg_lifecycle_count < 8) {
        dg_lifecycle_trace[dg_lifecycle_count++] = dg_lifecycle_base + 1;
    }
}

static void dg_lifecycle_second(void) {
    if (dg_lifecycle_count < 8) {
        dg_lifecycle_trace[dg_lifecycle_count++] = dg_lifecycle_base + 2;
    }
}

static void dg_lifecycle_error(void) {
    if (dg_lifecycle_count < 8) {
        dg_lifecycle_trace[dg_lifecycle_count++] = dg_lifecycle_base + 3;
    }
}

static void dg_lifecycle_normal_only(void) {
    if (dg_lifecycle_count < 8) {
        dg_lifecycle_trace[dg_lifecycle_count++] = dg_lifecycle_base + 4;
    }
}

static int dg_lifecycle_quit_cycle(int base) {
    dg_jmp_buf env;
    int jumped = dg_setjmp(env);
    if (jumped == 0) {
        I_ResetExitState();
        dg_lifecycle_base = base;
        I_AtExit(dg_lifecycle_first, 0);
        I_AtExit(dg_lifecycle_second, 0);
        dg_arm_exit(env);
        I_Quit();
    }
    dg_disarm_exit();
    I_ResetExitState();
    return jumped != 0;
}

static int dg_lifecycle_error_cycle(int base) {
    dg_jmp_buf env;
    int jumped = dg_setjmp(env);
    if (jumped == 0) {
        I_ResetExitState();
        dg_lifecycle_base = base;
        I_AtExit(dg_lifecycle_error, 1);
        I_AtExit(dg_lifecycle_normal_only, 0);
        dg_arm_exit(env);
        I_Error((char *)"dglibc lifecycle error %d", base);
    }
    dg_disarm_exit();
    I_ResetExitState();
    return jumped != 0;
}

static int dglibc_expect_path(const char *input, const char *expected) {
    char buffer[128];
    const char *resolved = dg_resolve_path(input, buffer, (uint32_t)sizeof(buffer));
    if (!resolved || strcmp(resolved, expected) != 0) {
        serial_write_string("[FAIL] dglibc resolved the wrong path for ");
        serial_write_string(input ? input : "(null)");
        serial_write_string("\n");
        return 0;
    }
    return 1;
}

static int dglibc_test_body(void) {
    /* snprintf */
    {
        char b[64];
        int n = dg_snprintf(b, sizeof(b), "hello %s %d %x %.2f", "world", 42, 0xCAFEu, 1.25);
        (void)n;
        const char *expected = "hello world 42 cafe 1.25";
        const char *p = b;
        const char *q = expected;
        while (*q) {
            if (*p++ != *q++) {
                break;
            }
        }
        if (*q != 0 || *p != 0) {
            serial_write_string("[FAIL] dglibc snprintf: ");
            serial_write_string(b);
            serial_write_string("\n");
            return 1;
        }
        if (dg_fclose(dg_stdout) != 0 || dg_ferror(dg_stdout)) {
            serial_write_string("[FAIL] dglibc damaged a static stream\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc snprintf\n");
    }

    /* malloc/free */
    {
        void *p = dg_malloc(1024u);
        if (!p) {
            serial_write_string("[FAIL] dg_malloc\n");
            return 1;
        }
        dg_free(p);
        p = dg_malloc(0xfffffff8u);
        if (p || dg_errno != DG_ENOMEM) {
            if (p)
                dg_free(p);
            serial_write_string("[FAIL] dg_malloc accepted header overflow\n");
            return 1;
        }
        p = dg_calloc(0x80000000u, 2u);
        if (p || dg_errno != DG_ENOMEM) {
            if (p)
                dg_free(p);
            serial_write_string("[FAIL] dg_calloc accepted product overflow\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc malloc/free\n");
    }

    /* setjmp/longjmp and the shell exit envelope */
    {
        dg_jmp_buf env;
        int rc = dg_setjmp(env);
        if (rc == 0) {
            dg_longjmp(env, 7);
        }
        if (rc == 7) {
            dg_arm_exit(env);
            dg_exit(9);
        }
        if (rc != 9) {
            serial_write_string("[FAIL] dglibc jump or exit envelope\n");
            return 1;
        }
        dg_disarm_exit();
        serial_write_string("[PASS] dglibc setjmp/longjmp and exit envelope\n");
    }

    /* Checked integer parsing used by Doom's configuration reader. */
    {
        char *end;
        int parsed = -1;
        const char *no_digits = "+";

        dg_errno = 0;
        if (strtol("2147483647x", &end, 10) != 2147483647L || *end != 'x' ||
            strtol("-2147483648", &end, 10) != (-2147483647L - 1L) || *end != '\0' ||
            strtol("0x2a", &end, 0) != 42L || *end != '\0' || strtol(no_digits, &end, 10) != 0L ||
            end != no_digits) {
            serial_write_string("[FAIL] dglibc parsed a bounded integer incorrectly\n");
            return 1;
        }
        dg_errno = 0;
        if (strtol("2147483648", &end, 10) != 2147483647L || dg_errno != 34 || *end != '\0') {
            serial_write_string("[FAIL] dglibc missed signed integer overflow\n");
            return 1;
        }
        dg_errno = 0;
        if (strtoul("4294967296", &end, 10) != 0xffffffffu || dg_errno != 34 || *end != '\0' ||
            sscanf(" 0x2a", " 0x%x", &parsed) != 1 || parsed != 42 ||
            sscanf(" sign", " %d", &parsed) != 0) {
            serial_write_string("[FAIL] dglibc accepted an unsafe integer conversion\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc checked integer parsing\n");
    }

    /* Exit callbacks belong to one in-process Doom launch. */
    {
        dg_lifecycle_count = 0;
        if (!dg_lifecycle_quit_cycle(100) || !dg_lifecycle_quit_cycle(200) ||
            !dg_lifecycle_error_cycle(300) || !dg_lifecycle_error_cycle(400) ||
            dg_lifecycle_count != 6 || dg_lifecycle_trace[0] != 102 ||
            dg_lifecycle_trace[1] != 101 || dg_lifecycle_trace[2] != 202 ||
            dg_lifecycle_trace[3] != 201 || dg_lifecycle_trace[4] != 303 ||
            dg_lifecycle_trace[5] != 403) {
            serial_write_string("[FAIL] dglibc reused Doom exit callbacks\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc Doom exit callback lifecycle\n");
    }

    /* Active Doom config and save paths stay under /home/doom. */
    {
        char path_buffer[128];
        char short_buffer[8];
        DG_FILE *directory;
        DG_FILE *missing;
        vfs_mkdir("/home/doom");
        if (!dglibc_expect_path(".", "/home/doom") ||
            !dglibc_expect_path(".savegame", "/home/doom") ||
            !dglibc_expect_path("./.savegame", "/home/doom") ||
            !dglibc_expect_path(".default.cfg", "/home/doom/.default.cfg") ||
            !dglibc_expect_path(".doomgenericdoom.cfg", "/home/doom/.doomgenericdoom.cfg") ||
            !dglibc_expect_path("temp.dsg", "/home/doom/temp.dsg") ||
            !dglibc_expect_path("./.savegame/temp.dsg", "/home/doom/temp.dsg") ||
            !dglibc_expect_path("./.savegame/doomsav0.dsg", "/home/doom/doomsav0.dsg") ||
            !dglibc_expect_path("/tmp/recovery.dsg", "/tmp/recovery.dsg")) {
            return 1;
        }
        if (dg_resolve_path("./.savegame/nested/file.dsg", path_buffer,
                            (uint32_t)sizeof(path_buffer)) != NULL) {
            serial_write_string("[FAIL] dglibc accepted a nested save path\n");
            return 1;
        }
        if (dg_resolve_path("./.savegame/../escape.dsg", path_buffer,
                            (uint32_t)sizeof(path_buffer)) != NULL) {
            serial_write_string("[FAIL] dglibc accepted save traversal\n");
            return 1;
        }
        if (dg_resolve_path(".doomgenericdoom.cfg", short_buffer, (uint32_t)sizeof(short_buffer)) !=
            NULL) {
            serial_write_string("[FAIL] dglibc truncated a config path\n");
            return 1;
        }
        directory = dg_fopen("/home/doom", "rb");
        if (directory || dg_errno != DG_EISDIR) {
            if (directory) {
                dg_fclose(directory);
            }
            serial_write_string("[FAIL] dglibc did not report a directory open\n");
            return 1;
        }
        missing = dg_fopen("/home/doom/dglibc-missing.cfg", "rb");
        if (missing || dg_errno != DG_ENOENT) {
            if (missing) {
                dg_fclose(missing);
            }
            serial_write_string("[FAIL] dglibc did not report a missing file\n");
            return 1;
        }
        if (dg_fopen("/home/doom/dglibc-invalid.cfg", "rx") != NULL || dg_errno != DG_EINVAL) {
            serial_write_string("[FAIL] dglibc accepted an invalid file mode\n");
            return 1;
        }
        if (dg_fopen("../doomsav0.dsg", "wb") != NULL || dg_errno != DG_EINVAL) {
            serial_write_string("[FAIL] dglibc accepted an unrelated save path\n");
            return 1;
        }
        if (!M_FileExists("/home/doom") || M_FileExists("/home/doom/dglibc-missing.cfg")) {
            serial_write_string("[FAIL] dglibc errno did not cross Doom objects\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc Doom path resolution\n");
        serial_write_string("[PASS] dglibc shared errno bridge\n");
    }

    if (M_ConfigFilesystemTest() != 0) {
        serial_write_string("[FAIL] dglibc Doom config round trip\n");
        return 1;
    }
    serial_write_string("[PASS] dglibc Doom config round trip\n");

    /* Config files use the same relative bridge without touching live names. */
    {
        const char *config = ".dglibc-test.cfg";
        const char *payload = "bridge=ready\n";
        char oversized[520];
        char restored[16];
        DG_FILE *f;
        uint32_t mode_flags;
        uint32_t oversized_index;

        vfs_mkdir("/home/doom");
        vfs_unlink("/home/doom/.dglibc-test.cfg");
        if (!dg_mode_flags("rb+", &mode_flags) || mode_flags != O_RDWR ||
            !dg_mode_flags("r+b", &mode_flags) || mode_flags != O_RDWR ||
            dg_mode_flags("r++", &mode_flags) || dg_mode_flags("rbb", &mode_flags)) {
            serial_write_string("[FAIL] dglibc parsed file modes incorrectly\n");
            return 1;
        }
        f = dg_fopen(config, "wb");
        if (!f) {
            serial_write_string("[FAIL] dglibc could not open a relative config\n");
            return 1;
        }
        if (vfs_read(f->fd, restored, 1u) != VFS_EACCES) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc read through a write-only stream\n");
            return 1;
        }
        if (vfs_seek(f->fd, 0x7fffffff, SEEK_SET) != 0x7fffffff ||
            vfs_seek(f->fd, 1, SEEK_CUR) != VFS_EINVAL ||
            vfs_write(f->fd, payload, 0xffffffffu) != VFS_ENOSPC ||
            vfs_seek(f->fd, 0, SEEK_SET) != 0) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc accepted a HomeFS offset overflow\n");
            return 1;
        }
        if (dg_fwrite(payload, 0xffffffffu, 2u, f) != 0u || dg_errno != DG_EINVAL || !f->is_err) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc accepted a write size overflow\n");
            return 1;
        }
        dg_clearerr(f);
        for (oversized_index = 0u; oversized_index + 1u < (uint32_t)sizeof(oversized);
             oversized_index++) {
            oversized[oversized_index] = 'x';
        }
        oversized[sizeof(oversized) - 1u] = '\0';
        if (dg_fprintf(f, "%s", oversized) != -1 || dg_errno != DG_EIO || !dg_ferror(f)) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc accepted oversized formatted output\n");
            return 1;
        }
        dg_clearerr(f);
        if (dg_fputs("bridge=", f) != 0 || dg_fprintf(f, "%s\n", "ready") != 6 ||
            dg_fflush(f) != 0 || dg_ferror(f)) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc could not write a relative config\n");
            return 1;
        }
        if (dg_fclose(f) != 0) {
            serial_write_string("[FAIL] dglibc could not close a relative config\n");
            return 1;
        }
        f = dg_fopen(config, "rb");
        if (!f) {
            serial_write_string("[FAIL] dglibc could not reopen a relative config\n");
            return 1;
        }
        if (vfs_write(f->fd, payload, 1u) != VFS_EACCES) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc wrote through a read-only stream\n");
            return 1;
        }
        if (dg_fread(restored, 0xffffffffu, 2u, f) != 0u || dg_errno != DG_EINVAL || !f->is_err) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc accepted a read size overflow\n");
            return 1;
        }
        dg_clearerr(f);
        if (dg_fread(restored, 0x80000000u, 1u, f) != 0u || !f->is_eof || f->is_err ||
            dg_fseek(f, 0, SEEK_SET) != 0) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc mishandled a short wide read\n");
            return 1;
        }
        if (dg_fread(restored, 1u, 13u, f) != 13u || dg_fclose(f) != 0 ||
            memcmp(restored, payload, 13u) != 0) {
            serial_write_string("[FAIL] dglibc could not read a relative config\n");
            return 1;
        }
        if (vfs_open("/home/doom/.dglibc-test.cfg", O_TRUNC) != VFS_EINVAL ||
            vfs_open("/home/doom/.dglibc-test.cfg", O_APPEND) != VFS_EINVAL) {
            serial_write_string("[FAIL] dglibc accepted read-only mutation flags\n");
            return 1;
        }
        f = dg_fopen(config, "ab+");
        if (!f || dg_fseek(f, 0, SEEK_SET) != 0 || dg_fwrite("!", 1u, 1u, f) != 1u ||
            dg_fclose(f) != 0) {
            serial_write_string("[FAIL] dglibc could not append after a seek\n");
            return 1;
        }
        f = dg_fopen(config, "rb");
        if (!f || dg_fread(restored, 1u, 14u, f) != 14u || dg_fclose(f) != 0 ||
            memcmp(restored, payload, 13u) != 0 || restored[13] != '!') {
            serial_write_string("[FAIL] dglibc append did not preserve the file tail\n");
            return 1;
        }
        if (remove(config) != 0) {
            serial_write_string("[FAIL] dglibc could not remove the test config\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc synthetic config filesystem bridge\n");
    }

    /* Doom save files use a relative virtual directory. */
    {
        const char *dir = "/home/dglibc-test-dir";
        const char *temp = "./.savegame/dglibc-test-temp.dsg";
        const char *save = "./.savegame/dglibc-test-save.dsg";
        const char *payload = "Cupid Doom save bridge\n";
        const char *destination_dir = "/home/dglibc-test-destination-dir";
        const char *missing_parent = "/home/doom/dglibc-missing-parent";
        const char *missing_destination = "/home/doom/dglibc-missing-parent/save.dsg";
        char restored[32];
        DG_FILE *f;
        DG_FILE *open_target;
        int directory_fd;
        vfs_stat_t st;

        vfs_mkdir("/home/doom");
        vfs_unlink(dir);
        vfs_unlink("/home/doom/dglibc-test-temp.dsg");
        vfs_unlink("/home/doom/dglibc-test-save.dsg");
        vfs_unlink(missing_destination);
        vfs_unlink(missing_parent);
        vfs_unlink(destination_dir);

        if (mkdir("./.savegame/", 0755u) == 0 || dg_errno != DG_EEXIST) {
            serial_write_string("[FAIL] dglibc save directory lost its existing state\n");
            return 1;
        }
        if (mkdir(dir, 0755u) != 0 || vfs_stat(dir, &st) != VFS_OK || st.type != VFS_TYPE_DIR) {
            serial_write_string("[FAIL] dglibc could not create the Doom save directory\n");
            return 1;
        }
        if (mkdir(dir, 0755u) == 0 || dg_errno != DG_EEXIST) {
            serial_write_string("[FAIL] dglibc mkdir hid an existing directory\n");
            return 1;
        }
        if (remove(dir) != 0 || vfs_stat(dir, &st) != VFS_ENOENT) {
            serial_write_string("[FAIL] dglibc did not remove the test directory\n");
            return 1;
        }
        if (remove(dir) == 0 || dg_errno != DG_ENOENT) {
            serial_write_string("[FAIL] dglibc remove hid a missing path\n");
            return 1;
        }

        f = dg_fopen(save, "wb");
        if (!f || dg_fwrite("old\n", 1u, 4u, f) != 4u || dg_fclose(f) != 0) {
            serial_write_string("[FAIL] dglibc could not seed the old save\n");
            return 1;
        }

        f = dg_fopen(temp, "wb");
        if (!f) {
            serial_write_string("[FAIL] dglibc could not write the temporary save\n");
            return 1;
        }
        if (dg_fwrite(payload, 1u, 23u, f) != 23u) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc could not write the temporary save\n");
            return 1;
        }
        if (dg_fclose(f) != 0) {
            serial_write_string("[FAIL] dglibc could not close the temporary save\n");
            return 1;
        }
        if (rename(temp, save) != 0) {
            serial_write_string("[FAIL] dglibc could not commit the temporary save\n");
            return 1;
        }
        f = dg_fopen(temp, "rb");
        if (f) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc rename left the temporary save behind\n");
            return 1;
        }
        f = dg_fopen(save, "rb");
        if (!f || dg_fread(restored, 1u, 23u, f) != 23u || dg_fclose(f) != 0 ||
            memcmp(restored, payload, 23u) != 0) {
            serial_write_string("[FAIL] dglibc did not preserve the committed save\n");
            return 1;
        }
        open_target = dg_fopen(save, "rb");
        f = dg_fopen(temp, "wb");
        if (!open_target || !f || dg_fwrite(payload, 1u, 23u, f) != 23u || dg_fclose(f) != 0 ||
            rename(temp, save) == 0 || dg_errno != DG_EBUSY || dg_fclose(open_target) != 0 ||
            rename(temp, save) != 0) {
            serial_write_string("[FAIL] dglibc replaced an open HomeFS target\n");
            return 1;
        }
        directory_fd = vfs_open("/home/doom", O_RDONLY);
        if (directory_fd < 0 || vfs_unlink("/home/doom/dglibc-test-save.dsg") != VFS_EBUSY ||
            vfs_close(directory_fd) != VFS_OK) {
            serial_write_string("[FAIL] dglibc invalidated a HomeFS iterator\n");
            return 1;
        }
        if (rename(save, save) != 0) {
            serial_write_string("[FAIL] dglibc changed an existing same-path rename\n");
            return 1;
        }
        if (rename(save, "/home/doom//dglibc-test-save.dsg") != 0) {
            serial_write_string("[FAIL] dglibc did not preserve an aliased rename\n");
            return 1;
        }
        if (vfs_rename("/home/doom/dglibc-test-save.dsg", missing_destination) != VFS_ENOENT ||
            vfs_stat("/home/doom/dglibc-test-save.dsg", &st) != VFS_OK ||
            vfs_stat(missing_parent, &st) != VFS_ENOENT) {
            serial_write_string("[FAIL] dglibc HomeFS rename created a parent\n");
            return 1;
        }
        if (mkdir(destination_dir, 0755u) != 0 || rename(save, destination_dir) == 0 ||
            dg_errno != DG_EISDIR || vfs_stat("/home/doom/dglibc-test-save.dsg", &st) != VFS_OK ||
            vfs_stat(destination_dir, &st) != VFS_OK || st.type != VFS_TYPE_DIR) {
            serial_write_string("[FAIL] dglibc rename damaged a directory boundary\n");
            return 1;
        }
        if (remove(destination_dir) != 0) {
            serial_write_string("[FAIL] dglibc could not remove the rename target directory\n");
            return 1;
        }
        if (rename(temp, save) == 0 || dg_errno != DG_ENOENT) {
            serial_write_string("[FAIL] dglibc rename hid a missing source\n");
            return 1;
        }
        if (rename("./.savegame/missing.dsg", "./.savegame/missing.dsg") == 0 ||
            dg_errno != DG_ENOENT) {
            serial_write_string("[FAIL] dglibc renamed a missing path to itself\n");
            return 1;
        }
        if (remove(save) != 0 || remove(save) == 0 || dg_errno != DG_ENOENT) {
            serial_write_string("[FAIL] dglibc remove did not report the save state\n");
            return 1;
        }
        if (dg_fopen("dglibc-relative.tmp", "wb") != NULL || dg_errno != DG_EINVAL) {
            serial_write_string("[FAIL] dglibc accepted an unrelated relative path\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc synthetic save filesystem bridge\n");
    }

    /* Native rename stays inside one mounted filesystem. */
    {
        const char *source = "/tmp/dglibc-rename-source";
        const char *destination = "/tmp/dglibc-rename-destination";
        const char *replacement = "/tmp/dglibc-rename-replacement";
        const char *cross_mount = "/home/doom/dglibc-cross-mount";
        const char *missing_parent = "/tmp/dglibc-rename-missing-parent";
        const char *missing_destination = "/tmp/dglibc-rename-missing-parent/file";
        char restored[4];
        int fd;
        int target_fd;
        int directory_fd;
        vfs_stat_t st;

        vfs_unlink(source);
        vfs_unlink(destination);
        vfs_unlink(replacement);
        vfs_unlink(cross_mount);
        vfs_unlink(missing_destination);
        vfs_unlink(missing_parent);
        fd = vfs_open(source, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0 || vfs_write(fd, "ram", 3u) != 3 || vfs_close(fd) != VFS_OK ||
            vfs_rename(source, destination) != VFS_OK || vfs_stat(source, &st) != VFS_ENOENT ||
            vfs_stat(destination, &st) != VFS_OK || st.size != 3u ||
            vfs_rename(destination, "/tmp//dglibc-rename-destination") != VFS_OK ||
            vfs_rename(destination, missing_destination) != VFS_ENOENT ||
            vfs_stat(destination, &st) != VFS_OK || vfs_stat(missing_parent, &st) != VFS_ENOENT ||
            vfs_rename(destination, cross_mount) != VFS_EXDEV ||
            vfs_stat(destination, &st) != VFS_OK || vfs_stat(cross_mount, &st) != VFS_ENOENT) {
            serial_write_string("[FAIL] dglibc crossed a VFS rename boundary\n");
            return 1;
        }
        fd = vfs_open(replacement, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0 || vfs_write(fd, "new", 3u) != 3 || vfs_close(fd) != VFS_OK) {
            serial_write_string("[FAIL] dglibc could not seed a replacement\n");
            return 1;
        }
        target_fd = vfs_open(destination, O_RDONLY);
        if (target_fd < 0 || vfs_rename(replacement, destination) != VFS_EBUSY ||
            vfs_stat(replacement, &st) != VFS_OK || vfs_stat(destination, &st) != VFS_OK ||
            vfs_close(target_fd) != VFS_OK) {
            serial_write_string("[FAIL] dglibc replaced an open VFS target\n");
            return 1;
        }
        directory_fd = vfs_open("/tmp", O_RDONLY);
        if (directory_fd < 0 || vfs_unlink(destination) != VFS_EBUSY ||
            vfs_stat(destination, &st) != VFS_OK || vfs_close(directory_fd) != VFS_OK ||
            vfs_rename(replacement, destination) != VFS_OK) {
            serial_write_string("[FAIL] dglibc invalidated a VFS iterator\n");
            return 1;
        }
        fd = vfs_open(destination, O_RDONLY);
        if (fd < 0 || vfs_read(fd, restored, 3u) != 3 || vfs_close(fd) != VFS_OK ||
            memcmp(restored, "new", 3u) != 0 || vfs_unlink(destination) != VFS_OK) {
            serial_write_string("[FAIL] dglibc lost a native VFS rename\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc VFS rename boundaries\n");
    }

    /* Copying or truncating a live path alias must preserve its bytes. */
    {
        const char *path = "/tmp/dglibc-copy-self";
        const char *directory = "/tmp/dglibc-copy-directory";
        char restored[5];
        int reader;
        int alias;

        vfs_unlink(path);
        vfs_unlink(directory);
        if (vfs_write_all(path, "copy", 4u) != 4 || vfs_mkdir(directory) != VFS_OK ||
            vfs_copy_file(path, path) != VFS_EINVAL ||
            vfs_copy_file("/tmp//dglibc-copy-self", "/tmp/dglibc-copy-self/") != VFS_EINVAL ||
            vfs_copy_file(directory, path) != VFS_EISDIR ||
            vfs_read_all(path, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "copy", 4u) != 0) {
            serial_write_string("[FAIL] dglibc accepted a VFS self-copy\n");
            return 1;
        }
        reader = vfs_open(path, O_RDONLY);
        alias = vfs_open("/tmp//dglibc-copy-self/", O_WRONLY | O_TRUNC);
        if (reader < 0 || alias != VFS_EBUSY || vfs_close(reader) != VFS_OK ||
            vfs_read_all(path, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "copy", 4u) != 0 || vfs_unlink(path) != VFS_OK ||
            vfs_unlink(directory) != VFS_OK) {
            serial_write_string("[FAIL] dglibc damaged a VFS path alias\n");
            if (alias >= 0)
                vfs_close(alias);
            return 1;
        }
        serial_write_string("[PASS] dglibc VFS copy boundaries\n");
    }

    /* A failed cache fill cannot relabel or re-dirty the evicted bytes. */
    if (blockcache_failure_selftest() != 0) {
        serial_write_string("[FAIL] dglibc block cache failure boundary\n");
        return 1;
    }
    serial_write_string("[PASS] dglibc block cache failure boundary\n");

    /* RamFS accepts its final byte and rejects writes or seeks past it. */
    {
        const int limit = (int)RAMFS_MAX_DATA;
        const char *path = "/tmp/dglibc-ramfs-limit";
        char last = 0;
        int fd;
        vfs_stat_t st;

        vfs_unlink(path);
        fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0 || vfs_seek(fd, limit - 1, SEEK_SET) != limit - 1 || vfs_write(fd, "Z", 1u) != 1 ||
            vfs_write(fd, "X", 1u) != VFS_ENOSPC || vfs_seek(fd, limit + 1, SEEK_SET) != VFS_ENOSPC ||
            vfs_close(fd) != VFS_OK || vfs_stat(path, &st) != VFS_OK || st.size != RAMFS_MAX_DATA) {
            serial_write_string("[FAIL] dglibc crossed the RamFS size boundary\n");
            return 1;
        }
        fd = vfs_open(path, O_RDONLY);
        if (fd < 0 || vfs_seek(fd, limit - 1, SEEK_SET) != limit - 1 || vfs_read(fd, &last, 1u) != 1 ||
            last != 'Z' || vfs_close(fd) != VFS_OK || vfs_unlink(path) != VFS_OK) {
            serial_write_string("[FAIL] dglibc changed RamFS after a rejected write\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc RamFS size boundary\n");
    }

    /* A FAT file write must not replace a directory with the same 8.3 name. */
    {
        const char *directory = "/disk/DGDIR";
        const char *child = "/disk/DGDIR/KEEP.TXT";
        char restored[5];
        int fd;
        vfs_stat_t st;

        vfs_unlink(child);
        vfs_unlink(directory);
        if (vfs_mkdir(directory) != VFS_OK) {
            serial_write_string("[FAIL] dglibc could not seed a FAT directory\n");
            return 1;
        }
        if (vfs_write_all(child, "keep", 4u) != 4 || fat16_write_file("DGDIR", "bad", 3u) >= 0 ||
            vfs_stat(directory, &st) != VFS_OK || st.type != VFS_TYPE_DIR) {
            serial_write_string("[FAIL] dglibc replaced a FAT directory\n");
            return 1;
        }
        fd = vfs_open(child, O_RDONLY);
        if (fd < 0 || vfs_read(fd, restored, 4u) != 4 || vfs_close(fd) != VFS_OK ||
            memcmp(restored, "keep", 4u) != 0 || vfs_unlink(child) != VFS_OK ||
            vfs_unlink(directory) != VFS_OK) {
            serial_write_string("[FAIL] dglibc damaged a FAT directory collision\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT directory collision\n");
    }

    /* FAT reads clamp by subtraction even for the largest request. */
    {
        const char *path = "/disk/RANGE.TXT";
        fat16_file_t *file;
        char restored[5];

        vfs_unlink(path);
        if (vfs_write_all(path, "range", 5u) != 5) {
            serial_write_string("[FAIL] dglibc could not seed a FAT range\n");
            return 1;
        }
        memset(restored, '?', sizeof(restored));
        file = fat16_open("RANGE.TXT");
        if (!file) {
            serial_write_string("[FAIL] dglibc could not open a FAT range\n");
            return 1;
        }
        file->position = 2u;
        if (fat16_read(file, restored, 0xffffffffu) != 3 || memcmp(restored, "nge", 3u) != 0 ||
            restored[3] != '?' || fat16_close(file) != 0 || vfs_unlink(path) != VFS_OK) {
            serial_write_string("[FAIL] dglibc crossed a FAT read boundary\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT read boundary\n");
    }

    /* Exhausting raw FAT handles must not turn O_CREAT into truncation. */
    {
        const char *path = "/disk/POOL.TXT";
        fat16_file_t *handles[16];
        char restored[5];
        int acquired = 0;
        int denied;
        int okay = 1;

        vfs_unlink(path);
        if (vfs_write_all(path, "alive", 5u) != 5) {
            serial_write_string("[FAIL] dglibc could not seed a FAT pool target\n");
            return 1;
        }
        while (acquired < 16) {
            fat16_file_t *handle = fat16_open("POOL.TXT");
            if (!handle)
                break;
            handles[acquired++] = handle;
        }
        denied = vfs_open(path, O_WRONLY | O_CREAT);
        if (acquired == 0 || denied != VFS_EMFILE) {
            if (denied >= 0)
                vfs_close(denied);
            okay = 0;
        }
        for (int i = 0; i < acquired; i++) {
            if (fat16_close(handles[i]) != 0)
                okay = 0;
        }
        if (!okay || vfs_read_all(path, restored, sizeof(restored)) != 5 ||
            memcmp(restored, "alive", 5u) != 0 || vfs_unlink(path) != VFS_OK) {
            serial_write_string("[FAIL] dglibc truncated through FAT handle exhaustion\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT handle exhaustion\n");
    }

    /* Replacement and deletion wait until the old FAT entry is unused. */
    {
        const char *path = "/disk/BUSY.TXT";
        char restored[4];
        int reader;
        int writer;

        vfs_unlink(path);
        if (vfs_write_all(path, "old", 3u) != 3) {
            serial_write_string("[FAIL] dglibc could not seed a busy FAT file\n");
            return 1;
        }
        reader = vfs_open(path, O_RDONLY);
        writer = vfs_open(path, O_WRONLY | O_TRUNC);
        if (reader < 0 || writer < 0 || vfs_write(writer, "new", 3u) != 3 ||
            vfs_close(writer) != VFS_EBUSY || vfs_unlink(path) != VFS_EBUSY ||
            vfs_read(reader, restored, 3u) != 3 || memcmp(restored, "old", 3u) != 0 ||
            vfs_close(reader) != VFS_OK || vfs_read_all(path, restored, sizeof(restored)) != 3 ||
            memcmp(restored, "old", 3u) != 0 || vfs_write_all(path, "new", 3u) != 3 ||
            vfs_read_all(path, restored, sizeof(restored)) != 3 ||
            memcmp(restored, "new", 3u) != 0 || vfs_unlink(path) != VFS_OK) {
            serial_write_string("[FAIL] dglibc replaced a live FAT entry\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT busy replacement\n");
    }

    /* FAT VFS paths must fit exactly into one-level 8.3 components. */
    {
        const char *base = "/disk/ABCDEFGH";
        const char *extended = "/disk/ABCDEFGH.TXT";
        char restored[5];
        int fd;

        vfs_unlink(base);
        vfs_unlink(extended);
        if (vfs_write_all(base, "base", 4u) != 4 || vfs_write_all(extended, "text", 4u) != 4) {
            serial_write_string("[FAIL] dglibc could not seed FAT path boundaries\n");
            return 1;
        }
        fd = vfs_open("/disk/ABCDEFGHZZZZZZZZ.TXT", O_WRONLY | O_CREAT | O_TRUNC);
        if (fd != VFS_EINVAL) {
            if (fd >= 0)
                vfs_close(fd);
            serial_write_string("[FAIL] dglibc accepted a long FAT component\n");
            return 1;
        }
        fd = vfs_open("/disk/ABCDEFGHZZZZ/TAIL.TXT", O_WRONLY | O_CREAT | O_TRUNC);
        if (fd != VFS_EINVAL) {
            if (fd >= 0)
                vfs_close(fd);
            serial_write_string("[FAIL] dglibc accepted a deep FAT alias\n");
            return 1;
        }
        if (vfs_read_all(base, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "base", 4u) != 0 ||
            vfs_read_all(extended, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "text", 4u) != 0 || vfs_unlink(base) != VFS_OK ||
            vfs_unlink(extended) != VFS_OK) {
            serial_write_string("[FAIL] dglibc aliased a rejected FAT path\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT 8.3 path boundary\n");
    }

    /* The live HomeFS container has one owner and no writable FAT alias. */
    {
        const char *probe = "/home/dglibc-container-owner";
        char restored[5];
        int alias = vfs_open("/disk/HOMEFS.SYS", O_WRONLY | O_TRUNC);

        vfs_unlink(probe);
        if (alias != VFS_EBUSY || vfs_unlink("/disk/HOMEFS.SYS") != VFS_EBUSY ||
            fat16_write_file("HOMEFS.SYS", "bad", 3u) != FAT16_BUSY ||
            vfs_write_all(probe, "safe", 4u) != 4 ||
            vfs_read_all(probe, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "safe", 4u) != 0 || vfs_unlink(probe) != VFS_OK ||
            vfs_mount(NULL, "/home-second", "homefs") != VFS_EBUSY) {
            if (alias >= 0)
                vfs_close(alias);
            serial_write_string("[FAIL] dglibc crossed the HomeFS container boundary\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc HomeFS mount boundary\n");
    }

    /* HomeFS recursive walks stay within their documented depth. */
    {
        char path[128];
        uint32_t length = 5u;
        int created = 0;
        int okay = 1;
        int status;

        strcpy(path, "/home");
        homefs_seed_begin();
        while (created < 32) {
            path[length++] = '/';
            path[length++] = 'z';
            path[length++] = 'z';
            path[length] = '\0';
            if (vfs_mkdir(path) != VFS_OK) {
                length -= 3u;
                path[length] = '\0';
                okay = 0;
                break;
            }
            created++;
        }
        if (okay) {
            path[length++] = '/';
            path[length++] = 'z';
            path[length++] = 'z';
            path[length] = '\0';
            status = vfs_mkdir(path);
            if (status == VFS_OK) {
                created++;
                okay = 0;
            } else {
                length -= 3u;
                path[length] = '\0';
                if (status != VFS_ENOENT)
                    okay = 0;
            }
        }
        while (created > 0) {
            if (vfs_unlink(path) != VFS_OK)
                okay = 0;
            length -= 3u;
            path[length] = '\0';
            created--;
        }
        homefs_seed_end();
        if (!okay) {
            serial_write_string("[FAIL] dglibc HomeFS depth boundary\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc HomeFS depth boundary\n");
    }

    return 0;
}

int dglibc_test_main(void) {
    int result;
    int publish_status;

    if (homefs_batch_end() != VFS_EINVAL) {
        serial_write_string("[FAIL] dglibc accepted an unmatched HomeFS batch\n");
        return 1;
    }
    if (homefs_batch_begin() != VFS_OK) {
        serial_write_string("[FAIL] dglibc could not begin a HomeFS batch\n");
        return 1;
    }

    result = dglibc_test_body();
    publish_status = homefs_batch_end();
    if (result != 0)
        return result;
    if (publish_status != VFS_OK) {
        serial_write_string("[FAIL] dglibc could not publish its HomeFS batch\n");
        return 1;
    }
    serial_write_string("[PASS] dglibc HomeFS batch boundary\n");
    return 0;
}
