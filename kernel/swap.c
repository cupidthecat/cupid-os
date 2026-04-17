/* kernel/swap.c - Opt-in swap manager (Phase B core scaffolding). */

#include "swap.h"
#include "swap_disk.h"
#include "vfs.h"
#include "memory.h"
#include "panic.h"
#include "../drivers/serial.h"

typedef struct {
    uint8_t        in_use;
    uint8_t        size_class;
    uint16_t       pin_count;
    void          *resident_ptr;
    uint32_t       disk_slot;
    uint32_t       size_bytes;
    swap_handle_t  lru_prev;
    swap_handle_t  lru_next;
    swap_handle_t  free_next;
} swap_slot_t;

typedef struct {
    uint32_t  slot_bytes;
    uint32_t  disk_slot_count;
    uint8_t  *disk_bitmap;
    uint32_t  disk_alloc;
    uint8_t  *ram_base;
    uint32_t  ram_cap;
    uint32_t  ram_alloc;
    uint8_t  *ram_free_bitmap;
} swap_class_t;

static swap_slot_t    g_slots[SWAP_MAX_HANDLES + 1];   /* index 0 reserved */
static swap_class_t   g_classes[SWAP_DISK_NUM_CLASSES];
static swap_handle_t  g_slot_free_head = SWAP_INVALID;
static swap_handle_t  g_lru_head = SWAP_INVALID;
static swap_handle_t  g_lru_tail = SWAP_INVALID;
static uint32_t       g_handles_in_use = 0;
static uint32_t       g_pinned_count = 0;
static uint32_t       g_evictions = 0;
static uint8_t        g_initialized = 0;

/* --- bitmap helpers (1 bit per slot; 1 = allocated) ---------------- */
static int32_t bitmap_alloc(uint8_t *bm, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!(bm[i >> 3] & (uint8_t)(1u << (i & 7u)))) {
            bm[i >> 3] |= (uint8_t)(1u << (i & 7u));
            return (int32_t)i;
        }
    }
    return -1;
}

static void bitmap_free(uint8_t *bm, uint32_t slot) {
    bm[slot >> 3] &= (uint8_t)~(1u << (slot & 7u));
}

/* Smallest class index that fits `size`, or -1 if too big. */
static int pick_class(uint32_t size) {
    for (int c = 0; c < SWAP_DISK_NUM_CLASSES; c++) {
        if (size <= swap_disk_slot_bytes[c]) return c;
    }
    return -1;
}

static swap_slot_t *validate_handle(swap_handle_t h) {
    if (h == SWAP_INVALID || h > SWAP_MAX_HANDLES) return NULL;
    swap_slot_t *s = &g_slots[h - 1];
    if (!s->in_use) return NULL;
    return s;
}

/* --- RAM slot bitmap + address math per class --- */
static int32_t ram_slot_alloc(swap_class_t *cls) {
    if (!cls->ram_free_bitmap || cls->ram_alloc >= cls->ram_cap) return -1;
    int32_t slot = bitmap_alloc(cls->ram_free_bitmap, cls->ram_cap);
    if (slot >= 0) cls->ram_alloc++;
    return slot;
}

static void ram_slot_free(swap_class_t *cls, uint32_t slot) {
    bitmap_free(cls->ram_free_bitmap, slot);
    cls->ram_alloc--;
}

static void *ram_slot_ptr(swap_class_t *cls, uint32_t slot) {
    return cls->ram_base + (size_t)slot * cls->slot_bytes;
}

static uint32_t ram_slot_from_ptr(const swap_class_t *cls, const void *ptr) {
    uint32_t diff = (uint32_t)(const uint8_t *)ptr - (uint32_t)cls->ram_base;
    return diff / cls->slot_bytes;
}

/* --- LRU doubly-linked list of unpinned resident handles ---------- */
static void lru_insert_head(swap_handle_t h) {
    swap_slot_t *s = &g_slots[h - 1];
    s->lru_prev = SWAP_INVALID;
    s->lru_next = g_lru_head;
    if (g_lru_head != SWAP_INVALID) g_slots[g_lru_head - 1].lru_prev = h;
    g_lru_head = h;
    if (g_lru_tail == SWAP_INVALID) g_lru_tail = h;
}

static void lru_remove(swap_handle_t h) {
    swap_slot_t *s = &g_slots[h - 1];
    if (s->lru_prev != SWAP_INVALID) g_slots[s->lru_prev - 1].lru_next = s->lru_next;
    else                              g_lru_head = s->lru_next;
    if (s->lru_next != SWAP_INVALID) g_slots[s->lru_next - 1].lru_prev = s->lru_prev;
    else                              g_lru_tail = s->lru_prev;
    s->lru_prev = SWAP_INVALID;
    s->lru_next = SWAP_INVALID;
}

/* Evict one unpinned handle in class `target` by writing its resident
 * bytes back to disk and releasing its RAM slot. Returns 0 on success,
 * -1 if no unpinned handle in this class exists in the LRU. */
