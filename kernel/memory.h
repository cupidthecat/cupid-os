#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

typedef struct heap_block {
    size_t size;
    struct heap_block* next;
    uint8_t free;
} heap_block_t;

#define PAGE_SIZE 4096
#define TOTAL_MEMORY_BYTES (32 * 1024 * 1024)
#define IDENTITY_MAP_SIZE TOTAL_MEMORY_BYTES
#define HEAP_INITIAL_PAGES 16
#define HEAP_MIN_SPLIT (sizeof(heap_block_t) + 8)

void pmm_init(uint32_t kernel_end);
void* pmm_alloc_page(void);
void* pmm_alloc_contiguous(uint32_t page_count);
void pmm_free_page(void* address);
uint32_t pmm_free_pages(void);
uint32_t pmm_total_pages(void);

void paging_init(void);

void heap_init(uint32_t initial_pages);
void* kmalloc(size_t size);
void kfree(void* ptr);

#endif
