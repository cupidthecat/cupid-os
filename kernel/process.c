#include "process.h"
#include "scheduler.h"
#include "memory.h"
#include "string.h"
#include "pic.h"

// External functions from kernel.c
extern void print(const char* str);
extern void print_int(int num);
extern void print_hex(uint32_t num);

// Global process state
pcb_t process_table[MAX_PROCESSES];
pcb_t* current_process = NULL;
uint32_t next_pid = 1;

// Helper to copy string with length limit
static void str_copy(char* dest, const char* src, size_t max) {
    size_t i;
    for (i = 0; i < max - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// Initialize the process system
void process_init(void) {
    // Clear process table
    memset(process_table, 0, sizeof(process_table));

    // Mark all slots as free
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].pid = 0;
        process_table[i].state = PROCESS_STATE_FREE;
    }

    current_process = NULL;
    next_pid = 1;
}

// Find a free slot in the process table
static pcb_t* find_free_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_STATE_FREE) {
            return &process_table[i];
        }
    }
    return NULL;
}

// Process entry wrapper - calls entry point then exits
// This is pushed onto the stack so returning from the entry point calls this
static void process_entry_wrapper(void) {
    // This should never be reached if the process calls process_exit()
    // But if it returns from its entry point, we exit cleanly
    process_exit(0);
}

// Create a new process
int32_t process_create(const char* name, void (*entry_point)(void), uint8_t priority) {
    if (priority >= NUM_PRIORITIES) priority = DEFAULT_PRIORITY;

    pcb_t* proc = find_free_slot();
    if (!proc) {
        print("process_create: no free slots\n");
        return -1;
    }

    void* stack = pmm_alloc_contiguous(PROCESS_STACK_PAGES);
    if (!stack) {
        print("process_create: failed to allocate stack\n");
        return -1;
    }

    proc->pid = next_pid++;
    str_copy(proc->name, name, PROCESS_NAME_MAX);

    proc->priority = priority;
    proc->quantum_total = priority_quantum[priority];
    proc->quantum_remaining = proc->quantum_total;
    proc->state = PROCESS_STATE_READY;

    proc->stack_base = (uint32_t)stack;
    proc->stack_size = PROCESS_STACK_SIZE;
    proc->stack_pages = PROCESS_STACK_PAGES;

    proc->ticks_used = 0;
    proc->parent_pid = current_process ? current_process->pid : 0;
    proc->exit_code = 0;

    // Build initial stack:
    // We want after iret to land in entry_point, with a return address to process_entry_wrapper.
    uint32_t* sp = (uint32_t*)((uint32_t)stack + PROCESS_STACK_SIZE);

    // Return address for entry_point -> if entry returns, we exit cleanly
    sp--;
    *sp = (uint32_t)process_entry_wrapper;

    // This is the "normal" ESP the process will run with after iret
    uint32_t run_esp = (uint32_t)sp;

    // Now build a fake IRQ frame BELOW run_esp that matches your restore sequence:
    // Stack (top->bottom for pops): gs, fs, es, ds, popa regs..., int_no, err_code, eip, cs, eflags

    sp = (uint32_t*)run_esp;

    // iret frame
    sp--; *sp = 0x202;          // eflags (IF=1)
    sp--; *sp = 0x08;           // cs
    sp--; *sp = (uint32_t)entry_point; // eip

    // irq "metadata"
    sp--; *sp = 0;              // err_code
    sp--; *sp = 32;             // int_no (IRQ0-style)

    // popa frame (order matches popa: edi,esi,ebp,esp,ebx,edx,ecx,eax popped in reverse push order)
    sp--; *sp = 0;              // eax
    sp--; *sp = 0;              // ecx
    sp--; *sp = 0;              // edx
    sp--; *sp = 0;              // ebx
    sp--; *sp = 0;              // esp placeholder (ignored by popa)
    sp--; *sp = 0;              // ebp
    sp--; *sp = 0;              // esi
    sp--; *sp = 0;              // edi

    // segment regs
    sp--; *sp = 0x10;           // ds
    sp--; *sp = 0x10;           // es
    sp--; *sp = 0x10;           // fs
    sp--; *sp = 0x10;           // gs

    // This is what process_switch_context will restore from
    proc->esp = (uint32_t)sp;

    scheduler_add(proc);
    return proc->pid;
}