static int class_evict_one(uint8_t target) {
    for (swap_handle_t h = g_lru_tail; h != SWAP_INVALID;
         h = g_slots[h - 1].lru_prev) {
        swap_slot_t *s = &g_slots[h - 1];
        if (s->size_class != target) continue;
        if (s->pin_count != 0) continue;   /* shouldn't be in LRU, defensive */

        swap_class_t *cls = &g_classes[target];
        int rc = swap_disk_write(target, s->disk_slot, s->resident_ptr);
        if (rc < 0) {
            kernel_panic("swap: disk_write failed during eviction h=%u rc=%d",
                         h, rc);
        }
        uint32_t ram_slot = ram_slot_from_ptr(cls, s->resident_ptr);
        ram_slot_free(cls, ram_slot);
        s->resident_ptr = NULL;
        lru_remove(h);
        g_evictions++;
        return 0;
    }
    return -1;
}

/* Tasks 4-9 fill these stubs. */
int swap_init(const char *backing_path, uint32_t pool_bytes) {
    if (g_initialized) return VFS_EACCES;  /* already initialized */
    if (!backing_path || pool_bytes == 0) return VFS_EINVAL;

    int rc = swap_disk_open(backing_path);
    if (rc < 0) return rc;

    /* Initialize handle table free list. Handle value = slot index + 1. */
    for (uint32_t i = 0; i < SWAP_MAX_HANDLES; i++) {
        g_slots[i].in_use = 0;
        g_slots[i].free_next = (swap_handle_t)(i + 2);  /* next handle index */
    }
    g_slots[SWAP_MAX_HANDLES - 1].free_next = SWAP_INVALID;  /* tail */
    g_slot_free_head = (swap_handle_t)1;  /* first handle */
    g_handles_in_use = 0;

    /* Initialize each size class:
     * - disk_bitmap: kmalloc (slot_count + 7)/8 bytes, zeroed.
     * - ram_base: kmalloc (pool_bytes / 4) bytes.
     * - ram_cap: (pool_bytes / 4) / slot_bytes.
     * - ram_free_bitmap: kmalloc (ram_cap + 7)/8 bytes. */
    uint32_t per_class_bytes = pool_bytes / SWAP_DISK_NUM_CLASSES;
    for (uint8_t c = 0; c < SWAP_DISK_NUM_CLASSES; c++) {
        swap_class_t *cls = &g_classes[c];
        cls->slot_bytes      = swap_disk_slot_bytes[c];
        cls->disk_slot_count = swap_disk_slot_count[c];
        cls->disk_alloc      = 0;

        uint32_t bitmap_bytes = (cls->disk_slot_count + 7u) / 8u;
        cls->disk_bitmap = (uint8_t *)kmalloc(bitmap_bytes);
        if (!cls->disk_bitmap) {
            KERROR("swap_init: kmalloc disk_bitmap class %u failed", c);
            swap_disk_close();
            return VFS_EIO;
        }
        for (uint32_t i = 0; i < bitmap_bytes; i++) cls->disk_bitmap[i] = 0;

        cls->ram_cap = per_class_bytes / cls->slot_bytes;
        if (cls->ram_cap == 0) {
            /* pool too small for this class - fine, we just can't load it. */
            cls->ram_base        = NULL;
            cls->ram_free_bitmap = NULL;
            cls->ram_alloc       = 0;
            continue;
        }
        cls->ram_base = (uint8_t *)kmalloc(cls->ram_cap * cls->slot_bytes);
        if (!cls->ram_base) {
            KERROR("swap_init: kmalloc ram_base class %u failed", c);
            swap_disk_close();
            return VFS_EIO;
        }
        uint32_t ram_bitmap_bytes = (cls->ram_cap + 7u) / 8u;
        cls->ram_free_bitmap = (uint8_t *)kmalloc(ram_bitmap_bytes);
        if (!cls->ram_free_bitmap) {
            KERROR("swap_init: kmalloc ram_free_bitmap class %u failed", c);
            swap_disk_close();
            return VFS_EIO;
        }
        for (uint32_t i = 0; i < ram_bitmap_bytes; i++) cls->ram_free_bitmap[i] = 0;
        cls->ram_alloc = 0;
    }

    g_lru_head = SWAP_INVALID;
    g_lru_tail = SWAP_INVALID;
    g_evictions = 0;
    g_pinned_count = 0;
    g_initialized = 1;

    KINFO("swap: initialized pool=%u bytes, 4 classes (1K/4K/16K/64K)",
          pool_bytes);
    for (uint8_t c = 0; c < SWAP_DISK_NUM_CLASSES; c++) {
        KINFO("swap: class %u: slot=%u B, disk cap=%u, ram cap=%u",
              c, g_classes[c].slot_bytes, g_classes[c].disk_slot_count,
              g_classes[c].ram_cap);
    }
    return 0;
}

