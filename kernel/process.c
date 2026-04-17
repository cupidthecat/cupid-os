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
#include "shell.h"
#include "gui.h"
#include "simd.h"
#include "percpu.h"
#include "bkl.h"
#include "../drivers/serial.h"

extern void context_switch(process_t *old_proc, process_t *new_proc);
extern void context_switch_resume(void);  /* resume label address */

_Static_assert(__alignof__(((process_t *)0)->fp_state) >= 16,
               "fp_state must be 16-byte aligned for FXSAVE/FXRSTOR");
_Static_assert(sizeof(((process_t *)0)->fp_state) == 512,
               "fp_state must be 512 bytes");

/* PCB field offsets baked into kernel/context_switch.asm.  If any of
 * these asserts fire, update the matching %define in that file. */
_Static_assert(__builtin_offsetof(process_t, context) +
               __builtin_offsetof(cpu_context_t, esp) == 32,
               "PCB ESP offset baked into context_switch.asm "
               "(PCB_ESP_OFFSET=32)");
_Static_assert(__builtin_offsetof(process_t, context) +
               __builtin_offsetof(cpu_context_t, eip) == 40,
               "PCB EIP offset baked into context_switch.asm "
               "(PCB_EIP_OFFSET=40)");
_Static_assert(__builtin_offsetof(process_t, fp_state) == 80,
               "PCB fp_state offset baked into context_switch.asm "
               "(PCB_FP_STATE_OFFSET=80)");

static process_t  process_table[MAX_PROCESSES];
static uint32_t   next_schedule_index  = 0;
static uint32_t   process_count        = 0;
static bool       scheduler_active     = false;

static const char *state_names[] = {
    "READY",
    "RUNNING",
    "BLOCKED",
    "TERMINATED"
};

static const char *domain_names[] = {
    "kernel",
    "hosted",
    "external"
};

static void idle_process(void);
static uint32_t find_free_slot(void);
static void process_reap_terminated(void);
static uint32_t process_create_common_locked(void (*entry_point)(void),
                                             const char *name,
                                             uint32_t stack_size,
                                             bool with_arg,
                                             uint32_t arg,
                                             process_domain_t domain);
static uint32_t process_create_common(void (*entry_point)(void),
                                      const char *name,
                                      uint32_t stack_size,
                                      bool with_arg,
                                      uint32_t arg,
                                      process_domain_t domain);

/* ══════════════════════════════════════════════════════════════════════
 *  Trampoline: if a process entry function returns, we land here
 *  and call process_exit() to clean up.
 * ══════════════════════════════════════════════════════════════════════ */
static void process_exit_trampoline(void) {
    process_exit();
    while (1) { __asm__ volatile("hlt"); }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Idle process - PID 1, always present
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
        if (process_table[i].pid == 0) {
            return i;
        }
    }
    return MAX_PROCESSES;
}

