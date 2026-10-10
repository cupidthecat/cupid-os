//help: P2 ISO9660 smoke: mount hello.iso, verify RR + case-fold + multi-mount
//help: Usage: feature17_iso (run after `make sync-iso` on host)
//
// This test exercises the end-to-end ISO9660 stack through the VFS layer.
// `vfs_mount` / `vfs_umount` are not bound as CupidC builtins, so we drive
// the shell via `shell_execute_line("mount ... iso9660")`.  `mount_count`
// reports a high-water-mark that never decrements on umount, so we verify
// mount state via `mount_path(i)` iteration instead (NULL for free slots).
// File I/O goes through the bound `vfs_open` / `vfs_read` / `vfs_close`
// builtins directly.

int str_eq_n(const char *a, const char *b, int n) {
    /* Byte-compare the first n bytes; stops at n, never reads past. */
    int i = 0;
    while (i < n) {
        if (a[i] != b[i]) return 0;
        i = i + 1;
    }
    return 1;
}

int is_mounted(const char *target) {
    /* mount_count is a high-water-mark; mount_path(i) returns NULL for
     * unmounted slots.  Scan all slots and look for an exact path match.*/
    int c = mount_count();
    int i = 0;
    while (i < c) {
        const char *p = mount_path(i);
        if (p != 0) {
            if (strcmp(p, target) == 0) return 1;
        }
        i = i + 1;
    }
    return 0;
}

int count_mounted_iso(const char *prefix, int prefix_len) {
    /* Count active mounts whose path starts with prefix.  Used to verify
     * multi-mount and pool-exhaustion scenarios.*/
    int c = mount_count();
    int n = 0;
    int i = 0;
    while (i < c) {
        const char *p = mount_path(i);
        if (p != 0) {
            if (str_eq_n(p, prefix, prefix_len)) n = n + 1;
        }
        i = i + 1;
    }
    return n;
}

int read_and_check(const char *path, const char *expected, int expected_len) {
    int fd = vfs_open(path, 0);
    if (fd < 0) {
        serial_printf("[feature17] FAIL open %s rc=%d\n", path, fd);
        return 0;
    }
    char buf[256];
    int i = 0;
    while (i < 256) { buf[i] = 0; i = i + 1; }
    int n = vfs_read(fd, buf, 255);
    vfs_close(fd);
    if (n != expected_len) {
        serial_printf("[feature17] FAIL %s length got=%d want=%d\n",
                      path, n, expected_len);
        return 0;
    }
    if (!str_eq_n(buf, expected, expected_len)) {
        serial_printf("[feature17] FAIL %s content mismatch\n", path);
        return 0;
    }
    return 1;
}

int check_directory_names() {
    int fd = vfs_open("/iso", 0);
    if (fd < 0) {
        serial_printf("[feature17] FAIL readdir open fd=%d\n", fd);
        return 0;
    }

    int seen_big = 0;
    int seen_script = 0;
    int seen_jpeg = 0;
    int seen_long = 0;
    int seen_readme = 0;
    int seen_sub = 0;
    int count = 0;
    int bad = 0;
    char ent[136];
    int rc = vfs_readdir(fd, ent);
    while (rc > 0) {
        int type = ent[132];
        if (strcmp(ent, "big.bin") == 0) {
            seen_big = seen_big + 1;
            if (type != 0) bad = 1;
        } else if (strcmp(ent, "gen_big.sh") == 0) {
            seen_script = seen_script + 1;
            if (type != 0) bad = 1;
        } else if (strcmp(ent, "jpeg_baseline_8x8.jpg") == 0) {
            seen_jpeg = seen_jpeg + 1;
            if (type != 0) bad = 1;
        } else if (strcmp(ent, "long_named_file.txt") == 0) {
            seen_long = seen_long + 1;
            if (type != 0) bad = 1;
        } else if (strcmp(ent, "readme.txt") == 0) {
            seen_readme = seen_readme + 1;
            if (type != 0) bad = 1;
        } else if (strcmp(ent, "sub") == 0) {
            seen_sub = seen_sub + 1;
            if (type != 1) bad = 1;
        } else {
            serial_printf("[feature17] FAIL readdir unexpected=%s\n", ent);
            bad = 1;
        }
        count = count + 1;
        rc = vfs_readdir(fd, ent);
    }
    vfs_close(fd);

    if (rc < 0) {
        serial_printf("[feature17] FAIL readdir rc=%d\n", rc);
        return 0;
    }
    if (count != 6 || seen_big != 1 || seen_script != 1 ||
        seen_jpeg != 1 || seen_long != 1 || seen_readme != 1 ||
        seen_sub != 1 || bad) {
        serial_printf(
            "[feature17] FAIL readdir count=%d seen=%d%d%d%d%d%d bad=%d\n",
            count, seen_big, seen_script, seen_jpeg, seen_long,
            seen_readme, seen_sub, bad);
        return 0;
    }

    serial_printf(
        "PASS feature17_readdir names=6 long=long_named_file.txt\n");
    return 1;
}

