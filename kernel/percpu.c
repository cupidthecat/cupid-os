#include "percpu.h"
#include "../drivers/serial.h"

per_cpu_t cpus[SMP_MAX_CPUS];
int smp_cpu_count_var = 1;

/* 5 boot entries (null, code, data, reserved, reserved) + 32 per-CPU entries */
#define GDT_ENTRIES (GDT_GS_BASE_INDEX + SMP_MAX_CPUS)
static uint64_t gdt_entries[GDT_ENTRIES] __attribute__((aligned(8)));

int smp_cpu_count(void)   { return smp_cpu_count_var; }
int smp_current_cpu(void) { return (int)this_cpu()->cpu_id; }

static uint64_t make_data_seg(uint32_t base, uint32_t limit) {
    uint64_t d = 0;
    d |= (uint64_t)(limit & 0xFFFFu);
    d |= (uint64_t)(base & 0xFFFFFFu) << 16;
    d |= (uint64_t)0x92u << 40;             /* data, present, ring0, RW */
    d |= (uint64_t)((limit >> 16) & 0xFu) << 48;
    d |= (uint64_t)0x4u << 52;              /* flags: 32-bit, no granularity */
    d |= (uint64_t)((base >> 24) & 0xFFu) << 56;
    return d;
}

/* Boot.asm GDT entries -- match byte-for-byte so CS=0x08, DS=0x10 stay valid.
 * Code: limit 0xFFFFF, base 0, access 0x9A, flags 0xC (4KB, 32-bit)
 * Data: limit 0xFFFFF, base 0, access 0x92, flags 0xC, base_hi 0 */
static uint64_t make_code32_kernel(void) {
    uint64_t d = 0;
    d |= 0xFFFFull;                          /* limit low */
    d |= (uint64_t)0x9Aull << 40;            /* code+present+ring0+readable */
    d |= (uint64_t)0xFull << 48;             /* limit high = F (full 0xFFFFF) */
    d |= (uint64_t)0xCull << 52;             /* granularity 4KB + 32-bit */
    return d;
}

static uint64_t make_data32_kernel(void) {
    uint64_t d = 0;
    d |= 0xFFFFull;
    d |= (uint64_t)0x92ull << 40;
    d |= (uint64_t)0xFull << 48;
    d |= (uint64_t)0xCull << 52;
    return d;
}

/* Called by each AP immediately upon entering C, before this_cpu().
 * Loads the kernel's extended GDT (already built by percpu_init_bsp)
 * and sets %gs to the AP's per-CPU descriptor so this_cpu() works. */
void percpu_load_kernel_gdt(int cpu_id) {
    struct __attribute__((packed)) {
        uint16_t limit;
        uint32_t base;
    } gdtr;
    gdtr.limit = (uint16_t)(sizeof(gdt_entries) - 1u);
    gdtr.base  = (uint32_t)&gdt_entries[0];
    __asm__ volatile(
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        : : "m"(gdtr) : "ax", "memory");
    /* Reload CS by pushing return address and doing a far-return. */
    __asm__ volatile(
        "pushl $0x08\n"
        "pushl $1f\n"
        "lretl\n"
        "1:\n"
        ::: "memory");
    {
        uint16_t gs_sel = (uint16_t)((GDT_GS_BASE_INDEX + cpu_id) << 3);
        __asm__ volatile("mov %0, %%gs" : : "r"(gs_sel));
    }
}

void percpu_init_bsp(void) {
    int i;
    for (i = 0; i < SMP_MAX_CPUS; i++) {
        cpus[i].self_ptr       = &cpus[i];
        cpus[i].cpu_id         = (uint8_t)i;
        cpus[i].apic_id        = 0xFFu;   /* 0xFF = undiscovered; T5/T6 fill in real IDs */
        cpus[i].bootstrap      = 0;
        cpus[i].online         = 0;
        cpus[i].current        = 0;
        cpus[i].idle           = 0;
        cpus[i].bkl_depth      = 0;
        cpus[i].preempt_count  = 0;
        cpus[i].idle_stack_top = 0;
        cpus[i].bkl_eflags_saved = 0;
        cpus[i].current_pid    = 0;
        cpus[i].call_fn        = 0;
        cpus[i].call_arg       = 0;
        cpus[i].call_pending   = 0;
        cpus[i].call_done      = 0;
    }
    cpus[0].bootstrap = 1;
    cpus[0].online    = 1;

    /* Build new GDT in C. */
    gdt_entries[0] = 0;                      /* null */
    gdt_entries[1] = make_code32_kernel();   /* 0x08 CS */
    gdt_entries[2] = make_data32_kernel();   /* 0x10 DS/ES/SS */
    gdt_entries[3] = 0;                      /* reserved */
    gdt_entries[4] = 0;                      /* reserved */

    for (i = 0; i < SMP_MAX_CPUS; i++) {
        uint32_t limit_bytes = (uint32_t)(sizeof(per_cpu_t) - 1u);
        uint32_t base = (uint32_t)&cpus[i];
        gdt_entries[GDT_GS_BASE_INDEX + i] =
            make_data_seg(base, limit_bytes);
    }

    /* Load new GDTR. */
    struct __attribute__((packed)) {
        uint16_t limit;
        uint32_t base;
    } gdtr;
    gdtr.limit = (uint16_t)(sizeof(gdt_entries) - 1u);
    gdtr.base  = (uint32_t)&gdt_entries[0];

    __asm__ volatile(
        "lgdt %0\n"
        /* Reload data segments (forces CPU to re-read descriptor from new GDT). */
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        /* Reload CS via far-jump. */
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "m"(gdtr) : "ax", "memory");

    /* Load BSP's GS selector. */
    {
        uint16_t gs_sel = (uint16_t)((GDT_GS_BASE_INDEX + 0) << 3);
        __asm__ volatile("mov %0, %%gs" : : "r"(gs_sel));
        KINFO("percpu: BSP gs_sel=%x cpus[]=%p gdt_base=%x",
              (unsigned)gs_sel, (void*)cpus, gdtr.base);
    }
}
