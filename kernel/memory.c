#include "memory.h"
#include "panic.h"
#include "string.h"
#include "kernel.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Physical Memory Manager (PMM) – unchanged logic, cleaned up
 * ══════════════════════════════════════════════════════════════════════ */

#define BITMAP_SIZE (TOTAL_MEMORY_BYTES / PAGE_SIZE / 32)

static uint32_t page_bitmap[BITMAP_SIZE];
static const uint32_t total_pages = TOTAL_MEMORY_BYTES / PAGE_SIZE;
static heap_block_t *heap_head = 0;

/* ── Allocation tracker (global, zero-init) ─────────────────────── */
static allocation_tracker_t tracker;

/* Output function pointers (can be overridden for GUI mode) */
static void (*mem_print)(const char*) = print;
static void (*mem_print_int)(uint32_t) = print_int;

void memory_set_output(void (*print_fn)(const char*), void (*print_int_fn)(uint32_t)) {
    if (print_fn) mem_print = print_fn;
    if (print_int_fn) mem_print_int = print_int_fn;
}

static inline uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}

static inline void bitmap_set(uint32_t index) {
    page_bitmap[index / 32] |= (1u << (index % 32));
}

static inline void bitmap_clear(uint32_t index) {
    page_bitmap[index / 32] &= ~(1u << (index % 32));
}

static inline uint8_t bitmap_test(uint32_t index) {
    return (page_bitmap[index / 32] >> (index % 32)) & 1u;
}

static void pmm_mark_region(uint32_t start, uint32_t end, uint8_t used) {
    if (start >= TOTAL_MEMORY_BYTES) return;
    if (end > TOTAL_MEMORY_BYTES) end = TOTAL_MEMORY_BYTES;

    uint32_t first_page = start / PAGE_SIZE;
    uint32_t last_page  = align_up(end, PAGE_SIZE) / PAGE_SIZE;

    for (uint32_t i = first_page; i < last_page; i++) {
        if (used) bitmap_set(i);
        else      bitmap_clear(i);
    }
}

void pmm_init(uint32_t kernel_end) {
    pmm_mark_region(0, TOTAL_MEMORY_BYTES, 0);
    uint32_t reserved_end = align_up(kernel_end, PAGE_SIZE);
    pmm_mark_region(0, reserved_end, 1);

    pmm_mark_region(0xB8000, 0xC0000, 1);                    /* VGA            */
    pmm_mark_region(0x90000 - (16 * PAGE_SIZE), 0x90000, 1);  /* kernel stack   */
    pmm_mark_region(0xA0000, 0x100000, 1);                    /* BIOS hole      */
}

void *pmm_alloc_contiguous(uint32_t page_count) {
    if (page_count == 0) return 0;

    uint32_t run_start  = 0;
    uint32_t run_length = 0;

    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            if (run_length == 0) run_start = i;
            run_length++;
            if (run_length == page_count) {
                for (uint32_t j = run_start; j < run_start + page_count; j++)
                    bitmap_set(j);
                return (void *)(run_start * PAGE_SIZE);
            }
        } else {
            run_length = 0;
        }
    }
    return 0;
}

void *pmm_alloc_page(void) { return pmm_alloc_contiguous(1); }

void pmm_free_page(void *address) {
    uint32_t addr = (uint32_t)address;
    if (addr >= TOTAL_MEMORY_BYTES || (addr % PAGE_SIZE) != 0) return;
    bitmap_clear(addr / PAGE_SIZE);
}

uint32_t pmm_free_pages(void) {
    uint32_t free = 0;
    for (uint32_t i = 0; i < total_pages; i++)
        if (!bitmap_test(i)) free++;
    return free;
}

uint32_t pmm_total_pages(void) { return total_pages; }

/* ══════════════════════════════════════════════════════════════════════
 *  Allocation Tracker
 * ══════════════════════════════════════════════════════════════════════ */

static void track_allocation(void *user_ptr, uint32_t size,
                             uint32_t ts, const char *file, uint32_t line) {
    allocation_record_t *rec = &tracker.records[tracker.next_slot];

    rec->address   = user_ptr;
    rec->size      = size;
    rec->timestamp = ts;
    rec->file      = file;
    rec->line      = line;
    rec->active    = 1;

    tracker.next_slot = (tracker.next_slot + 1) % MAX_ALLOCATIONS;
    tracker.active_count++;
    tracker.total_bytes += size;

    if (tracker.total_bytes > tracker.peak_bytes)
        tracker.peak_bytes = tracker.total_bytes;
    if (tracker.active_count > tracker.peak_count)
        tracker.peak_count = tracker.active_count;
}