int check_baseline_jpeg() {
    char encoded[512];
    int fd = vfs_open("/iso/jpeg_baseline_8x8.jpg", 0);
    if (fd < 0) {
        serial_printf("[feature17] FAIL jpeg fixture open fd=%d\n", fd);
        return 0;
    }

    int n = vfs_read(fd, encoded, 512);
    vfs_close(fd);
    if (n != 331) {
        serial_printf("[feature17] FAIL jpeg fixture length=%d\n", n);
        return 0;
    }

    int *pixels = 0;
    int width = 0;
    int height = 0;
    int rc = jpeg_decode_mem(encoded, n, &pixels, &width, &height);
    if (rc != 0 || pixels == 0) {
        serial_printf("[feature17] FAIL jpeg_decode_mem rc=%d\n", rc);
        return 0;
    }

    if (width != 8 || height != 8) {
        serial_printf("[feature17] FAIL jpeg size=%dx%d\n", width, height);
        kfree(pixels);
        return 0;
    }

    int bad = -1;
    int i = 0;
    while (i < 64) {
        if ((pixels[i] & 0x00FFFFFF) != 0x00808080) {
            bad = i;
            i = 64;
        } else {
            i = i + 1;
        }
    }

    if (bad >= 0) {
        serial_printf("[feature17] FAIL jpeg pixel=%d rgb=%x\n",
                      bad, pixels[bad] & 0x00FFFFFF);
        kfree(pixels);
        return 0;
    }

    kfree(pixels);
    serial_printf("PASS jpeg_decode_mem baseline 8x8 gray128\n");
    return 1;
}

int check_glyph_raster() {
    int faces = fontsys_face_count();
    if (faces < 4) {
        serial_printf("[feature17] FAIL glyph_rasterize faces=%d\n", faces);
        return 0;
    }

    int face = fontsys_match("Liberation Mono", 3, 400, 0);
    if (face < 0 || face >= faces) {
        serial_printf("[feature17] FAIL glyph_rasterize face=%d\n", face);
        return 0;
    }

    /* Size 37 and Q avoid every normal UI cache entry. The first width
     * request must decode and rasterize the outline; the second checks the
     * cache entry produced by that path. */
    int width = fontsys_run_width(face, 37, "Q", 1);
    if (width <= 0) {
        serial_printf("[feature17] FAIL glyph_rasterize width=%d\n", width);
        return 0;
    }
    int cached = fontsys_run_width(face, 37, "Q", 1);
    if (cached != width) {
        serial_printf("[feature17] FAIL glyph_rasterize cache=%d width=%d\n",
                      cached, width);
        return 0;
    }

    serial_printf(
        "PASS glyph_rasterize Liberation Mono Q size37 width=%d cache=%d\n",
        width, cached);
    return 1;
}