// Special function to make the current kernel execution context PID 1
int32_t process_create_kernel(void) {
    pcb_t* proc = &process_table[0];

    proc->pid = next_pid++;  // Should be 1
    str_copy(proc->name, "kernel", PROCESS_NAME_MAX);

    // CPU state will be filled when we switch away
    proc->eax = 0;
    proc->ebx = 0;
    proc->ecx = 0;
    proc->edx = 0;
    proc->esi = 0;
    proc->edi = 0;
    proc->ebp = 0;
    proc->esp = 0;  // Will be set on first switch
    proc->eip = 0;
    proc->eflags = 0x202;

    // Scheduling
    proc->priority = DEFAULT_PRIORITY;
    proc->quantum_total = priority_quantum[DEFAULT_PRIORITY];
    proc->quantum_remaining = proc->quantum_total;
    proc->state = PROCESS_STATE_RUNNING;  // Already running

    // Kernel uses existing stack at 0x90000
    proc->stack_base = 0x90000 - 0x10000;  // 64KB below stack top
    proc->stack_size = 0x10000;  // 64KB

    // Bookkeeping
    proc->ticks_used = 0;
    proc->parent_pid = 0;  // No parent
    proc->exit_code = 0;

    // Set as current process
    current_process = proc;

    // Don't add to scheduler - it's already running

    return proc->pid;
}

// Current process exits
void process_exit(int exit_code) {
    __asm__ volatile("cli");   // Critical: don't take IRQs on a "freed" stack

    if (!current_process) {
        return;
    }

    // Can't exit kernel process
    if (current_process->pid == 1) {
        print("process_exit: cannot exit kernel\n");
        return;
    }

    current_process->exit_code = exit_code;
    current_process->state = PROCESS_STATE_TERMINATED;

    // Remove from scheduler (should already be out since it's running)
    scheduler_remove(current_process);

    // Free stack memory
    if (current_process->stack_base && current_process->pid != 1) {
        for (uint32_t p = 0; p < current_process->stack_pages; p++) {
            pmm_free_page((void*)(current_process->stack_base + p * PAGE_SIZE));
        }
    }

    // Mark slot as free
    current_process->pid = 0;
    current_process->state = PROCESS_STATE_FREE;

    // Force context switch to next process
    pcb_t* next = scheduler_next();
    if (next) {
        current_process = next;
        next->state = PROCESS_STATE_RUNNING;
        next->quantum_remaining = next->quantum_total;

        // Restore the saved frame exactly as-is (no PIC EOI here - not in IRQ)
        __asm__ volatile(
            "mov %0, %%esp\n"
            "pop %%gs\n"
            "pop %%fs\n"
            "pop %%es\n"
            "pop %%ds\n"
            "popa\n"
            "add $8, %%esp\n"
            "iret\n"
            :
            : "r"(next->esp)
            : "memory"
        );
    }

    // Should never reach here
    while (1) {
        __asm__ volatile("hlt");
    }
}

// Kill another process by PID
int process_kill(uint32_t pid) {
    if (pid == 0 || pid == 1) {
        return -1;  // Can't kill invalid or kernel
    }

    pcb_t* proc = process_get_by_pid(pid);
    if (!proc) {
        return -1;
    }

    // If killing current process, use exit
    if (proc == current_process) {
        process_exit(-1);
        return 0;  // Won't reach here
    }

    // Remove from scheduler
    scheduler_remove(proc);

    // Free stack
    if (proc->stack_base && proc->pid != 1) {
        for (uint32_t p = 0; p < proc->stack_pages; p++) {
            pmm_free_page((void*)(proc->stack_base + p * PAGE_SIZE));
        }
    }

    // Mark as free
    proc->state = PROCESS_STATE_FREE;
    proc->pid = 0;

    return 0;
}

