#include "bkl.h"
#include "percpu.h"

typedef struct {
    volatile uint32_t ticket_head;
    volatile uint32_t ticket_tail;
    int32_t  owner_cpu;
    uint32_t depth;
} bkl_t;

static bkl_t klock;
static bool bkl_init_done = false;

void bkl_init(void) {
    klock.ticket_head = 0;
    klock.ticket_tail = 0;
    klock.owner_cpu = -1;
    klock.depth = 0;
    bkl_init_done = true;
}

bool bkl_is_initialized(void) { return bkl_init_done; }

static uint32_t save_and_cli(void) {
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    return eflags;
}

static void restore_if(uint32_t eflags) {
    if (eflags & (1u << 9)) __asm__ volatile("sti");
}

void bkl_lock(void) {
    uint32_t eflags = save_and_cli();
    per_cpu_t *c = this_cpu();
    if (klock.owner_cpu == (int32_t)c->cpu_id) {
        klock.depth++;
        c->bkl_depth = klock.depth;
        return;  /* inner acquisition; outer saved eflags already */
    }
    uint32_t my_ticket = __atomic_fetch_add(&klock.ticket_tail, 1, __ATOMIC_SEQ_CST);
    while (__atomic_load_n(&klock.ticket_head, __ATOMIC_ACQUIRE) != my_ticket) {
        __asm__ volatile("pause");
    }
    klock.owner_cpu = (int32_t)c->cpu_id;
    klock.depth = 1;
    c->bkl_eflags_saved = eflags;
    c->bkl_depth = 1;
}

void bkl_unlock(void) {
    per_cpu_t *c = this_cpu();
    if (klock.depth == 0) return;   /* programming error — ignore */
    uint32_t eflags = c->bkl_eflags_saved;
    klock.depth--;
    c->bkl_depth = klock.depth;
    if (klock.depth == 0) {
        klock.owner_cpu = -1;
        __atomic_fetch_add(&klock.ticket_head, 1, __ATOMIC_RELEASE);
        restore_if(eflags);
    }
}

bool bkl_held_by_this_cpu(void) {
    return klock.owner_cpu == (int32_t)this_cpu()->cpu_id;
}
