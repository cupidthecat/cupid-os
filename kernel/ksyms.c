/*
 * ksyms - kernel function symbol table for backtrace decoding.
 *
 * The blob lives in section .ksyms, contributed by ksyms_data.o (built
 * from a generated C file in two passes - first link extracts symbols,
 * second link embeds them).  See tools/mksyms.sh.
 *
 * If the blob is absent or has bad magic, all lookups return NULL and
 * callers fall back to printing raw addresses.  This keeps panics
 * functional even before the symbol pipeline has run.
 */

#include "ksyms.h"

#define KSYM_MAGIC 0x4D59534BU /* 'KSYM' little-endian */

/* Defined either by the generated ksyms_data.c (real blob) or below as
 * a weak fallback so an un-extracted build still links. */
extern const unsigned char ksym_blob[];
extern const unsigned int  ksym_blob_size;

/* Weak fallbacks: a 16-byte zero header so the blob is "present but
 * empty".  ksym_lookup() detects bad magic and returns NULL.  These
 * symbols are overridden by ksyms_data.o once the second-pass link
 * runs. */
const unsigned char ksym_blob[16] __attribute__((weak,
    section(".ksyms"), aligned(4))) = {0};
const unsigned int ksym_blob_size __attribute__((weak)) = 0;

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t string_off;
    uint32_t total_size;
} ksym_header_t;

typedef struct {
    uint32_t addr;
    uint32_t name_off;
} ksym_entry_t;

static const ksym_header_t *ksym_header(void) {
    if (ksym_blob_size < sizeof(ksym_header_t)) return 0;
    const ksym_header_t *h = (const ksym_header_t *)ksym_blob;
    if (h->magic != KSYM_MAGIC) return 0;
    if (h->total_size > ksym_blob_size) return 0;
    if (h->string_off < sizeof(*h)) return 0;
    if (h->string_off > h->total_size) return 0;
    return h;
}

const char *ksym_lookup(uint32_t addr, uint32_t *off_out) {
    const ksym_header_t *h = ksym_header();
    if (!h || h->count == 0) return 0;

    const ksym_entry_t *ents =
        (const ksym_entry_t *)((const unsigned char *)h + sizeof(*h));
    const char *strtab =
        (const char *)((const unsigned char *)h + h->string_off);

    /* Binary search for the largest entry with addr <= target. */
    uint32_t lo = 0, hi = h->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (ents[mid].addr <= addr) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return 0;
    const ksym_entry_t *e = &ents[lo - 1];

    /* Sanity-bound the offset against the next entry (or 0x10000 cap if
     * this is the last symbol - function bodies > 64K are rejected so a
     * stray PC doesn't get mis-attributed to the final symbol). */
    uint32_t func_end;
    if (lo < h->count) func_end = ents[lo].addr;
    else               func_end = e->addr + 0x10000U;
    if (addr >= func_end) return 0;

    if (off_out) *off_out = addr - e->addr;
    /* name_off is bounded by string_off..total_size; trust the encoder. */
    return strtab + e->name_off;
}

void ksym_backtrace(uint32_t start_ebp, uint32_t start_eip, int max_frames,
                    void (*print_line)(int frame, uint32_t addr,
                                       const char *name, uint32_t off)) {
    if (!print_line || max_frames <= 0) return;

    /* Frame 0: the faulting instruction itself. */
    uint32_t off = 0;
    const char *name = ksym_lookup(start_eip, &off);
    print_line(0, start_eip, name, off);

    uint32_t ebp = start_ebp;
    for (int i = 1; i < max_frames; i++) {
        /* Stop if EBP looks bogus.  The kernel stack window per
         * docs/code is roughly 0x1000..0x900000; outside that, refuse
         * to deref to avoid faulting inside the panic path. */
        if (ebp < 0x1000U || ebp > 0x900000U) break;

        uint32_t ret_addr = *(const uint32_t *)(ebp + 4);
        uint32_t prev_ebp = *(const uint32_t *)ebp;

        /* Return addresses point to the byte AFTER the CALL.  If the
         * caller's last instruction is a tail call to a function that
         * happens to end the parent's body, the literal ret_addr can
         * land at the start of the next function's symbol.  Look up
         * (ret_addr - 1) so we resolve to the function the CALL was
         * actually inside. */
        uint32_t lookup_addr = (ret_addr > 0) ? (ret_addr - 1) : 0;
        off = 0;
        name = ksym_lookup(lookup_addr, &off);
        print_line(i, ret_addr, name, off);

        if (prev_ebp <= ebp) break;
        ebp = prev_ebp;
    }
}
