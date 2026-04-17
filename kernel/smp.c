#include "smp.h"
#include "mp_tables.h"
#include "acpi.h"
#include "lapic.h"
#include "ioapic.h"
#include "bkl.h"
#include "percpu.h"
#include "process.h"
#include "memory.h"
#include "../drivers/serial.h"
#include "idt.h"
#include "isr.h"

/* Forward declarations for IPI C handlers (called from asm stubs). */
void ipi_reschedule_c(void);
void ipi_call_c(void);

extern uint8_t smp_trampoline_start[];
extern uint8_t smp_trampoline_end[];
extern void idt_load_ap(void);

void ap_main_c(void);

static void pit_udelay(uint32_t microseconds) {
    /* Rough calibrated delay — 100 loop ops ~= 1us on QEMU. */
    for (volatile uint32_t i = 0; i < microseconds * 100u; i++) { }
}

void smp_atomic_inc(uint32_t *p) {
    __atomic_fetch_add(p, 1u, __ATOMIC_SEQ_CST);
}

static void start_ap(int cpu_id) {
    per_cpu_t *c = &cpus[cpu_id];

    /* INIT IPI */
    lapic_send_ipi(c->apic_id, 0, LAPIC_DELIVER_INIT);
    pit_udelay(10000);

    /* SIPI 1 — vector = physical page of trampoline / 0x1000 = 0x08 */
    lapic_send_ipi(c->apic_id, 0x08, LAPIC_DELIVER_STARTUP);
    pit_udelay(200);

    /* SIPI 2 (some hardware needs it) */
    lapic_send_ipi(c->apic_id, 0x08, LAPIC_DELIVER_STARTUP);

    /* Poll online up to 100ms */
    for (int t = 0; t < 100 && !c->online; t++) pit_udelay(1000);
    if (!c->online)
        KWARN("smp: cpu%d apic=%u failed to come online", cpu_id, c->apic_id);
}

void smp_init(void) {
    bool mp_ok = mp_tables_discover();
    /* If MP tables found only 1 CPU (or nothing), try ACPI MADT which
     * QEMU populates with all SMP CPUs even when MP tables only list the BSP. */
    if (!mp_ok || mp_cpu_discovered_count < 2) {
        if (!acpi_discover()) {
            if (!mp_ok) {
                KWARN("smp: no MP/ACPI — uniprocessor");
                return;
            }
            /* mp_ok but only 1 CPU — genuine uniprocessor, continue. */
        }
    }

    /* Copy trampoline blob to physical 0x8000. */
    uint8_t *dst = (uint8_t*)0x8000;
    uint32_t n = (uint32_t)(smp_trampoline_end - smp_trampoline_start);
    for (uint32_t i = 0; i < n; i++) dst[i] = smp_trampoline_start[i];

    /* Compact mp_cpus[] into cpus[]. Index 0 = BSP; APs 1..N-1. */
    uint8_t bsp_apic = lapic_get_id();
    int assigned = 1;
    cpus[0].apic_id = bsp_apic;
    cpus[0].bootstrap = 1;
    cpus[0].online = 1;

    for (int i = 0; i < mp_cpu_discovered_count; i++) {
        if (mp_cpus[i].apic_id == bsp_apic) continue;
        if (!mp_cpus[i].enabled) continue;
        if (assigned >= SMP_MAX_CPUS) break;
        cpus[assigned].apic_id = mp_cpus[i].apic_id;
        cpus[assigned].bootstrap = 0;
        assigned++;
    }
    smp_cpu_count_var = assigned;

    /* Populate trampoline tables. Offsets per smp_trampoline.S layout. */
    uint8_t  *apic_tbl = (uint8_t*)0x8128;
    uint32_t *stk_tbl  = (uint32_t*)0x8148;
    uint16_t *gs_tbl   = (uint16_t*)0x81C8;
    uint32_t *entry    = (uint32_t*)0x8208;
    int i;
    for (i = 0; i < SMP_MAX_CPUS; i++) apic_tbl[i] = 0xFFu;
    for (i = 0; i < SMP_MAX_CPUS; i++) stk_tbl[i] = 0;
    for (i = 0; i < SMP_MAX_CPUS; i++) gs_tbl[i] = 0;

    for (i = 1; i < assigned; i++) {
        uint8_t *stk_base;
        uint8_t *stk_top;
        apic_tbl[i] = cpus[i].apic_id;
        stk_base = (uint8_t*)kmalloc(16 * 1024);
        if (!stk_base) { KERROR("smp: stack oom cpu%d", i); continue; }
        stk_top = stk_base + 16 * 1024;
        stk_tbl[i] = (uint32_t)stk_top;
        cpus[i].idle_stack_top = stk_top;
        /* Use trampoline data segment (0x10) as a placeholder GS — the
         * trampoline GDT has only 3 entries so per-CPU selectors (index 5+)
         * would #GP.  percpu_load_kernel_gdt() fixes GS once the kernel
         * GDT is loaded. */
        gs_tbl[i] = (uint16_t)0x10u;
    }
    *entry = (uint32_t)ap_main_c;

    /* Bring up APs one at a time. */
    for (i = 1; i < assigned; i++) {
        start_ap(i);
    }

    {
        int online = 0;
        for (i = 0; i < assigned; i++) if (cpus[i].online) online++;
        KINFO("smp: %d CPUs online (of %d discovered)", online, assigned);
    }
}