swap_handle_t swap_kmalloc(uint32_t size) {
    if (!g_initialized) return SWAP_INVALID;
    if (size == 0 || size > swap_disk_slot_bytes[SWAP_DISK_NUM_CLASSES - 1]) {
        return SWAP_INVALID;
    }

    int class_idx = pick_class(size);
    if (class_idx < 0) return SWAP_INVALID;

    swap_class_t *cls = &g_classes[class_idx];

    /* Allocate disk slot. */
    int32_t disk = bitmap_alloc(cls->disk_bitmap, cls->disk_slot_count);
    if (disk < 0) return SWAP_INVALID;

    /* Pop a handle from the free list. */
    swap_handle_t h = g_slot_free_head;
    if (h == SWAP_INVALID) {
        bitmap_free(cls->disk_bitmap, (uint32_t)disk);
        return SWAP_INVALID;
    }
    swap_slot_t *s = &g_slots[h - 1];
    g_slot_free_head = s->free_next;
    s->free_next = SWAP_INVALID;

    /* Populate. */
    s->in_use        = 1;
    s->size_class    = (uint8_t)class_idx;
    s->pin_count     = 0;
    s->resident_ptr  = NULL;
    s->disk_slot     = (uint32_t)disk;
    s->size_bytes    = size;
    s->lru_prev      = SWAP_INVALID;
    s->lru_next      = SWAP_INVALID;

    cls->disk_alloc++;
    g_handles_in_use++;
    return h;
}

void *swap_pin(swap_handle_t h) {
    swap_slot_t *s = validate_handle(h);
    if (!s) return NULL;

    /* Already resident -- just bump pin count and remove from LRU if
     * this is the first pin (count was 0). */
    if (s->resident_ptr) {
        if (s->pin_count == 0) lru_remove(h);
        s->pin_count++;
        g_pinned_count++;
        return s->resident_ptr;
    }

    /* Need to load.  First try to get a RAM slot; evict if needed. */
    swap_class_t *cls = &g_classes[s->size_class];
    if (cls->ram_cap == 0) return NULL;   /* pool too small for this class */

    int32_t ram = ram_slot_alloc(cls);
    while (ram < 0) {
        if (class_evict_one(s->size_class) < 0) {
            /* All this class's RAM is pinned. */
            return NULL;
        }
        ram = ram_slot_alloc(cls);
    }

    void *ptr = ram_slot_ptr(cls, (uint32_t)ram);
    int rc = swap_disk_read(s->size_class, s->disk_slot, ptr);
    if (rc < 0) {
        ram_slot_free(cls, (uint32_t)ram);
        return NULL;
    }

    s->resident_ptr = ptr;
    s->pin_count    = 1;
    g_pinned_count++;
    return ptr;
}

void swap_unpin(swap_handle_t h) {
    swap_slot_t *s = validate_handle(h);
    if (!s) return;

    if (s->pin_count == 0) {
        kernel_panic("swap_unpin on already-unpinned handle %u", h);
    }
    s->pin_count--;
    g_pinned_count--;

    /* pin_count hitting 0 -> insert at LRU head (most-recently-used) */
    if (s->pin_count == 0) {
        lru_insert_head(h);
    }
}

void swap_free(swap_handle_t h) {
    swap_slot_t *s = validate_handle(h);
    if (!s) return;  /* invalid: silent no-op (caller bug, but don't panic) */

    if (s->pin_count > 0) {
        kernel_panic("swap_free on pinned handle %u (pin_count=%u)",
                     h, s->pin_count);
    }

    /* Free disk slot. */
    swap_class_t *cls = &g_classes[s->size_class];
    bitmap_free(cls->disk_bitmap, s->disk_slot);
    cls->disk_alloc--;

    /* If resident, release RAM slot and remove from LRU. */
    if (s->resident_ptr) {
        lru_remove(h);
        uint32_t ram_slot = ram_slot_from_ptr(cls, s->resident_ptr);
        ram_slot_free(cls, ram_slot);
        s->resident_ptr = NULL;
    }

    /* Return handle to free list. */
    s->in_use = 0;
    s->free_next = g_slot_free_head;
    g_slot_free_head = h;
    g_handles_in_use--;
}

void swap_stats(swap_stats_t *out) {
    if (!out) return;
    out->handles_in_use = g_handles_in_use;
    out->handles_total  = SWAP_MAX_HANDLES;
    out->evictions      = g_evictions;
    out->pinned_count   = g_pinned_count;
    for (int c = 0; c < SWAP_DISK_NUM_CLASSES; c++) {
        out->disk_alloc[c] = g_classes[c].disk_alloc;
        out->disk_cap[c]   = swap_disk_slot_count[c];
        out->ram_alloc[c]  = g_classes[c].ram_alloc;
        out->ram_cap[c]    = g_classes[c].ram_cap;
    }
}
