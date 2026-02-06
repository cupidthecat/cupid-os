/**
 * process.h - Process management and scheduler declarations for CupidOS
 *
 * Implements preemptive multitasking via kernel threads with round-robin
 * scheduling.  All processes run in ring 0 sharing the same address space
 * (TempleOS-inspired, no security boundaries).
 *
 * Key design points:
 *   - Up to 32 concurrent processes
 *   - 10ms time slices driven by PIT timer (100Hz)
 *   - Idle process (PID 1) is always present, never exits
 *   - Process crashes are isolated — faulty process is terminated,
 *     kernel continues running
 */

#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"

/* ── Limits ───────────────────────────────────────────────────────── */
#define MAX_PROCESSES       32
#define DEFAULT_STACK_SIZE  8192      /* 8KB per process              */
#define PROCESS_NAME_LEN    32

/* ── Stack canary for overflow detection ──────────────────────────── */
#define STACK_CANARY        0xDEADC0DE

/* ── Process states ───────────────────────────────────────────────── */
typedef enum {
    PROCESS_READY = 0,       /* Ready to run                         */
    PROCESS_RUNNING,         /* Currently executing on CPU            */
    PROCESS_BLOCKED,         /* Waiting for event (future use)        */
    PROCESS_TERMINATED       /* Exited, slot can be reclaimed         */
} process_state_t;

/* ── Saved CPU context ────────────────────────────────────────────── */
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, esp, ebp;
    uint32_t eip, eflags;
    uint32_t cs, ds, es, fs, gs, ss;
} cpu_context_t;

/* ── Process Control Block (PCB) ──────────────────────────────────── */
typedef struct {
    uint32_t         pid;                     /* 1–32, 0 = unused     */
    process_state_t  state;                   /* Current state        */
    cpu_context_t    context;                 /* Saved CPU registers  */
    void            *stack_base;              /* Bottom of stack mem  */
    uint32_t         stack_size;              /* Stack size in bytes  */
    char             name[PROCESS_NAME_LEN];  /* Human-readable name  */
} process_t;

/* ── Public API ───────────────────────────────────────────────────── */

/**
 * process_init - Initialize the process subsystem
 *
 * Creates the idle process (PID 1) and sets up internal state.
 * Must be called once during kernel boot before enabling the scheduler.
 */
void process_init(void);

/**
 * process_create - Spawn a new kernel thread
 *
 * @entry_point: function to run as the process body
 * @name:        human-readable name (shown by `ps`)
 * @stack_size:  stack allocation in bytes (use DEFAULT_STACK_SIZE)
 *
 * Returns the new PID (2–32) on success, or 0 on failure.
 */
uint32_t process_create(void (*entry_point)(void),
                        const char *name,
                        uint32_t stack_size);

/**
 * process_exit - Terminate the currently running process
 *
 * Frees the process stack, marks the slot as free, and immediately
 * reschedules.  Never returns.  Cannot exit the idle process (PID 1).
 */
void process_exit(void);

/**
 * process_yield - Voluntarily give up the CPU
 *
 * Marks the current process as READY and triggers a reschedule.
 */
void process_yield(void);

/**
 * process_get_current_pid - Get the PID of the running process
 */
uint32_t process_get_current_pid(void);

/**
 * process_kill - Terminate a process by PID
 *
 * Frees the stack, marks the slot as free, and reschedules if the
 * killed process is the currently running one.  Cannot kill the idle
 * process (PID 1).
 */
void process_kill(uint32_t pid);

/**
 * process_get_state - Get the state of a process by PID
 *
 * Returns the process_state_t for the given PID, or -1 if the PID
 * does not refer to an active process slot.
 */
int process_get_state(uint32_t pid);

/**
 * process_list - Print all processes (used by `ps` shell command)
 */
void process_list(void);

/**
 * schedule - Select and switch to the next READY process
 *
 * Called from the timer IRQ0 handler every 10ms for preemptive
 * multitasking.  Also called explicitly by process_exit/process_yield.
 *
 * Uses round-robin scheduling with the idle process as a fallback.
 */
void schedule(void);

/**
 * process_is_active - Returns true if the scheduler is running
 */
bool process_is_active(void);

/**
 * process_get_count - Returns the number of active processes
 */
uint32_t process_get_count(void);

/**
 * process_start_scheduler - Activate preemptive scheduling
 *
 * Call after all initial processes are created and the PIT is set to
 * 100Hz.  After this, every timer tick triggers schedule().
 */
void process_start_scheduler(void);

/**
 * process_register_current - Register the currently running thread
 *
 * Adds the calling thread (e.g. the kernel main / desktop loop) to
 * the process table so the scheduler can save and restore its context.
 * Does NOT allocate a new stack — uses the existing kernel stack.
 *
 * @name: human-readable name (e.g. "desktop")
 * Returns: the assigned PID, or 0 on failure.
 */
uint32_t process_register_current(const char *name);

#endif /* PROCESS_H */