void main() {
    int ok = 1;

    /* 1. Mount via the shell command; verify the path is now active. */
    shell_execute_line("mount /disk/hello.iso /iso iso9660");
    if (!is_mounted("/iso")) {
        serial_printf("[feature17] FAIL mount /iso not active\n");
        ok = 0;
    }

    /* 2. Read readme.txt (base 8.3 + lowercase via RR NM) */
    if (!read_and_check("/iso/readme.txt", "P2 ISO9660 smoke test\n", 22)) ok = 0;

    /* 3. Case-fold: uppercase also opens */
    int fd = vfs_open("/iso/README.TXT", 0);
    if (fd < 0) { serial_printf("[feature17] FAIL case-fold fd=%d\n", fd); ok = 0; }
    else        vfs_close(fd);

    /* 4. Long filename via Rock Ridge NM */
    fd = vfs_open("/iso/long_named_file.txt", 0);
    if (fd < 0) { serial_printf("[feature17] FAIL RR long name fd=%d\n", fd); ok = 0; }
    else        vfs_close(fd);

    /* 5. Directory iteration retains all six Rock Ridge display names. */
    if (!check_directory_names()) ok = 0;

    /* 6. Subdir traversal */
    if (!read_and_check("/iso/sub/nested.txt", "Nested file under sub/\n", 23)) ok = 0;

    /* 7. 4K file spanning 2 sectors (0x00..0xFF pattern repeating) */
    fd = vfs_open("/iso/big.bin", 0);
    if (fd < 0) { serial_printf("[feature17] FAIL big.bin open fd=%d\n", fd); ok = 0; }
    else {
        char bigbuf[4096];
        int j = 0;
        while (j < 4096) { bigbuf[j] = 0; j = j + 1; }
        int n = vfs_read(fd, bigbuf, 4096);
        if (n != 4096) {
            serial_printf("[feature17] FAIL big read got=%d want=4096\n", n);
            ok = 0;
        } else {
            int bad = -1;
            int k = 0;
            while (k < 4096) {
                if ((bigbuf[k] & 0xFF) != (k & 0xFF)) { bad = k; k = 4096; }
                else k = k + 1;
            }
            if (bad >= 0) {
                serial_printf("[feature17] FAIL big pattern at byte %d\n", bad);
                ok = 0;
            }
        }
        vfs_close(fd);
    }

    /* 8. Decode a byte-fixed baseline JPEG through the production path. */
    if (!check_baseline_jpeg()) ok = 0;

    /* 9. Rasterize an uncached bundled TrueType glyph and reuse its cache. */
    if (!check_glyph_raster()) ok = 0;

    /* 10. Unmount */
    shell_execute_line("umount /iso");
    if (is_mounted("/iso")) {
        serial_printf("[feature17] FAIL umount /iso still active\n");
        ok = 0;
    }

    /* 11. Multi-mount: 2 concurrent */
    shell_execute_line("mount /disk/hello.iso /iso_a iso9660");
    shell_execute_line("mount /disk/hello.iso /iso_b iso9660");
    if (!is_mounted("/iso_a") || !is_mounted("/iso_b")) {
        serial_printf("[feature17] FAIL multi mount: a=%d b=%d\n",
                      is_mounted("/iso_a"), is_mounted("/iso_b"));
        ok = 0;
    }
    int fa = vfs_open("/iso_a/readme.txt", 0);
    int fb = vfs_open("/iso_b/readme.txt", 0);
    if (fa < 0 || fb < 0) {
        serial_printf("[feature17] FAIL multi-mount read fa=%d fb=%d\n", fa, fb);
        ok = 0;
    }
    if (fa >= 0) vfs_close(fa);
    if (fb >= 0) vfs_close(fb);
    shell_execute_line("umount /iso_a");
    shell_execute_line("umount /iso_b");

    /* 12. Pool exhaustion: mount 4 then try a 5th.  iso9660 loopdev pool
     * is size 4; 5th should be rejected.  We verify by counting /iso_N
     * slots before and after the 5th mount attempt.*/
    shell_execute_line("mount /disk/hello.iso /iso_1 iso9660");
    shell_execute_line("mount /disk/hello.iso /iso_2 iso9660");
    shell_execute_line("mount /disk/hello.iso /iso_3 iso9660");
    shell_execute_line("mount /disk/hello.iso /iso_4 iso9660");
    int before5 = count_mounted_iso("/iso_", 5);
    if (before5 != 4) {
        serial_printf("[feature17] FAIL pool fill: got %d /iso_ mounts\n", before5);
        ok = 0;
    }
    shell_execute_line("mount /disk/hello.iso /iso_5 iso9660");
    int after5 = count_mounted_iso("/iso_", 5);
    if (after5 != before5) {
        serial_printf("[feature17] FAIL 5th mount not rejected (%d->%d)\n",
                      before5, after5);
        ok = 0;
    }
    shell_execute_line("umount /iso_1");
    shell_execute_line("umount /iso_2");
    shell_execute_line("umount /iso_3");
    shell_execute_line("umount /iso_4");

    if (ok) {
        serial_printf("PASS feature17_iso\n");
        println("PASS feature17_iso");
    } else {
        serial_printf("FAIL feature17_iso\n");
        println("FAIL feature17_iso");
    }
}
