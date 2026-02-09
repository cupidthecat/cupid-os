#include "memory.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "kernel.h"
#include "panic.h"
#include "string.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Physical Memory Manager (PMM) – unchanged logic, cleaned up
 * ══════════════════════════════════════════════════════════════════════ */

#define BITMAP_SIZE (TOTAL_MEMORY_BYTES / PAGE_SIZE / 32)

static uint32_t page_bitmap[BITMAP_SIZE];
static const uint32_t total_pages = TOTAL_MEMORY_BYTES / PAGE_SIZE;
static heap_block_t *heap_head = 0;

/* ── Allocation tracker (global, zero-init) ─────────────────────── */
static allocation_tracker_t tracker;

/* ── Stack guard tracking ───────────────────────────────────────── */
static uint32_t stack_peak_usage = 0;

/* ── Output function pointers (for GUI mode) ───────────────────── */
static void (*mem_print)(const char *) = print;
static void (*mem_print_int)(uint32_t) = print_int;

void memory_set_output(void (*print_fn)(const char *),
                       void (*print_int_fn)(uint32_t)) {
  mem_print = print_fn ? print_fn : print;
  mem_print_int = print_int_fn ? print_int_fn : print_int;
}

static inline void bitmap_set(uint32_t page) {
  page_bitmap[page / 32] |= (1u << (page % 32));
}

static inline void bitmap_clear(uint32_t page) {
  page_bitmap[page / 32] &= ~(1u << (page % 32));
}

static inline int bitmap_test(uint32_t page) {
  return (page_bitmap[page / 32] & (1u << (page % 32))) != 0;
}

static inline uint32_t align_up(uint32_t val, uint32_t align) {
  return (val + align - 1) & ~(align - 1);
}

static void pmm_mark_region(uint32_t start, uint32_t end, int used) {
  uint32_t start_page = start / PAGE_SIZE;
  uint32_t end_page = (end + PAGE_SIZE - 1) / PAGE_SIZE;
  for (uint32_t i = start_page; i < end_page && i < total_pages; i++) {
    if (used)
      bitmap_set(i);
    else
      bitmap_clear(i);
  }
}

void pmm_reserve_region(uint32_t start, uint32_t size) {
  if (size == 0)
    return;
  pmm_mark_region(start, start + size, 1);
}

void pmm_release_region(uint32_t start, uint32_t size) {
  if (size == 0)
    return;
  pmm_mark_region(start, start + size, 0);
}

void pmm_init(uint32_t kernel_end) {
  pmm_mark_region(0, TOTAL_MEMORY_BYTES, 0);
  uint32_t reserved_end = align_up(kernel_end, PAGE_SIZE);
  pmm_mark_region(0, reserved_end, 1);

  pmm_mark_region(0xA0000, 0x100000, 1);  /* BIOS/VGA hole  */
  pmm_mark_region(0x200000, 0x280000, 1); /* kernel stack (512KB) */

  /* Reserve CupidC JIT/AOT execution regions so the heap never
   * allocates into them.  Each region = 128KB code + 32KB data. */
  pmm_mark_region(0x00200000, 0x00228000, 1); /* AOT region     */
  pmm_mark_region(0x00400000, 0x00428000, 1); /* JIT region     */

  /* Initialize stack guard after reserving the region */
  stack_guard_init();
}

void *pmm_alloc_contiguous(uint32_t page_count) {
  if (page_count == 0)
    return 0;

  uint32_t run_start = 0;
  uint32_t run_length = 0;

  for (uint32_t i = 0; i < total_pages; i++) {
    if (!bitmap_test(i)) {
      if (run_length == 0)
        run_start = i;
      run_length++;
      if (run_length == page_count) {
        for (uint32_t j = run_start; j < run_start + page_count; j++) {
          bitmap_set(j);
        }
        return (void *)(run_start * PAGE_SIZE);
      }
    } else {
      run_length = 0;
    }
  }
  return 0;
}

void *pmm_alloc_page(void) {
  for (uint32_t i = 0; i < total_pages; i++) {
    if (!bitmap_test(i)) {
      bitmap_set(i);
      return (void *)(i * PAGE_SIZE);
    }
  }
  return 0;
}

void pmm_free_page(void *address) {
  uint32_t page = (uint32_t)address / PAGE_SIZE;
  if (page < total_pages) {
    bitmap_clear(page);
  }
}

