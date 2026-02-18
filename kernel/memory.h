#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define CANARY_FRONT 0xDEADBEEF
#define CANARY_BACK 0xBEEFDEAD
#define POISON_FREE 0xFEFEFEFE

typedef struct heap_block {
  uint32_t canary_front; /* Must be CANARY_FRONT           */
  size_t size;           /* Size of the user-data region   */
  struct heap_block *next;
  uint8_t free;
  uint32_t timestamp;     /* Allocation time (ms)           */
  const char *alloc_file; /* Source file of allocation      */
  uint32_t alloc_line;    /* Source line of allocation      */
} heap_block_t;

#define PAGE_SIZE 4096
#define TOTAL_MEMORY_BYTES (128 * 1024 * 1024)
#define IDENTITY_MAP_SIZE TOTAL_MEMORY_BYTES
#define HEAP_INITIAL_PAGES                                                     \
  8192 /* 32MB initial heap */
#define HEAP_MIN_SPLIT (sizeof(heap_block_t) + 8)

#define STACK_BOTTOM 0x800000u /* Bottom of kernel stack (8MB)   */
#define STACK_TOP 0x880000u    /* Top of kernel stack            */
#define STACK_SIZE (STACK_TOP - STACK_BOTTOM)
#define STACK_GUARD_MAGIC 0x5741524Eu /* "WARN" in hex                 */
#define STACK_GUARD_SIZE 16           /* Guard zone at bottom (bytes)   */

void pmm_init(uint32_t kernel_end);
void *pmm_alloc_page(void);
void *pmm_alloc_contiguous(uint32_t page_count);
void pmm_free_page(void *address);
uint32_t pmm_free_pages(void);
uint32_t pmm_total_pages(void);

/**
 * pmm_reserve_region - Mark a physical address range as used.
 * pmm_release_region - Mark a physical address range as free.
 * Used by the ELF loader to reserve/free pages at specific addresses.
 */
void pmm_reserve_region(uint32_t start, uint32_t size);
void pmm_release_region(uint32_t start, uint32_t size);

void paging_init(void);

void heap_init(uint32_t initial_pages);
void *kmalloc_debug(size_t size, const char *file, uint32_t line);
void kfree(void *ptr);

/* Macro so every call site automatically records file + line       */
#define kmalloc(size) kmalloc_debug((size), __FILE__, __LINE__)

#define MAX_ALLOCATIONS 1024

typedef struct allocation_record {
  void *address;      /* User data pointer               */
  uint32_t size;      /* Size in bytes                   */
  uint32_t timestamp; /* When allocated (ms since boot)  */
  const char *file;   /* Source file                     */
  uint32_t line;      /* Source line                     */
  uint8_t active;     /* 1 = active, 0 = freed           */
} allocation_record_t;

typedef struct allocation_tracker {
  allocation_record_t records[MAX_ALLOCATIONS];
  uint32_t next_slot; /* Circular index                  */
  uint32_t active_count;
  uint32_t total_bytes; /* Currently allocated bytes       */
  uint32_t peak_bytes;
  uint32_t peak_count;
} allocation_tracker_t;

/* Check all live blocks for corrupted canaries.  Panics on failure. */
void heap_check_integrity(void);

/* Scan for allocations older than threshold_ms.                     */
void detect_memory_leaks(uint32_t threshold_ms);

/* Print allocation statistics to VGA (and serial).                  */
void print_memory_stats(void);

/* Set output functions for memory debugging (for GUI mode support) */
void memory_set_output(void (*print_fn)(const char *),
                       void (*print_int_fn)(uint32_t));

void stack_guard_init(void);
void stack_guard_check(void);
uint32_t stack_usage_current(void);
uint32_t stack_usage_peak(void);

#endif
