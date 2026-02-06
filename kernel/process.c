/**
 * process.c - Process management and round-robin scheduler for CupidOS
 *
 * Implements cooperative/preemptive multitasking using kernel threads.
 * All processes share the same flat 32-bit address space and run in ring 0.
 *
 * Context switch strategy:
 *   We use a pure-assembly context_switch() routine (context_switch.asm).
 *   It saves all callee-saved registers (EBX, ESI, EDI, EBP, EFLAGS)
 *   onto the current stack, stores ESP into the old process's PCB,
 *   then switches ESP to the new process and jumps to its saved EIP.
 *
 *   Resumed processes jump to context_switch_resume which pops the
 *   saved registers and does `ret`, returning normally to whoever
 *   called schedule().
 *
 *   New processes have their initial EIP set to their entry function
 *   with a return address of process_exit_trampoline on the stack.
 */

#include "process.h"
#include "kernel.h"
#include "memory.h"
#include "string.h"
#include "types.h"
#include "debug.h"
#include "../drivers/serial.h"

/* ── Assembly context switch (context_switch.asm) ─────────────────── */
extern void context_switch(uint32_t *old_esp, uint32_t new_esp, uint32_t new_eip);
extern void context_switch_resume(void);  /* resume label address */

/* ── Internal state ───────────────────────────────────────────────── */
static process_t  process_table[MAX_PROCESSES];
static uint32_t   current_pid          = 0;
static uint32_t   next_schedule_index  = 0;
static uint32_t   process_count        = 0;
static bool       scheduler_active     = false;

/* ── State name strings ───────────────────────────────────────────── */
static const char *state_names[] = {
    "READY",
    "RUNNING",
    "BLOCKED",
    "TERMINATED"
};

/* ── Forward declarations ─────────────────────────────────────────── */
static void idle_process(void);
static uint32_t find_free_slot(void);

/* ══════════════════════════════════════════════════════════════════════
 *  Trampoline: if a process entry function returns, we land here
 *  and call process_exit() to clean up.
 * ══════════════════════════════════════════════════════════════════════ */