static void process_reap_terminated(void) {
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROCESS_TERMINATED ||
            process_table[i].pid == 0) {
            continue;
        }

        uint32_t dead_pid = process_table[i].pid;
        shell_jit_discard_by_owner(dead_pid);
        gui_destroy_windows_by_owner(dead_pid);

        if (process_table[i].stack_base) {
            kfree(process_table[i].stack_base);
            process_table[i].stack_base = NULL;
        }
        if (process_table[i].image_size > 0) {
            pmm_release_region(process_table[i].image_base,
                               process_table[i].image_size);
            process_table[i].image_base = 0;
            process_table[i].image_size = 0;
        }

        process_table[i].pid = 0;
        memset(&process_table[i], 0, sizeof(process_t));
        if (process_count > 0) {
            process_count--;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_init
 * ══════════════════════════════════════════════════════════════════════ */
void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    this_cpu()->current_pid = 0;
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
static uint32_t process_create_common_locked(void (*entry_point)(void),
                                             const char *name,
                                             uint32_t stack_size,
                                             bool with_arg,
                                             uint32_t arg,
                                             process_domain_t domain) {
    process_reap_terminated();

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

    p->on_cpu     = 0xFFu;   /* not running on any CPU yet */
    p->last_cpu   = 0u;
    p->pid        = slot + 1;
    p->state      = PROCESS_READY;
    p->stack_base = stack;
    p->stack_size = stack_size;
    if (domain > PROCESS_DOMAIN_EXTERNAL) {
        domain = PROCESS_DOMAIN_KERNEL;
    }
    p->domain     = domain;

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
     *   context_switch(old_proc, new_proc)
     * which loads ESP from new_proc->context.esp and jumps to
     * new_proc->context.eip.  For a brand-new process we set
     * context.eip = entry_point and pre-place process_exit_trampoline
     * as a fake return address on its stack, so when entry_point
     * returns, `ret` lands in the trampoline.
     */
    uint32_t top = ((uint32_t)stack + stack_size) & ~0xFu;
    uint32_t *sp = (uint32_t *)top;
    if (with_arg) {
        sp--;
        *sp = arg;
    }
    sp--;
    *sp = (uint32_t)process_exit_trampoline;

    p->context.esp    = (uint32_t)sp;
    p->context.eip    = (uint32_t)entry_point;

    /* Seed fp_state with a fresh FXSAVE from the currently-init'd FPU
     * so the first FXRSTOR into this process loads a valid image.  The
     * PCB was just memset to zero above; a zeroed 512-byte region is
     * technically an acceptable FXRSTOR source (MXCSR reserved bits =
     * 0), but grabbing a real snapshot is free and cleaner. */
    __asm__ volatile("fxsave (%0)" : : "r"(p->fp_state) : "memory");

    process_count++;

    if (with_arg) {
        serial_printf("[PROCESS] Created PID %u \"%s\" domain=%s arg=0x%x "
                      "(stack=%u, entry=0x%x)\n",
                      p->pid, p->name, process_domain_name(p->domain), arg,
                      stack_size, (uint32_t)entry_point);
    } else {
        serial_printf("[PROCESS] Created PID %u \"%s\" domain=%s "
                      "(stack=%u, entry=0x%x)\n",
                      p->pid, p->name, process_domain_name(p->domain),
                      stack_size, (uint32_t)entry_point);
    }

    return p->pid;
}

static uint32_t process_create_common(void (*entry_point)(void),
                                      const char *name,
                                      uint32_t stack_size,
                                      bool with_arg,
                                      uint32_t arg,
                                      process_domain_t domain) {
    bkl_lock();
    uint32_t pid = process_create_common_locked(entry_point, name, stack_size,
                                                with_arg, arg, domain);
    bkl_unlock();
    return pid;
}

uint32_t process_create(void (*entry_point)(void),
                        const char *name,
                        uint32_t stack_size) {
    return process_create_common(entry_point, name, stack_size, false, 0,
                                 PROCESS_DOMAIN_KERNEL);
}

uint32_t process_create_ex(void (*entry_point)(void),
                           const char *name,
                           uint32_t stack_size,
                           process_domain_t domain) {
    return process_create_common(entry_point, name, stack_size, false, 0,
                                 domain);
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_create_with_arg - like process_create but pushes one
 *  uint32_t argument onto the stack so the entry function receives it.
 * ══════════════════════════════════════════════════════════════════════ */
uint32_t process_create_with_arg(void (*entry_point)(void),
                                 const char *name,
                                 uint32_t stack_size,
                                 uint32_t arg) {
    return process_create_common(entry_point, name, stack_size, true, arg,
                                 PROCESS_DOMAIN_KERNEL);
}

uint32_t process_create_with_arg_ex(void (*entry_point)(void),
                                    const char *name,
                                    uint32_t stack_size,
                                    uint32_t arg,
                                    process_domain_t domain) {
    return process_create_common(entry_point, name, stack_size, true, arg,
                                 domain);
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_exit
 * ══════════════════════════════════════════════════════════════════════ */
void process_exit(void) {
    bkl_lock();

    uint32_t exit_pid = this_cpu()->current_pid;
    if (exit_pid == 0) {
        for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
            if (process_table[i].pid != 0 &&
                process_table[i].state == PROCESS_RUNNING) {
                exit_pid = process_table[i].pid;
                break;
            }
        }
    }

    if (exit_pid == 0 || exit_pid == 1) {
        bkl_unlock();
        return;
    }

    process_t *p = &process_table[exit_pid - 1];
    serial_printf("[PROCESS] PID %u \"%s\" exiting\n", p->pid, p->name);

    /* Mark terminated but DON'T free the stack yet - we're still
     * running on it.  The stack will be freed lazily when the slot
     * is reused by process_create or after schedule switches away. */
    p->state        = PROCESS_TERMINATED;
    p->on_cpu       = 0xFFu;
    this_cpu()->current_pid = 0;

    /* Reschedule - never returns.
     * schedule() will use dummy_esp path since current_pid == 0.
     * Release BKL before schedule() so schedule() can re-acquire it. */
    bkl_unlock();
    schedule();
    serial_printf("[EXIT] BUG: schedule returned in process_exit\n");

    /* Should never reach here */
    while (1) { __asm__ volatile("hlt"); }
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_yield
 * ══════════════════════════════════════════════════════════════════════ */
void process_yield(void) {
    if (!scheduler_active || this_cpu()->current_pid == 0) return;
    extern void kernel_clear_reschedule(void);
    kernel_clear_reschedule();   /* consume the deferred flag */
    schedule();
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_get_current_pid
 * ══════════════════════════════════════════════════════════════════════ */
uint32_t process_get_current_pid(void) {
    return this_cpu()->current_pid;
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_kill
 * ══════════════════════════════════════════════════════════════════════ */
void process_kill(uint32_t pid) {
    if (pid == 0 || pid == 1) {
        KWARN("Cannot kill PID %u", pid);
        return;
    }

    /* Virtual PIDs 99,98,97... = JIT programs (99 = oldest, descending) */
    if (pid >= 96 && pid <= 99) {
        int jit_count = shell_jit_suspended_count();
        int jit_index = (int)(99 - pid); /* PID 99=index 0, 98=index 1, etc */
        if (jit_index < jit_count) {
            const char *name = shell_jit_suspended_get_name(jit_index);
            serial_printf("[PROCESS] Killing JIT program \"%s\"\n", name);
            print("Killing JIT program: ");
            print(name);
            print("\n");
            shell_jit_program_kill_at(jit_index);
        } else {
            print("No JIT program at PID ");
            print_int(pid);
            print("\n");
        }
        return;
    }

    if (pid > MAX_PROCESSES) {
        KWARN("Invalid PID %u", pid);
        return;
    }

    bkl_lock();

    process_t *p = &process_table[pid - 1];
    if (p->pid == 0) {
        bkl_unlock();
        KWARN("PID %u does not exist", pid);
        return;
    }

    serial_printf("[PROCESS] Killing PID %u \"%s\"\n", p->pid, p->name);

    if (pid == this_cpu()->current_pid) {
        /* Killing self - mark terminated and reschedule.
         * Stack freed later by reaper in find_free_slot. */
        p->state  = PROCESS_TERMINATED;
        p->on_cpu = 0xFFu;
        this_cpu()->current_pid = 0;
        bkl_unlock();
        schedule();
    } else {
        /* Killing another process - safe to free stack now */
        p->state  = PROCESS_TERMINATED;
        p->on_cpu = 0xFFu;
        shell_jit_discard_by_owner(pid);
        gui_destroy_windows_by_owner(pid);
        if (p->stack_base) {
            kfree(p->stack_base);
            p->stack_base = NULL;
        }
        if (p->image_size > 0) {
            pmm_release_region(p->image_base, p->image_size);
            p->image_base = 0;
            p->image_size = 0;
        }
        p->pid = 0;
        memset(p, 0, sizeof(process_t));
        process_count--;
        bkl_unlock();
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_get_state - return state of a process by PID
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
 *  process_list - `ps` shell command
 * ══════════════════════════════════════════════════════════════════════ */
void process_list(void) {
    process_reap_terminated();

    print("PID  STATE      DOMAIN    NAME\n");
    print("---  ---------  --------  ----------------\n");
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &process_table[i];
        if (p->pid == 0) continue;

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

        {
            const char *dname = process_domain_name(p->domain);
            uint32_t dlen = 0;
            print(dname);
            while (dname[dlen]) dlen++;
            for (uint32_t pad = dlen; pad < 10; pad++) print(" ");
        }

        print(p->name);
        print("\n");
    }
    print("Total: ");
    print_int(process_count);
    print(" process(es)");

    /* Show suspended (minimized) JIT programs */
    int jit_count = shell_jit_suspended_count();
    if (jit_count > 0) {
        print(", ");
        print_int((uint32_t)jit_count);
        print(" JIT program(s)");
    }
    print("\n");

    for (int j = 0; j < jit_count; j++) {
        uint32_t vpid = 99 - (uint32_t)j;
        print_int(vpid);
        print("   SUSPENDED  ");
        print(shell_jit_suspended_get_name(j));
        print(" (JIT)\n");
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_list_adam - TempleOS-style task tree
 *
 *  Adam is the root task. Every process is a descendant. Group by
 *  domain (kernel/hosted/external). No parent tracking, so the tree
 *  is two levels deep: Adam > domain > process.
 * ══════════════════════════════════════════════════════════════════════ */
void process_list_adam(void) {
    process_reap_terminated();

    print("+ Adam (pid 1, cupid-os ring 0)\n");

    static const process_domain_t domains[] = {
        PROCESS_DOMAIN_KERNEL,
        PROCESS_DOMAIN_HOSTED,
        PROCESS_DOMAIN_EXTERNAL
    };

    for (int d = 0; d < 3; d++) {
        process_domain_t dom = domains[d];
        const char *dname = process_domain_name(dom);

        uint32_t count = 0;
        for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
            process_t *p = &process_table[i];
            if (p->pid == 0 || p->pid == 1) continue;
            if (p->domain == dom) count++;
        }
        if (count == 0) continue;

        print("|-- ");
        print(dname);
        print(" (");
        print_int(count);
        print(")\n");

        for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
            process_t *p = &process_table[i];
            if (p->pid == 0 || p->pid == 1) continue;
            if (p->domain != dom) continue;

            print("|   `-- [");
            print_int(p->pid);
            print("] ");
            print(p->name);

            const char *sname = "?";
            if (p->state <= PROCESS_TERMINATED) {
                sname = state_names[p->state];
            }
            print(" <");
            print(sname);
            print(">\n");
        }
    }

    int jit_count = shell_jit_suspended_count();
    if (jit_count > 0) {
        print("|-- suspended JIT (");
        print_int((uint32_t)jit_count);
        print(")\n");
        for (int j = 0; j < jit_count; j++) {
            print("|   `-- ");
            print(shell_jit_suspended_get_name(j));
            print("\n");
        }
    }

    print("`-- total: ");
    print_int(process_count);
    print(" task(s) under Adam\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  schedule - Round-robin context switch
 *
 *  1. Find the next READY process (round-robin, fallback to idle)
 *  2. If different from current, call the assembly context_switch()
 *     which saves all callee-saved regs + ESP, then switches stack
 *     and jumps to the new process's saved EIP.
 *
 *  When a previously-saved process is rescheduled, context_switch()
 *  jumps to context_switch_resume which pops the saved regs and
 *  returns normally - so schedule() returns to its caller as if
 *  nothing happened.
 * ══════════════════════════════════════════════════════════════════════ */
/* schedule_locked - called with BKL held (or from context before BKL exists).
 * Does the actual round-robin pick and context switch. */
static void schedule_locked(void) {
    if (process_count == 0 || !scheduler_active) return;

    uint32_t cpu_id = this_cpu()->cpu_id;

    /* Mark current process as READY (if it's still running) */
    uint32_t cur_pid = this_cpu()->current_pid;
    if (cur_pid != 0 && cur_pid <= MAX_PROCESSES) {
        process_t *current = &process_table[cur_pid - 1];
        if (current->pid != 0 && current->state == PROCESS_RUNNING) {
            current->state = PROCESS_READY;
            current->on_cpu = 0xFFu;
        }

        /* Stack canary check */
        if (current->stack_base &&
            *(uint32_t *)current->stack_base != STACK_CANARY) {
            serial_printf("[PROCESS] Stack overflow PID %u \"%s\"\n",
                          current->pid, current->name);
            current->state = PROCESS_TERMINATED;
            current->on_cpu = 0xFFu;
            kfree(current->stack_base);
            current->stack_base = NULL;
            current->pid = 0;
            memset(current, 0, sizeof(process_t));
            process_count--;
            this_cpu()->current_pid = 0;
            cur_pid = 0;
        }
    }

    /* ── Find next READY process (round-robin) ────────────────────── */
    process_t *next = NULL;
    uint32_t searches = 0;

    while (searches < MAX_PROCESSES) {
        next_schedule_index = (next_schedule_index + 1) % MAX_PROCESSES;
        process_t *candidate = &process_table[next_schedule_index];

        /* Skip the current process - we want a DIFFERENT one */
        if (candidate->pid != 0 &&
            candidate->pid != cur_pid &&
            candidate->state == PROCESS_READY) {
            next = candidate;
            break;
        }
        searches++;
    }

    /* Fall back to idle (index 0, PID 1) */
    if (!next) {
        next = &process_table[0];
        if (next->pid == 0) {
            /* No processes at all - just restore current as running */
            if (cur_pid != 0 && cur_pid <= MAX_PROCESSES) {
                process_table[cur_pid - 1].state = PROCESS_RUNNING;
            }
            return;
        }
    }

    /* Same process - just mark running and return */
    if (next->pid == cur_pid && cur_pid != 0) {
        next->state = PROCESS_RUNNING;
        next->on_cpu = (uint8_t)cpu_id;
        return;
    }

    /* ── Perform context switch ───────────────────────────────────── */
    uint32_t old_pid = cur_pid;
    this_cpu()->current_pid = next->pid;
    next->state    = PROCESS_RUNNING;
    next->last_cpu = (uint8_t)cpu_id;
    next->on_cpu   = (uint8_t)cpu_id;

    /* context_switch(old_proc, new_proc):
     *   - pushes callee-saved regs on current stack
     *   - fxsaves SSE/x87 state into old_proc->fp_state
     *   - stores current ESP into old_proc->context.esp
     *   - loads ESP from new_proc->context.esp
     *   - fxrstors new_proc->fp_state
     *   - jumps to new_proc->context.eip
     *
     * For a previously-saved process, new_proc->context.eip =
     * context_switch_resume (we seed it below before switching).
     * For a brand-new process, new_proc->context.eip = entry_point;
     * its stack already has process_exit_trampoline as the return
     * address (set up by process_create_common_locked).
     *
     * context_switch never returns normally - when THIS process is
     * later resumed, context_switch_resume pops the saved regs and
     * does `ret`, returning us right here. */

    if (old_pid != 0 && old_pid <= MAX_PROCESSES) {
        process_t *old = &process_table[old_pid - 1];
        /* Seed the old PCB's resume EIP so its next FXRSTOR+jmp
         * lands in context_switch_resume. */
        old->context.eip = (uint32_t)context_switch_resume;
        __asm__ volatile("sti");
        context_switch(old, next);
        /* We resume here when rescheduled.
         * Re-disable interrupts: BKL's unlock will restore IF. */
        __asm__ volatile("cli");
    } else {
        /* No valid old process (e.g. entering after process_exit).
         * Use a throw-away PCB scratch area so context_switch has
         * somewhere to stash its garbage save.  We never read any
         * of its fields again.  Must be 16-byte aligned (fp_state
         * constraint inherited from the containing struct). */
        static process_t dummy __attribute__((aligned(16)));
        __asm__ volatile("sti");
        context_switch(&dummy, next);
        /* We resume here when rescheduled (dummy path). */
        __asm__ volatile("cli");
    }
}

void schedule(void) {
    bkl_lock();
    schedule_locked();
    bkl_unlock();
}

/* ══════════════════════════════════════════════════════════════════════
 *  Utility queries
 * ══════════════════════════════════════════════════════════════════════ */
bool process_is_active(void) {
    return scheduler_active;
}

uint32_t process_get_count(void) {
    process_reap_terminated();
    {
        uint32_t live_count = 0;
        for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
            if (process_table[i].pid != 0 &&
                process_table[i].state != PROCESS_TERMINATED) {
                live_count++;
            }
        }
        process_count = live_count;
        return live_count;
    }
}

void process_start_scheduler(void) {
    scheduler_active = true;
    KINFO("Scheduler started (%u processes)", process_count);
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_register_current - Register the already-running thread
 *
 *  Used to add the kernel main thread (desktop loop) to the process
 *  table so the scheduler can properly save and restore its context.
 *  Does NOT allocate a stack - the kernel stack is managed externally.
 * ══════════════════════════════════════════════════════════════════════ */
uint32_t process_register_current(const char *name) {
    process_reap_terminated();

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
    p->domain     = PROCESS_DOMAIN_KERNEL;

    if (name) {
        uint32_t i = 0;
        while (name[i] && i < PROCESS_NAME_LEN - 1) {
            p->name[i] = name[i];
            i++;
        }
        p->name[i] = '\0';
    }

    p->on_cpu     = (uint8_t)this_cpu()->cpu_id;
    p->last_cpu   = (uint8_t)this_cpu()->cpu_id;
    process_count++;
    this_cpu()->current_pid = p->pid;

    /* Seed fp_state with current FPU snapshot so the first context
     * switch away from this thread has a valid image to restore. */
    __asm__ volatile("fxsave (%0)" : : "r"(p->fp_state) : "memory");

    serial_printf("[PROCESS] Registered current thread as PID %u \"%s\"\n",
                  p->pid, p->name);

    return p->pid;
}

const char *process_domain_name(process_domain_t domain) {
    if (domain > PROCESS_DOMAIN_EXTERNAL) {
        return domain_names[PROCESS_DOMAIN_KERNEL];
    }
    return domain_names[domain];
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_block / process_unblock - Suspend/resume a READY process
 *
 *  Used by the JIT region manager (shell.c) to prevent a process from
 *  being scheduled while the shared JIT code region contains code that
 *  belongs to a different process.
 * ══════════════════════════════════════════════════════════════════════ */
void process_block(uint32_t pid) {
    if (pid == 0 || pid == 1 || pid > MAX_PROCESSES) return;
    bkl_lock();
    process_t *p = &process_table[pid - 1];
    if (p->pid == pid && p->state == PROCESS_READY) {
        p->state = PROCESS_BLOCKED;
    }
    bkl_unlock();
}

void process_unblock(uint32_t pid) {
    if (pid == 0 || pid > MAX_PROCESSES) return;
    bkl_lock();
    process_t *p = &process_table[pid - 1];
    if (p->pid == pid && p->state == PROCESS_BLOCKED) {
        p->state = PROCESS_READY;
    }
    bkl_unlock();
}

/* ══════════════════════════════════════════════════════════════════════
 *  process_set_image - Associate an ELF image region with a process
 * ══════════════════════════════════════════════════════════════════════ */
void process_set_image(uint32_t pid, uint32_t base, uint32_t size) {
    if (pid == 0 || pid > MAX_PROCESSES) return;
    process_t *p = &process_table[pid - 1];
    if (p->pid != pid) return;
    p->image_base = base;
    p->image_size = size;
}