// Voluntarily yield CPU time
void process_yield(void) {
    if (!current_process || !scheduler_is_enabled()) {
        return;
    }

    // Set quantum to 0 to trigger switch on next timer tick
    current_process->quantum_remaining = 0;

    // Could also trigger software interrupt here for immediate switch
    // For now, just wait for next timer tick
}

// Set process priority
int process_set_priority(uint32_t pid, uint8_t priority) {
    if (priority >= NUM_PRIORITIES) {
        return -1;
    }

    pcb_t* proc = process_get_by_pid(pid);
    if (!proc) {
        return -1;
    }

    // If in ready queue, need to move to new priority queue
    if (proc->state == PROCESS_STATE_READY) {
        scheduler_remove(proc);
        proc->priority = priority;
        proc->quantum_total = priority_quantum[priority];
        scheduler_add(proc);
    } else {
        proc->priority = priority;
        proc->quantum_total = priority_quantum[priority];
    }

    return 0;
}

// Get process state
process_state_t process_get_state(uint32_t pid) {
    pcb_t* proc = process_get_by_pid(pid);
    if (!proc) {
        return PROCESS_STATE_FREE;
    }
    return (process_state_t)proc->state;
}

// Get current process
pcb_t* process_get_current(void) {
    return current_process;
}

// Get process by PID
pcb_t* process_get_by_pid(uint32_t pid) {
    if (pid == 0) {
        return NULL;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid &&
            process_table[i].state != PROCESS_STATE_FREE) {
            return &process_table[i];
        }
    }
    return NULL;
}

// Block current process
void process_block(void) {
    if (!current_process) {
        return;
    }

    current_process->state = PROCESS_STATE_BLOCKED;
    // Don't add to scheduler - blocked processes aren't ready

    // Yield to trigger switch
    process_yield();
}

// Unblock a process
void process_unblock(uint32_t pid) {
    pcb_t* proc = process_get_by_pid(pid);
    if (!proc || proc->state != PROCESS_STATE_BLOCKED) {
        return;
    }

    proc->state = PROCESS_STATE_READY;
    scheduler_add(proc);
}

// Context switch - called from timer IRQ when quantum expires
void process_switch_context(struct registers* regs) {
    if (!scheduler_is_enabled() || !current_process) {
        return;
    }

    // Save the *actual* interrupt frame pointer (top of frame: gs on most stubs)
    current_process->esp = (uint32_t)regs;

    // Move current back to ready queue if still runnable
    if (current_process->state == PROCESS_STATE_RUNNING) {
        current_process->state = PROCESS_STATE_READY;
        scheduler_add(current_process);
    }

    pcb_t* next = scheduler_next();
    if (!next) {
        // Shouldn't happen if PID 1 exists, but be safe
        current_process->state = PROCESS_STATE_RUNNING;
        return;
    }

    current_process = next;
    next->state = PROCESS_STATE_RUNNING;
    next->quantum_remaining = next->quantum_total;

    // IMPORTANT: send EOI before we iret away (we bypass normal irq return path)
    pic_send_eoi(0); // IRQ0 timer

    // Restore the saved frame exactly as-is
    __asm__ volatile(
        "mov %0, %%esp\n"
        "pop %%gs\n"
        "pop %%fs\n"
        "pop %%es\n"
        "pop %%ds\n"
        "popa\n"
        "add $8, %%esp\n"  // skip int_no, err_code
        "iret\n"
        :
        : "r"(next->esp)
        : "memory"
    );

    // Never reached - iret transfers control to new process
}

// Get state name string
const char* process_state_name(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_FREE:       return "FREE";
        case PROCESS_STATE_READY:      return "READY";
        case PROCESS_STATE_RUNNING:    return "RUNNING";
        case PROCESS_STATE_BLOCKED:    return "BLOCKED";
        case PROCESS_STATE_TERMINATED: return "TERMINATED";
        default:                       return "UNKNOWN";
    }
}