static void untrack_allocation(void *user_ptr, uint32_t size) {
    for (uint32_t i = 0; i < MAX_ALLOCATIONS; i++) {
        if (tracker.records[i].active && tracker.records[i].address == user_ptr) {
            tracker.records[i].active = 0;
            tracker.active_count--;
            tracker.total_bytes -= size;
            return;
        }
    }
    KWARN("Freeing untracked allocation: 0x%x", (uint32_t)user_ptr);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Heap Allocator — with canaries, poisoning & tracking
 * ══════════════════════════════════════════════════════════════════════ */

static heap_block_t *heap_request_block(size_t min_size) {
    size_t required = min_size + sizeof(heap_block_t);
    uint32_t pages  = (uint32_t)(align_up((uint32_t)required, PAGE_SIZE) / PAGE_SIZE);
    if (pages == 0) pages = 1;

    void *region = pmm_alloc_contiguous(pages);
    if (!region) return 0;

    heap_block_t *block = (heap_block_t *)region;
    block->canary_front = CANARY_FRONT;
    block->size         = pages * PAGE_SIZE - sizeof(heap_block_t);
    block->next         = 0;
    block->free         = 1;
    block->timestamp    = 0;
    block->alloc_file   = "heap_expand";
    block->alloc_line   = 0;

    if (!heap_head) {
        heap_head = block;
    } else {
        heap_block_t *cur = heap_head;
        while (cur->next) cur = cur->next;
        cur->next = block;
    }
    return block;
}

void heap_init(uint32_t initial_pages) {
    if (initial_pages == 0) initial_pages = HEAP_INITIAL_PAGES;
    size_t bytes = initial_pages * PAGE_SIZE;

    void *region = pmm_alloc_contiguous(initial_pages);
    if (!region) return;

    heap_head               = (heap_block_t *)region;
    heap_head->canary_front = CANARY_FRONT;
    heap_head->size         = bytes - sizeof(heap_block_t);
    heap_head->next         = 0;
    heap_head->free         = 1;
    heap_head->timestamp    = 0;
    heap_head->alloc_file   = "heap_init";
    heap_head->alloc_line   = 0;
}

static void split_block(heap_block_t *block, size_t requested) {
    size_t total_needed = requested + sizeof(heap_block_t);
    if (block->size <= total_needed + HEAP_MIN_SPLIT) return;

    uint8_t *block_end     = (uint8_t *)block + sizeof(heap_block_t) + block->size;
    uint8_t *new_block_addr = (uint8_t *)block + sizeof(heap_block_t) + requested;
    heap_block_t *nb        = (heap_block_t *)new_block_addr;

    nb->canary_front = CANARY_FRONT;
    nb->size         = (size_t)(block_end - new_block_addr) - sizeof(heap_block_t);
    nb->next         = block->next;
    nb->free         = 1;
    nb->timestamp    = 0;
    nb->alloc_file   = "split";
    nb->alloc_line   = 0;

    block->size = requested;
    block->next = nb;
}

static heap_block_t *find_free_block(size_t size) {
    heap_block_t *cur = heap_head;
    while (cur) {
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return 0;
}

void *kmalloc_debug(size_t size, const char *file, uint32_t line) {
    if (size == 0) return 0;
    size = (size_t)align_up((uint32_t)size, 8);

    /* We need room for user data + 4 bytes for the back canary */
    size_t total = size + sizeof(uint32_t);

    heap_block_t *block = find_free_block(total);
    if (!block) {
        block = heap_request_block(total);
        if (!block) return 0;
    }

    split_block(block, total);
    block->free         = 0;
    block->canary_front = CANARY_FRONT;
    block->timestamp    = timer_get_uptime_ms();
    block->alloc_file   = file;
    block->alloc_line   = line;

    uint8_t  *user_data   = (uint8_t *)block + sizeof(heap_block_t);
    uint32_t *canary_back = (uint32_t *)(user_data + size);
    *canary_back = CANARY_BACK;

    track_allocation(user_data, (uint32_t)size,
                     block->timestamp, file, line);
    return user_data;
}

static void merge_with_next(heap_block_t *block) {
    if (!block || !block->next) return;

    uint8_t *block_end = (uint8_t *)block + sizeof(heap_block_t) + block->size;
    if (block_end == (uint8_t *)block->next && block->next->free) {
        heap_block_t *next = block->next;
        block->size += sizeof(heap_block_t) + next->size;
        block->next  = next->next;
    }
}

void kfree(void *ptr) {
    if (!ptr) return;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    /* ── verify front canary ── */
    if (block->canary_front != CANARY_FRONT) {
        kernel_panic("Heap corruption: front canary destroyed\n"
                     "  Address: 0x%x\n"
                     "  Expected: 0xDEADBEEF  Found: 0x%x",
                     (uint32_t)ptr, block->canary_front);
    }

    /* User size = block->size minus the 4-byte back canary */
    size_t user_size = block->size - sizeof(uint32_t);

    /* ── verify back canary ── */
    uint32_t *canary_back = (uint32_t *)((uint8_t *)ptr + user_size);
    if (*canary_back != CANARY_BACK) {
        kernel_panic("Heap corruption: back canary destroyed (overflow)\n"
                     "  Address: 0x%x  Size: %u bytes\n"
                     "  Expected: 0xBEEFDEAD  Found: 0x%x\n"
                     "  Allocated at %s:%u",
                     (uint32_t)ptr, (uint32_t)user_size,
                     *canary_back,
                     block->alloc_file ? block->alloc_file : "?",
                     block->alloc_line);
    }

    /* Poison freed memory */
    memset(ptr, (int)POISON_FREE, user_size);

    untrack_allocation(ptr, (uint32_t)user_size);

    block->free = 1;
    merge_with_next(block);

    /* merge with predecessor if adjacent */
    heap_block_t *cur = heap_head;
    while (cur && cur->next != block) cur = cur->next;
    if (cur && cur->free) merge_with_next(cur);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Memory safety helpers
 * ══════════════════════════════════════════════════════════════════════ */

void heap_check_integrity(void) {
    uint32_t checked   = 0;
    uint32_t corrupted = 0;

    heap_block_t *cur = heap_head;
    while (cur) {
        if (!cur->free) {
            checked++;
            if (cur->canary_front != CANARY_FRONT) {
                KERROR("Front canary corrupt at block 0x%x (alloc %s:%u)",
                       (uint32_t)cur,
                       cur->alloc_file ? cur->alloc_file : "?",
                       cur->alloc_line);
                corrupted++;
            }

            size_t user_size   = cur->size - sizeof(uint32_t);
            uint8_t *user_data = (uint8_t *)cur + sizeof(heap_block_t);
            uint32_t *cb       = (uint32_t *)(user_data + user_size);
            if (*cb != CANARY_BACK) {
                KERROR("Back canary corrupt at block 0x%x (alloc %s:%u)",
                       (uint32_t)cur,
                       cur->alloc_file ? cur->alloc_file : "?",
                       cur->alloc_line);
                corrupted++;
            }
        }
        cur = cur->next;
    }

    if (corrupted > 0) {
        kernel_panic("Heap integrity check failed: %u/%u blocks corrupted",
                     corrupted, checked);
    }
}

void detect_memory_leaks(uint32_t threshold_ms) {
    uint32_t now = timer_get_uptime_ms();
    uint32_t leaked_count = 0;
    uint32_t leaked_bytes = 0;

    KINFO("Scanning for memory leaks (threshold: %u ms)...", threshold_ms);

    for (uint32_t i = 0; i < MAX_ALLOCATIONS; i++) {
        allocation_record_t *rec = &tracker.records[i];
        if (!rec->active) continue;
        if ((now - rec->timestamp) < threshold_ms) continue;

        leaked_count++;
        leaked_bytes += rec->size;

        serial_printf("  LEAK: %u bytes at 0x%x  allocated at %s:%u  age %u ms\n",
                      rec->size, (uint32_t)rec->address,
                      rec->file ? rec->file : "?", rec->line,
                      now - rec->timestamp);
        mem_print("  LEAK: ");
        mem_print_int(rec->size);
        mem_print(" bytes at ");
        print_hex((uint32_t)rec->address);
        mem_print("  from ");
        mem_print(rec->file ? rec->file : "?");
        mem_print(":");
        mem_print_int(rec->line);
        mem_print("\n");
    }

    if (leaked_count > 0) {
        KERROR("Found %u leaked allocations (%u bytes total)",
               leaked_count, leaked_bytes);
        mem_print("Found "); mem_print_int(leaked_count);
        mem_print(" leaked allocations ("); mem_print_int(leaked_bytes);
        mem_print(" bytes)\n");
    } else {
        KINFO("No leaks detected");
        mem_print("No leaks detected\n");
    }
}

void print_memory_stats(void) {
    mem_print("Memory Statistics:\n");
    mem_print("  Active allocations: "); mem_print_int(tracker.active_count); mem_print("\n");
    mem_print("  Total allocated:    "); mem_print_int(tracker.total_bytes);
    mem_print(" bytes ("); mem_print_int(tracker.total_bytes / 1024); mem_print(" KB)\n");
    mem_print("  Peak allocations:   "); mem_print_int(tracker.peak_count); mem_print("\n");
    mem_print("  Peak memory:        "); mem_print_int(tracker.peak_bytes);
    mem_print(" bytes ("); mem_print_int(tracker.peak_bytes / 1024); mem_print(" KB)\n");

    uint32_t free_pg  = pmm_free_pages();
    uint32_t total_pg = pmm_total_pages();
    mem_print("  Physical pages:     "); mem_print_int(free_pg);
    mem_print(" free / "); mem_print_int(total_pg); mem_print(" total\n");
    mem_print("  Physical free:      "); mem_print_int(free_pg * 4); mem_print(" KB\n");

    serial_printf("memstats: active=%u  total_bytes=%u  peak_bytes=%u  "
                  "free_pages=%u  total_pages=%u\n",
                  tracker.active_count, tracker.total_bytes,
                  tracker.peak_bytes, free_pg, total_pg);
}
