#include "memory.h"

#define BITMAP_SIZE (TOTAL_MEMORY_BYTES / PAGE_SIZE / 32)

static uint32_t page_bitmap[BITMAP_SIZE];
static const uint32_t total_pages = TOTAL_MEMORY_BYTES / PAGE_SIZE;
static heap_block_t* heap_head = 0;

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
    uint32_t last_page = align_up(end, PAGE_SIZE) / PAGE_SIZE;

    for (uint32_t i = first_page; i < last_page; i++) {
        if (used) {
            bitmap_set(i);
        } else {
            bitmap_clear(i);
        }
    }
}

void pmm_init(uint32_t kernel_end) {
    // Mark everything free then reserve the kernel region (including boot + stack)
    pmm_mark_region(0, TOTAL_MEMORY_BYTES, 0);
    uint32_t reserved_end = align_up(kernel_end, PAGE_SIZE);
    pmm_mark_region(0, reserved_end, 1);

    // Keep critical low memory regions off-limits
    pmm_mark_region(0xB8000, 0xC0000, 1);                 // VGA text buffer
    pmm_mark_region(0x90000 - (16 * PAGE_SIZE), 0x90000, 1); // Kernel stack (64KB)
    pmm_mark_region(0xA0000, 0x100000, 1);                // Legacy video/BIOS hole
}

void* pmm_alloc_contiguous(uint32_t page_count) {
    if (page_count == 0) return 0;

    uint32_t run_start = 0;
    uint32_t run_length = 0;

    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            if (run_length == 0) run_start = i;
            run_length++;
            if (run_length == page_count) {
                for (uint32_t j = run_start; j < run_start + page_count; j++) {
                    bitmap_set(j);
                }
                return (void*)((run_start * PAGE_SIZE));
            }
        } else {
            run_length = 0;
        }
    }
    return 0;
}

void* pmm_alloc_page(void) {
    return pmm_alloc_contiguous(1);
}

void pmm_free_page(void* address) {
    uint32_t addr = (uint32_t)address;
    if (addr >= TOTAL_MEMORY_BYTES || (addr % PAGE_SIZE) != 0) {
        return;
    }
    uint32_t index = addr / PAGE_SIZE;
    bitmap_clear(index);
}

uint32_t pmm_free_pages(void) {
    uint32_t free = 0;
    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) free++;
    }
    return free;
}

uint32_t pmm_total_pages(void) {
    return total_pages;
}

static heap_block_t* heap_request_block(size_t min_size) {
    size_t required = min_size + sizeof(heap_block_t);
    uint32_t pages = (uint32_t)((align_up((uint32_t)required, PAGE_SIZE)) / PAGE_SIZE);
    if (pages == 0) pages = 1;

    void* region = pmm_alloc_contiguous(pages);
    if (!region) {
        return 0;
    }

    heap_block_t* block = (heap_block_t*)region;
    block->size = pages * PAGE_SIZE - sizeof(heap_block_t);
    block->next = 0;
    block->free = 1;

    if (!heap_head) {
        heap_head = block;
    } else {
        heap_block_t* current = heap_head;
        while (current->next) {
            current = current->next;
        }
        current->next = block;
    }
    return block;
}

void heap_init(uint32_t initial_pages) {
    if (initial_pages == 0) {
        initial_pages = HEAP_INITIAL_PAGES;
    }
    size_t bytes = initial_pages * PAGE_SIZE;
    void* region = pmm_alloc_contiguous(initial_pages);
    if (!region) {
        return;
    }

    heap_head = (heap_block_t*)region;
    heap_head->size = bytes - sizeof(heap_block_t);
    heap_head->next = 0;
    heap_head->free = 1;
}

static void split_block(heap_block_t* block, size_t requested) {
    size_t total_needed = requested + sizeof(heap_block_t);
    if (block->size <= total_needed + HEAP_MIN_SPLIT) {
        return;
    }

    uint8_t* block_end = (uint8_t*)block + sizeof(heap_block_t) + block->size;
    uint8_t* new_block_addr = (uint8_t*)block + sizeof(heap_block_t) + requested;
    heap_block_t* new_block = (heap_block_t*)new_block_addr;

    new_block->size = (size_t)(block_end - new_block_addr) - sizeof(heap_block_t);
    new_block->next = block->next;
    new_block->free = 1;

    block->size = requested;
    block->next = new_block;
}

static heap_block_t* find_free_block(size_t size) {
    heap_block_t* current = heap_head;
    while (current) {
        if (current->free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    return 0;
}

void* kmalloc(size_t size) {
    if (size == 0) return 0;
    size = align_up((uint32_t)size, 8);

    heap_block_t* block = find_free_block(size);
    if (!block) {
        block = heap_request_block(size);
        if (!block) {
            return 0;
        }
    }

    split_block(block, size);
    block->free = 0;
    return (void*)((uint8_t*)block + sizeof(heap_block_t));
}

static void merge_with_next(heap_block_t* block) {
    if (!block || !block->next) return;

    uint8_t* block_end = (uint8_t*)block + sizeof(heap_block_t) + block->size;
    if (block_end == (uint8_t*)block->next && block->next->free) {
        heap_block_t* next = block->next;
        block->size += sizeof(heap_block_t) + next->size;
        block->next = next->next;
    }
}

void kfree(void* ptr) {
    if (!ptr) return;

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->free = 1;
    merge_with_next(block);

    // Attempt to merge previous if it is adjacent
    heap_block_t* current = heap_head;
    while (current && current->next != block) {
        current = current->next;
    }
    if (current && current->free) {
        merge_with_next(current);
    }
}
