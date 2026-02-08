#include "memory.h"
#include "types.h"

#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_USER 0x4

static uint32_t* page_directory = 0;

static uint32_t* get_page_table(uint32_t directory_index) {
    if (page_directory[directory_index] & PAGE_PRESENT) {
        return (uint32_t*)(page_directory[directory_index] & 0xFFFFF000);
    }

    uint32_t* table = (uint32_t*)pmm_alloc_page();
    if (!table) {
        return 0;
    }

    for (int i = 0; i < 1024; i++) {
        table[i] = 0;
    }

    page_directory[directory_index] = ((uint32_t)table) | PAGE_PRESENT | PAGE_RW;
    return table;
}

static void map_page_identity(uint32_t address) {
    uint32_t directory_index = address >> 22;
    uint32_t table_index = (address >> 12) & 0x3FF;

    uint32_t* table = get_page_table(directory_index);
    if (!table) {
        return;
    }

    table[table_index] = (address & 0xFFFFF000) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
}

void paging_init(void) {
    page_directory = (uint32_t*)pmm_alloc_page();
    if (!page_directory) {
        return;
    }

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0;
    }

    for (uint32_t addr = 0; addr < IDENTITY_MAP_SIZE; addr += PAGE_SIZE) {
        map_page_identity(addr);
    }

    /* Map VBE linear framebuffer region (address stored by bootloader at 0x0500).
     * Covers 640*480*4 = 1,228,800 bytes rounded up to next 4KB page boundary. */
    {
        uint32_t lfb = *(volatile uint32_t *)0x0500U;
        if (lfb != 0U && lfb >= 0x100000U) {
            uint32_t off;
            for (off = 0; off < 0x140000U; off += PAGE_SIZE) {
                map_page_identity(lfb + off);
            }
        }
    }

    uint32_t pd_phys = (uint32_t)page_directory;
    __asm__ volatile("mov %0, %%cr3" :: "r"(pd_phys));

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
}