uint32_t pmm_free_pages(void) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < total_pages; i++) {
    if (!bitmap_test(i))
      count++;
  }
  return count;
}

uint32_t pmm_total_pages(void) { return total_pages; }

/* ══════════════════════════════════════════════════════════════════════
 *  Heap Allocator with Memory Safety Features
 * ══════════════════════════════════════════════════════════════════════ */

static uint32_t get_back_canary_addr(heap_block_t *block) {
  return (uint32_t)block + sizeof(heap_block_t) + block->size;
}

static void write_canaries(heap_block_t *block) {
  block->canary_front = CANARY_FRONT;
  uint32_t *back_canary = (uint32_t *)get_back_canary_addr(block);
  *back_canary = CANARY_BACK;
}

static int check_canaries(heap_block_t *block) {
  if (block->canary_front != CANARY_FRONT)
    return 0;
  uint32_t *back_canary = (uint32_t *)get_back_canary_addr(block);
  return (*back_canary == CANARY_BACK);
}

void heap_init(uint32_t initial_pages) {
  if (initial_pages == 0)
    return;

  void *base = pmm_alloc_contiguous(initial_pages);
  if (!base) {
    kernel_panic("Failed to allocate initial heap pages");
    return;
  }

  heap_head = (heap_block_t *)base;
  size_t total_size = initial_pages * PAGE_SIZE;
  size_t usable = total_size - sizeof(heap_block_t) - sizeof(uint32_t);

  heap_head->size = usable;
  heap_head->next = 0;
  heap_head->free = 1;
  heap_head->timestamp = 0;
  heap_head->alloc_file = 0;
  heap_head->alloc_line = 0;

  write_canaries(heap_head);

  tracker.next_slot = 0;
  tracker.active_count = 0;
  tracker.total_bytes = 0;
  tracker.peak_bytes = 0;
  tracker.peak_count = 0;

  serial_printf("[heap] Initialized: %u KB at 0x%x\n", (total_size / 1024),
                (uint32_t)base);
}

static void track_allocation(void *ptr, uint32_t size, const char *file,
                             uint32_t line) {
  uint32_t idx = tracker.next_slot;
  tracker.records[idx].address = ptr;
  tracker.records[idx].size = size;
  tracker.records[idx].timestamp = timer_get_uptime_ms();
  tracker.records[idx].file = file;
  tracker.records[idx].line = line;
  tracker.records[idx].active = 1;

  tracker.next_slot = (tracker.next_slot + 1) % MAX_ALLOCATIONS;
  tracker.active_count++;
  tracker.total_bytes += size;

  if (tracker.total_bytes > tracker.peak_bytes)
    tracker.peak_bytes = tracker.total_bytes;
  if (tracker.active_count > tracker.peak_count)
    tracker.peak_count = tracker.active_count;
}

static void track_free(void *ptr) {
  for (uint32_t i = 0; i < MAX_ALLOCATIONS; i++) {
    if (tracker.records[i].active && tracker.records[i].address == ptr) {
      tracker.records[i].active = 0;
      if (tracker.active_count > 0)
        tracker.active_count--;
      if (tracker.total_bytes >= tracker.records[i].size)
        tracker.total_bytes -= tracker.records[i].size;
      return;
    }
  }
}

void *kmalloc_debug(size_t size, const char *file, uint32_t line) {
  if (size == 0)
    return 0;
  if (!heap_head)
    return 0;

  size_t needed = size + sizeof(uint32_t);
  heap_block_t *current = heap_head;

  while (current) {
    if (!check_canaries(current)) {
      serial_printf("[heap] CORRUPTION detected in block at 0x%x\n",
                    (uint32_t)current);
      kernel_panic("Heap corruption detected in kmalloc");
    }

    if (current->free && current->size >= needed) {
      if (current->size >= needed + HEAP_MIN_SPLIT) {
        size_t old_size = current->size;
        current->size = needed;

        heap_block_t *new_block =
            (heap_block_t *)((uint32_t)current + sizeof(heap_block_t) +
                             current->size + sizeof(uint32_t));
        new_block->size =
            old_size - needed - sizeof(heap_block_t) - sizeof(uint32_t);
        new_block->next = current->next;
        new_block->free = 1;
        new_block->timestamp = 0;
        new_block->alloc_file = 0;
        new_block->alloc_line = 0;
        write_canaries(new_block);

        current->next = new_block;
      }

      current->free = 0;
      current->timestamp = timer_get_uptime_ms();
      current->alloc_file = file;
      current->alloc_line = line;
      write_canaries(current);

      void *user_ptr = (void *)((uint32_t)current + sizeof(heap_block_t));
      track_allocation(user_ptr, (uint32_t)size, file, line);

      return user_ptr;
    }
    current = current->next;
  }

  serial_printf("[heap] kmalloc(%u) failed - out of memory\n", (uint32_t)size);
  return 0;
}