static void process_exit_trampoline(void) {
    process_exit();
    while (1) { __asm__ volatile("hlt"); }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Idle process — PID 1, always present
 * ══════════════════════════════════════════════════════════════════════ */
static void idle_process(void) {
    while (1) {
        /* Check if a reschedule was requested by the timer IRQ.
         * Without this, once the scheduler picks idle, no other
         * process would ever get CPU time back (since preemptive
         * scheduling is deferred, not done inside the IRQ). */
        extern void kernel_check_reschedule(void);
        kernel_check_reschedule();
        __asm__ volatile("sti; hlt");
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  find_free_slot
 * ══════════════════════════════════════════════════════════════════════ */
static uint32_t find_free_slot(void) {
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        /* Reap terminated processes (deferred from process_exit) */
        if (process_table[i].state == PROCESS_TERMINATED && process_table[i].pid != 0) {
            if (process_table[i].stack_base) {
                kfree(process_table[i].stack_base);
                process_table[i].stack_base = NULL;
            }
            process_table[i].pid = 0;
            memset(&process_table[i], 0, sizeof(process_t));
            process_count--;
        }
        if (process_table[i].pid == 0) {
            return i;
        }
    }
    return MAX_PROCESSES;
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_init
 * ══════════════════════════════════════════════════════════════════════ */
void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    current_pid         = 0;
    next_schedule_index = 0;
    process_count       = 0;
    scheduler_active    = false;

    uint32_t pid = process_create(idle_process, "idle", DEFAULT_STACK_SIZE);
    if (pid != 1) {
        serial_printf("[PROCESS] FATAL: idle got PID %u\n", pid);
    }

    KINFO("Process subsystem initialized (idle PID 1)");
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_create
 * ══════════════════════════════════════════════════════════════════════ */
uint32_t process_create(void (*entry_point)(void),
                        const char *name,
                        uint32_t stack_size) {
    if (!entry_point) {
        KERROR("process_create: NULL entry point");
        return 0;
    }
    if (process_count >= MAX_PROCESSES) {
        KWARN("process_create: table full (%u/%u)",
              process_count, (uint32_t)MAX_PROCESSES);
        return 0;
    }
    if (stack_size < 1024) {
        stack_size = DEFAULT_STACK_SIZE;
    }

    uint32_t slot = find_free_slot();
    if (slot >= MAX_PROCESSES) {
        KWARN("process_create: no free slot");
        return 0;
    }

    void *stack = kmalloc(stack_size);
    if (!stack) {
        KERROR("process_create: stack alloc failed (%u bytes)", stack_size);
        return 0;
    }

    /* Place canary at the bottom of the stack */
    *(uint32_t *)stack = STACK_CANARY;

    process_t *p = &process_table[slot];
    memset(p, 0, sizeof(process_t));

    p->pid        = slot + 1;
    p->state      = PROCESS_READY;
    p->stack_base = stack;
    p->stack_size = stack_size;

    /* Copy name */
    if (name) {
        uint32_t i = 0;
        while (name[i] && i < PROCESS_NAME_LEN - 1) {
            p->name[i] = name[i];
            i++;
        }
        p->name[i] = '\0';
    }

    /*
     * Set up initial stack for first context switch.
     *
     * When schedule() picks this process for the first time, it calls:
     *   context_switch(&old_esp, new_esp, new_eip)
     *
     * For a BRAND NEW process, new_eip = entry_point.  The entry_point
     * function will eventually return, hitting process_exit_trampoline.
     * We set up the stack so that a `ret` from entry_point goes to
     * process_exit_trampoline.
     */
    uint32_t *sp = (uint32_t *)((uint32_t)stack + stack_size);
    sp--;
    *sp = (uint32_t)process_exit_trampoline;  /* return address for entry_point */

    p->context.esp    = (uint32_t)sp;
    p->context.eip    = (uint32_t)entry_point;

    process_count++;

    serial_printf("[PROCESS] Created PID %u \"%s\" (stack=%u, entry=0x%x)\n",
                  p->pid, p->name, stack_size, (uint32_t)entry_point);

    return p->pid;
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_exit
 * ══════════════════════════════════════════════════════════════════════ */
void process_exit(void) {
    __asm__ volatile("cli");

    if (current_pid == 0 || current_pid == 1) {
        __asm__ volatile("sti");
        return;
    }

    process_t *p = &process_table[current_pid - 1];
    serial_printf("[PROCESS] PID %u \"%s\" exiting\n", p->pid, p->name);

    /* Mark terminated but DON'T free the stack yet — we're still
     * running on it.  The stack will be freed lazily when the slot
     * is reused by process_create or after schedule switches away. */
    p->state = PROCESS_TERMINATED;
    current_pid = 0;

    /* Reschedule — never returns.
     * schedule() will use dummy_esp path since current_pid == 0. */
    __asm__ volatile("sti");
    schedule();

    /* Should never reach here */
    while (1) { __asm__ volatile("hlt"); }
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_yield
 * ══════════════════════════════════════════════════════════════════════ */
void process_yield(void) {
    if (!scheduler_active || current_pid == 0) return;
    extern void kernel_clear_reschedule(void);
    kernel_clear_reschedule();   /* consume the deferred flag */
    schedule();
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_get_current_pid
 * ══════════════════════════════════════════════════════════════════════ */
uint32_t process_get_current_pid(void) {
    return current_pid;
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_kill
 * ══════════════════════════════════════════════════════════════════════ */
void process_kill(uint32_t pid) {
    if (pid == 0 || pid == 1) {
        KWARN("Cannot kill PID %u", pid);
        return;
    }
    if (pid > MAX_PROCESSES) {
        KWARN("Invalid PID %u", pid);
        return;
    }

    __asm__ volatile("cli");

    process_t *p = &process_table[pid - 1];
    if (p->pid == 0) {
        __asm__ volatile("sti");
        KWARN("PID %u does not exist", pid);
        return;
    }

    serial_printf("[PROCESS] Killing PID %u \"%s\"\n", p->pid, p->name);

    if (pid == current_pid) {
        /* Killing self — mark terminated and reschedule.
         * Stack freed later by reaper in find_free_slot. */
        p->state = PROCESS_TERMINATED;
        current_pid = 0;
        __asm__ volatile("sti");
        schedule();
    } else {
        /* Killing another process — safe to free stack now */
        p->state = PROCESS_TERMINATED;
        if (p->stack_base) {
            kfree(p->stack_base);
            p->stack_base = NULL;
        }
        p->pid = 0;
        memset(p, 0, sizeof(process_t));
        process_count--;
        __asm__ volatile("sti");
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_get_state — return state of a process by PID
 * ══════════════════════════════════════════════════════════════════════ */
int process_get_state(uint32_t pid) {
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && pid != 0) {
            return (int)process_table[i].state;
        }
    }
    return -1; /* Not found */
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_list — `ps` shell command
 * ══════════════════════════════════════════════════════════════════════ */
void process_list(void) {
    print("PID  STATE      NAME\n");
    print("---  ---------  ----------------\n");
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &process_table[i];
        if (p->pid == 0) continue;

        /* Reap terminated processes so they don't linger in ps output */
        if (p->state == PROCESS_TERMINATED) {
            if (p->stack_base) {
                kfree(p->stack_base);
                p->stack_base = NULL;
            }
            p->pid = 0;
            memset(p, 0, sizeof(process_t));
            process_count--;
            continue;
        }

        if (p->pid < 10) print(" ");
        print_int(p->pid);
        print("   ");

        const char *sname = "UNKNOWN";
        if (p->state <= PROCESS_TERMINATED) {
            sname = state_names[p->state];
        }
        print(sname);

        uint32_t slen = 0;
        while (sname[slen]) slen++;
        for (uint32_t pad = slen; pad < 11; pad++) print(" ");

        print(p->name);
        print("\n");
    }
    print("Total: ");
    print_int(process_count);
    print(" process(es)\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  schedule — Round-robin context switch
 *
 *  1. Find the next READY process (round-robin, fallback to idle)
 *  2. If different from current, call the assembly context_switch()
 *     which saves all callee-saved regs + ESP, then switches stack
 *     and jumps to the new process's saved EIP.
 *
 *  When a previously-saved process is rescheduled, context_switch()
 *  jumps to context_switch_resume which pops the saved regs and
 *  returns normally — so schedule() returns to its caller as if
 *  nothing happened.
 * ══════════════════════════════════════════════════════════════════════ */
void schedule(void) {
    if (process_count == 0 || !scheduler_active) return;

    __asm__ volatile("cli");

    /* Mark current process as READY (if it's still running) */
    if (current_pid != 0 && current_pid <= MAX_PROCESSES) {
        process_t *current = &process_table[current_pid - 1];
        if (current->pid != 0 && current->state == PROCESS_RUNNING) {
            current->state = PROCESS_READY;
        }

        /* Stack canary check */
        if (current->stack_base &&
            *(uint32_t *)current->stack_base != STACK_CANARY) {
            serial_printf("[PROCESS] Stack overflow PID %u \"%s\"\n",
                          current->pid, current->name);
            current->state = PROCESS_TERMINATED;
            kfree(current->stack_base);
            current->stack_base = NULL;
            current->pid = 0;
            memset(current, 0, sizeof(process_t));
            process_count--;
            current_pid = 0;
        }
    }

    /* ── Find next READY process (round-robin) ────────────────────── */
    process_t *next = NULL;
    uint32_t searches = 0;

    while (searches < MAX_PROCESSES) {
        next_schedule_index = (next_schedule_index + 1) % MAX_PROCESSES;
        process_t *candidate = &process_table[next_schedule_index];

        if (candidate->pid != 0 && candidate->state == PROCESS_READY) {
            next = candidate;
            break;
        }
        searches++;
    }

    /* Fall back to idle (index 0, PID 1) */
    if (!next) {
        next = &process_table[0];
        if (next->pid == 0) {
            /* No processes at all — just re-enable interrupts */
            if (current_pid != 0 && current_pid <= MAX_PROCESSES) {
                process_table[current_pid - 1].state = PROCESS_RUNNING;
            }
            __asm__ volatile("sti");
            return;
        }
    }

    /* Same process — just mark running and return */
    if (next->pid == current_pid && current_pid != 0) {
        next->state = PROCESS_RUNNING;
        __asm__ volatile("sti");
        return;
    }

    /* ── Perform context switch ───────────────────────────────────── */
    uint32_t old_pid = current_pid;
    current_pid = next->pid;
    next->state = PROCESS_RUNNING;

    uint32_t new_esp = next->context.esp;
    uint32_t new_eip = next->context.eip;

    /* For a previously-saved process, new_eip = context_switch_resume
     * (set by context_switch() when it saved the process).
     * For a brand-new process, new_eip = entry_point function.
     *
     * context_switch() saves callee-saved regs + ESP, then does:
     *   mov esp, new_esp
     *   jmp new_eip
     *
     * It never returns to us — when THIS process is later resumed,
     * context_switch_resume pops the saved regs and does `ret`,
     * returning us right here. */

    if (old_pid != 0 && old_pid <= MAX_PROCESSES) {
        process_t *old = &process_table[old_pid - 1];
        /* context_switch saves ESP into old->context.esp,
         * and sets the resume EIP to context_switch_resume */
        old->context.eip = (uint32_t)context_switch_resume;
        __asm__ volatile("sti");
        context_switch(&old->context.esp, new_esp, new_eip);
        /* We resume here when rescheduled */
    } else {
        /* No valid old process — just switch (e.g. during exit) */
        uint32_t dummy_esp;
        __asm__ volatile("sti");
        context_switch(&dummy_esp, new_esp, new_eip);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Utility queries
 * ══════════════════════════════════════════════════════════════════════ */
bool process_is_active(void) {
    return scheduler_active;
}

uint32_t process_get_count(void) {
    return process_count;
}

void process_start_scheduler(void) {
    scheduler_active = true;
    KINFO("Scheduler started (%u processes)", process_count);
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_register_current — Register the already-running thread
 *
 *  Used to add the kernel main thread (desktop loop) to the process
 *  table so the scheduler can properly save and restore its context.
 *  Does NOT allocate a stack — the kernel stack is managed externally.
 * ══════════════════════════════════════════════════════════════════════ */
uint32_t process_register_current(const char *name) {
    if (process_count >= MAX_PROCESSES) {
        KWARN("process_register_current: table full");
        return 0;
    }

    uint32_t slot = find_free_slot();
    if (slot >= MAX_PROCESSES) {
        KWARN("process_register_current: no free slot");
        return 0;
    }

    process_t *p = &process_table[slot];
    memset(p, 0, sizeof(process_t));

    p->pid        = slot + 1;
    p->state      = PROCESS_RUNNING;
    p->stack_base = NULL;           /* We don't own this stack */
    p->stack_size = 0;

    if (name) {
        uint32_t i = 0;
        while (name[i] && i < PROCESS_NAME_LEN - 1) {
            p->name[i] = name[i];
            i++;
        }
        p->name[i] = '\0';
    }

    process_count++;
    current_pid = p->pid;

    serial_printf("[PROCESS] Registered current thread as PID %u \"%s\"\n",
                  p->pid, p->name);

    return p->pid;
}