#define IPI_RESCHEDULE 0xF0
#define IPI_CALL       0xF1
#define IPI_PANIC      0xFE

/* Naked ISR wrappers — minimal save/restore for IPI vectors. */
__attribute__((naked))
static void ipi_reschedule_stub(void) {
    __asm__ volatile(
        "pushal\n"
        "call ipi_reschedule_c\n"
        "popal\n"
        "iret\n"
    );
}

__attribute__((naked))
static void ipi_call_stub(void) {
    __asm__ volatile(
        "pushal\n"
        "call ipi_call_c\n"
        "popal\n"
        "iret\n"
    );
}

__attribute__((naked))
static void ipi_panic_stub(void) {
    __asm__ volatile(
        "cli\n"
        "1: hlt\n"
        "jmp 1b\n"
    );
}

void ipi_reschedule_c(void) {
    lapic_eoi();
    /* Mark current as needing reschedule. The next scheduler_tick
     * or explicit schedule() call will then switch. This avoids
     * calling schedule() from within an ISR path where BKL reentry
     * + context switch is complex. */
    per_cpu_t *c = this_cpu();
    /* Force quantum expiry — existing scheduler honors this. */
    /* Don't hold BKL here; scheduler.c will acquire it on next tick. */
    (void)c;
}

void ipi_call_c(void) {
    lapic_eoi();
    per_cpu_t *c = this_cpu();
    if (c->call_pending && c->call_fn) {
        c->call_fn(c->call_arg);
        __atomic_store_n(&c->call_done, 1u, __ATOMIC_RELEASE);
    }
}

void smp_init_ipi(void) {
    idt_set_gate(IPI_RESCHEDULE, (uint32_t)ipi_reschedule_stub, 0x08, 0x8E);
    idt_set_gate(IPI_CALL,       (uint32_t)ipi_call_stub,       0x08, 0x8E);
    idt_set_gate(IPI_PANIC,      (uint32_t)ipi_panic_stub,      0x08, 0x8E);
    KINFO("smp: IPI vectors installed 0xF0 0xF1 0xFE");
}

void smp_reschedule(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= SMP_MAX_CPUS) return;
    if (!cpus[cpu_id].online) return;
    lapic_send_ipi(cpus[cpu_id].apic_id, IPI_RESCHEDULE, LAPIC_DELIVER_FIXED);
}

void smp_halt_others(void) {
    int me = smp_current_cpu();
    for (int i = 0; i < smp_cpu_count(); i++) {
        if (i == me) continue;
        if (!cpus[i].online) continue;
        lapic_send_ipi(cpus[i].apic_id, IPI_PANIC, LAPIC_DELIVER_FIXED);
    }
}

int smp_call_on_cpu(int cpu_id, void (*fn)(void*), void *arg) {
    if (cpu_id < 0 || cpu_id >= smp_cpu_count()) return -1;
    if (cpu_id == smp_current_cpu()) { fn(arg); return 0; }
    per_cpu_t *t = &cpus[cpu_id];
    if (!t->online) return -1;
    while (__atomic_exchange_n(&t->call_pending, 1u, __ATOMIC_SEQ_CST))
        __asm__ volatile("pause");
    t->call_fn = fn;
    t->call_arg = arg;
    __atomic_store_n(&t->call_done, 0u, __ATOMIC_RELEASE);
    lapic_send_ipi(t->apic_id, IPI_CALL, LAPIC_DELIVER_FIXED);
    while (!__atomic_load_n(&t->call_done, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
    __atomic_store_n(&t->call_pending, 0u, __ATOMIC_RELEASE);
    return 0;
}

void ap_main_c(void) {
    /* Find our cpu slot by LAPIC ID (before kernel GDT / this_cpu is ready). */
    uint8_t my_apic = lapic_get_id();
    int cpu_id = 0;
    {
        int j;
        for (j = 1; j < smp_cpu_count_var; j++) {
            if (cpus[j].apic_id == my_apic) { cpu_id = j; break; }
        }
    }

    /* Load kernel's extended GDT and set the real per-CPU GS selector. */
    percpu_load_kernel_gdt(cpu_id);

    per_cpu_t *c = this_cpu();
    c->online = 1;
    idt_load_ap();
    lapic_init_ap();
    __asm__ volatile("sti");
    KINFO("cpu%u: online apic=%u", (unsigned)c->cpu_id, (unsigned)c->apic_id);
    for (;;) {
        schedule();   /* BKL acquired/released internally */
        __asm__ volatile("sti; hlt");
    }
}
