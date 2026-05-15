//help: P3 opt-in swap smoke: per-class round-trip, disk cap, nested pin, bad-handle
//help: Usage: swapinit && feature18_swap

/* CupidC doesn't have memset/memcmp; inline trivial helpers.
 * CupidC also doesn't natively recognize uint32_t - use int (32-bit) for
 * swap handles. The BIND entries in kernel/cupidc.c bind swap_kmalloc to
 * return a 32-bit handle in EAX, which lands in an int just fine.*/

void fill_byte(char *p, int n, char v) {
    int i = 0;
    while (i < n) { p[i] = v; i = i + 1; }
}

int verify_byte(char *p, int n, char v) {
    int i = 0;
    while (i < n) {
        if (p[i] != v) return 0;
        i = i + 1;
    }
    return 1;
}

void main() {
    int ok = 1;

    /* Keep the smoke test self-contained. If swap is already initialized,
     * swap_init rejects the duplicate setup and the existing pool remains
     * usable; if not, this creates the default 4 MB resident pool.*/
    swap_init("/disk/swap.bin", 4194304);

    /* 1. Per-class round trip: one handle per class, unique byte pattern.
     *    Classes are 1K / 4K / 16K / 64K; pick a size that lands in each.*/
    int h1 = swap_kmalloc(500);    /* class 0: 1K */
    int h2 = swap_kmalloc(3000);   /* class 1: 4K */
    int h3 = swap_kmalloc(9000);   /* class 2: 16K */
    int h4 = swap_kmalloc(40000);  /* class 3: 64K */
    if (h1 == 0 || h2 == 0 || h3 == 0 || h4 == 0) {
        serial_printf("[feature18] FAIL kmalloc per-class: %d %d %d %d\n",
                      h1, h2, h3, h4);
        ok = 0;
    } else {
        char *p1 = (char *)swap_pin(h1);
        char *p2 = (char *)swap_pin(h2);
        char *p3 = (char *)swap_pin(h3);
        char *p4 = (char *)swap_pin(h4);

        if (p1 == 0 || p2 == 0 || p3 == 0 || p4 == 0) {
            serial_printf("[feature18] FAIL pin per-class\n");
            ok = 0;
        } else {
            fill_byte(p1, 1024, 0x11);
            fill_byte(p2, 4096, 0x22);
            fill_byte(p3, 16384, 0x33);
            fill_byte(p4, 65536, 0x44);
        }
        if (p1 != 0) swap_unpin(h1);
        if (p2 != 0) swap_unpin(h2);
        if (p3 != 0) swap_unpin(h3);
        if (p4 != 0) swap_unpin(h4);

        if (p1 != 0 && p4 != 0) {
            /* Re-pin and verify patterns survived the unpin (disk or RAM doesn't
             * matter - the slot-backed invariant is address-independent of where
             * the bytes live).*/
            p1 = (char *)swap_pin(h1);
            if (p1 == 0 || !verify_byte(p1, 1024, 0x11)) {
                serial_printf("[feature18] FAIL h1 pattern after repin\n");
                ok = 0;
            }
            if (p1 != 0) swap_unpin(h1);

            p4 = (char *)swap_pin(h4);
            if (p4 == 0 || !verify_byte(p4, 65536, 0x44)) {
                serial_printf("[feature18] FAIL h4 pattern after repin\n");
                ok = 0;
            }
            if (p4 != 0) swap_unpin(h4);
        }

        swap_free(h1); swap_free(h2); swap_free(h3); swap_free(h4);
    }

    /* 2. Class 3 (64K) disk cap - allocate until full.
     *    Default disk cap for class 3 is 64 slots; 65th should fail.*/
    int hs3[65];
    int i = 0;
    while (i < 65) {
        hs3[i] = swap_kmalloc(65536);
        i = i + 1;
    }

    /* Of the 65 attempts, the first 64 should succeed and the 65th fail. */
    int good = 0;
    int j = 0;
    while (j < 65) {
        if (hs3[j] != 0) good = good + 1;
        j = j + 1;
    }
    if (good < 64) {
        serial_printf("[feature18] FAIL class-3 disk cap: got %d/65 (want 64)\n",
                      good);
        ok = 0;
    }
    if (hs3[64] != 0) {
        serial_printf("[feature18] FAIL 65th class-3 alloc should be SWAP_INVALID\n");
        ok = 0;
    }
    /* Clean up. */
    j = 0;
    while (j < 64) {
        if (hs3[j] != 0) swap_free(hs3[j]);
        j = j + 1;
    }

    /* 3. Nested pin: pin(h) + pin(h) should yield the same pointer;
     *    unpin once should leave it pinned (refcount semantics).*/
    int hn = swap_kmalloc(100);
    char *pn1 = (char *)swap_pin(hn);
    char *pn2 = (char *)swap_pin(hn);
    if (hn == 0 || pn1 == 0 || pn2 == 0) {
        serial_printf("[feature18] FAIL nested pin setup\n");
        ok = 0;
    } else if (pn1 != pn2) {
        serial_printf("[feature18] FAIL nested pin ptr mismatch\n");
        ok = 0;
    }
    if (hn != 0) {
        swap_unpin(hn);  /* refcount -> 1, still pinned */
        swap_unpin(hn);  /* refcount -> 0, now evictable */
        swap_free(hn);
    }

    /* 4. Invalid-handle ops must not panic or crash. */
    if (swap_pin(0) != 0) {
        serial_printf("[feature18] FAIL pin(0) returned non-NULL\n");
        ok = 0;
    }
    if (swap_pin(99999) != 0) {
        serial_printf("[feature18] FAIL pin(99999) returned non-NULL\n");
        ok = 0;
    }
    swap_unpin(0);       /* must not panic */
    swap_free(0);        /* must not panic */
    swap_free(99999);    /* must not panic */

    if (ok) {
        serial_printf("PASS feature18_swap\n");
        println("PASS feature18_swap");
    } else {
        serial_printf("FAIL feature18_swap\n");
        println("FAIL feature18_swap");
    }
}