void kfree(void *ptr) {
  if (!ptr)
    return;

  heap_block_t *block = (heap_block_t *)((uint32_t)ptr - sizeof(heap_block_t));

  if (!check_canaries(block)) {
    serial_printf("[heap] Double-free or corruption at 0x%x\n", (uint32_t)ptr);
    kernel_panic("Heap corruption detected in kfree");
  }

  if (block->free) {
    serial_printf(
        "[heap] Double-free detected at 0x%x (previously freed at %u ms)\n",
        (uint32_t)ptr, block->timestamp);
    kernel_panic("Double-free detected");
  }

  block->free = 1;
  block->timestamp = timer_get_uptime_ms();

  uint8_t *data = (uint8_t *)ptr;
  for (size_t i = 0; i < block->size; i++) {
    data[i] = (uint8_t)(POISON_FREE >> ((i % 4) * 8));
  }

  track_free(ptr);

  heap_block_t *current = heap_head;
  while (current && current->next) {
    if (current->free && current->next->free) {
      heap_block_t *next = current->next;
      current->size += sizeof(heap_block_t) + next->size + sizeof(uint32_t);
      current->next = next->next;
      write_canaries(current);
    } else {
      current = current->next;
    }
  }
}

void heap_check_integrity(void) {
  heap_block_t *current = heap_head;
  uint32_t block_count = 0;
  uint32_t corruption_count = 0;

  while (current) {
    block_count++;
    if (!check_canaries(current)) {
      corruption_count++;
      serial_printf("[heap] Block %u at 0x%x: CORRUPTED (size=%u, free=%u)\n",
                    block_count, (uint32_t)current, current->size,
                    current->free);
    }
    current = current->next;
  }

  if (corruption_count > 0) {
    serial_printf("[heap] INTEGRITY CHECK FAILED: %u/%u blocks corrupted\n",
                  corruption_count, block_count);
    kernel_panic("Heap integrity check failed");
  } else {
    serial_printf("[heap] Integrity check passed: %u blocks OK\n", block_count);
  }
}

void detect_memory_leaks(uint32_t threshold_ms) {
  uint32_t now = timer_get_uptime_ms();
  uint32_t leak_count = 0;
  uint32_t leak_bytes = 0;

  serial_printf("[heap] Scanning for leaks (threshold: %u ms)...\n",
                threshold_ms);

  for (uint32_t i = 0; i < MAX_ALLOCATIONS; i++) {
    if (tracker.records[i].active) {
      uint32_t age = now - tracker.records[i].timestamp;
      if (age >= threshold_ms) {
        leak_count++;
        leak_bytes += tracker.records[i].size;
        serial_printf(
            "[heap] LEAK: %u bytes at 0x%x (age: %u ms, %s:%u)\n",
            tracker.records[i].size, (uint32_t)tracker.records[i].address, age,
            tracker.records[i].file ? tracker.records[i].file : "unknown",
            tracker.records[i].line);
      }
    }
  }

  if (leak_count > 0) {
    serial_printf("[heap] Found %u leaks totaling %u bytes\n", leak_count,
                  leak_bytes);
    mem_print("Memory leaks detected: ");
    mem_print_int(leak_count);
    mem_print(" allocations, ");
    mem_print_int(leak_bytes);
    mem_print(" bytes\n");
  } else {
    KINFO("No leaks detected");
    mem_print("No leaks detected\n");
  }
}

void print_memory_stats(void) {
  mem_print("Memory Statistics:\n");
  mem_print("  Active allocations: ");
  mem_print_int(tracker.active_count);
  mem_print("\n");
  mem_print("  Total allocated:    ");
  mem_print_int(tracker.total_bytes);
  mem_print(" bytes (");
  mem_print_int(tracker.total_bytes / 1024);
  mem_print(" KB)\n");
  mem_print("  Peak allocations:   ");
  mem_print_int(tracker.peak_count);
  mem_print("\n");
  mem_print("  Peak memory:        ");
  mem_print_int(tracker.peak_bytes);
  mem_print(" bytes (");
  mem_print_int(tracker.peak_bytes / 1024);
  mem_print(" KB)\n");

  uint32_t free_pg = pmm_free_pages();
  uint32_t total_pg = pmm_total_pages();
  mem_print("  Physical pages:     ");
  mem_print_int(free_pg);
  mem_print(" free / ");
  mem_print_int(total_pg);
  mem_print(" total\n");
  mem_print("  Physical free:      ");
  mem_print_int(free_pg * 4);
  mem_print(" KB\n");

  serial_printf("memstats: active=%u  total_bytes=%u  peak_bytes=%u  "
                "free_pages=%u  total_pages=%u\n",
                tracker.active_count, tracker.total_bytes, tracker.peak_bytes,
                free_pg, total_pg);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Stack Guard Implementation
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * stack_guard_init - Initialize stack guard canaries
 *
 * Writes magic values to the bottom STACK_GUARD_SIZE bytes of the kernel
 * stack. If the stack overflows, these will be corrupted and detected.
 */
void stack_guard_init(void) {
  uint32_t *guard_zone = (uint32_t *)STACK_BOTTOM;

  /* Fill guard zone with magic values */
  for (uint32_t i = 0; i < STACK_GUARD_SIZE / sizeof(uint32_t); i++) {
    guard_zone[i] = STACK_GUARD_MAGIC;
  }

  stack_peak_usage = 0;

  serial_printf("[stack] Guard initialized: %u KB stack (%x - %x)\n",
                STACK_SIZE / 1024, STACK_BOTTOM, STACK_TOP);
}

/**
 * stack_guard_check - Verify stack guard is intact
 *
 * Checks if the guard zone at the bottom of the stack has been corrupted.
 * If corruption is detected, triggers a panic with diagnostic information.
 */
void stack_guard_check(void) {
  uint32_t *guard_zone = (uint32_t *)STACK_BOTTOM;
  uint32_t corrupted_count = 0;

  /* Check each guard value */
  for (uint32_t i = 0; i < STACK_GUARD_SIZE / sizeof(uint32_t); i++) {
    if (guard_zone[i] != STACK_GUARD_MAGIC) {
      corrupted_count++;
    }
  }

  if (corrupted_count > 0) {
    /* Get current stack pointer */
    uint32_t esp;
    __asm__ volatile("movl %%esp, %0" : "=r"(esp));

    uint32_t current_usage = STACK_TOP - esp;

    serial_printf("[stack] OVERFLOW DETECTED!\n");
    serial_printf("[stack] Guard zone corruption: %u/%u values corrupted\n",
                  corrupted_count, STACK_GUARD_SIZE / 4);
    serial_printf("[stack] Current ESP: 0x%x (usage: %u bytes)\n", esp,
                  current_usage);
    serial_printf("[stack] Peak usage: %u bytes\n", stack_peak_usage);
    serial_printf("[stack] Stack bounds: 0x%x - 0x%x (%u KB)\n", STACK_BOTTOM,
                  STACK_TOP, STACK_SIZE / 1024);

    kernel_panic("STACK OVERFLOW: Guard zone corrupted");
  }
}

/**
 * stack_usage_current - Get current stack usage in bytes
 *
 * Calculates how much of the kernel stack is currently in use by
 * reading ESP and comparing it to the stack top.
 */
uint32_t stack_usage_current(void) {
  uint32_t esp;
  __asm__ volatile("movl %%esp, %0" : "=r"(esp));

  /* Stack grows downward, so usage = top - current */
  uint32_t usage = STACK_TOP - esp;

  /* Track peak usage */
  if (usage > stack_peak_usage) {
    stack_peak_usage = usage;
  }

  return usage;
}

/**
 * stack_usage_peak - Get peak stack usage in bytes
 *
 * Returns the maximum stack depth seen since boot.
 */
uint32_t stack_usage_peak(void) { return stack_peak_usage; }
