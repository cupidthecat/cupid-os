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
 *   - Process crashes are isolated - faulty process is terminated,
 *     kernel continues running
*/

#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"

#define MAX_PROCESSES       32
#define DEFAULT_STACK_SIZE  32768     /* 32KB per process */
#define PROCESS_NAME_LEN    32

#define STACK_CANARY        0xDEADC0DE

typedef enum {
    PROCESS_READY = 0,       /* Ready to run */
    PROCESS_RUNNING,         /* Currently executing on CPU */
    PROCESS_BLOCKED,         /* Waiting for event (future use) */
    PROCESS_TERMINATED       /* Exited, slot can be reclaimed */
} process_state_t;

typedef enum {
    PROCESS_DOMAIN_KERNEL = 0,   /* Kernel-owned thread / service */
    PROCESS_DOMAIN_HOSTED,       /* Hosted in-kernel app/runtime */
    PROCESS_DOMAIN_EXTERNAL      /* ELF/CUPD program via loader */
} process_domain_t;

typedef enum {
    PROCESS_IMAGE_NONE = 0,
    PROCESS_IMAGE_PERMANENT,
    PROCESS_IMAGE_EXTERNAL_LEASE
} process_image_ownership_t;

/* A process image descriptor is consumed only after successful process
 * creation.  An external lease generation is opaque to callers. */
typedef struct {
    uint32_t                  base;
    uint32_t                  size;
    process_image_ownership_t ownership;
    uint32_t                  lease_generation;
} process_image_t;

typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, esp, ebp;
    uint32_t eip, eflags;
    uint32_t cs, ds, es, fs, gs, ss;
} cpu_context_t;

typedef struct {
    uint32_t         pid;                     /* 1-32, 0 = unused */
    process_state_t  state;                   /* Current state */
    cpu_context_t    context;                 /* Saved CPU registers */
    uint8_t          fp_state[512] __attribute__((aligned(16)));
                                              /* FXSAVE/FXRSTOR area */
    void            *stack_base;              /* Bottom of stack mem */
    uint32_t         stack_size;              /* Stack size in bytes */
    uint32_t         image_base;              /* Owned image range start */
    uint32_t         image_size;              /* Owned image range size */
    process_image_ownership_t image_ownership; /* Image cleanup policy */
    uint32_t         image_lease_generation;  /* External lease identity */
    process_domain_t domain;                  /* Execution domain */
    char             name[PROCESS_NAME_LEN];  /* Human-readable name */
    uint8_t          on_cpu;   /* 0..31 = CPU currently running this process;
                                * 0xFFu = detached and safe to reap when
                                * state is PROCESS_TERMINATED */
    uint8_t          last_cpu; /* last CPU that ran this process */
} process_t;


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
 * Returns the new PID (2-32) on success, or 0 on failure.
*/
uint32_t process_create(void (*entry_point)(void),
                        const char *name,
                        uint32_t stack_size);

uint32_t process_create_ex(void (*entry_point)(void),
                           const char *name,
                           uint32_t stack_size,
                           process_domain_t domain);

/**
 * process_create_with_arg - Spawn a new kernel thread with one argument
 *
 * Like process_create, but pushes `arg` onto the stack so the entry
 * function can receive it as its first parameter.  Image-owning loaders use
 * process_create_with_arg_image_ex so metadata is published atomically.
 *
 * @entry_point: function to run (cast to void(*)(void) but actually
 *               takes one uint32_t argument)
 * @name:        human-readable name
 * @stack_size:  stack allocation in bytes
 * @arg:         32-bit argument pushed onto the stack
 *
 * Returns the new PID (2-32) on success, or 0 on failure.
*/
uint32_t process_create_with_arg(void (*entry_point)(void),
                                 const char *name,
                                 uint32_t stack_size,
                                 uint32_t arg);

uint32_t process_create_with_arg_ex(void (*entry_point)(void),
                                    const char *name,
                                    uint32_t stack_size,
                                    uint32_t arg,
                                    process_domain_t domain);

/**
 * process_create_with_arg_image_ex - Spawn with atomically-published image
 * ownership metadata
 *
 * On success, the descriptor is consumed and reset to PROCESS_IMAGE_NONE in
 * the same BKL critical section that publishes the READY process.  On failure,
 * the caller retains the descriptor and must pass it to process_image_discard.
 */
uint32_t process_create_with_arg_image_ex(void (*entry_point)(void),
                                          const char *name,
                                          uint32_t stack_size,
                                          uint32_t arg,
                                          process_domain_t domain,
                                          process_image_t *image);

/**
 * process_external_image_claim - Try to acquire the fixed external ELF arena
 *
 * On success, initializes image with the permanent arena and an exclusive
 * lease.  The descriptor must be consumed by process creation or discarded.
 */
bool process_external_image_claim(process_image_t *image);

/**
 * process_image_discard - Release an unconsumed image descriptor
 *
 * Cancels an external lease.  Permanent images require no cleanup.  The
 * descriptor is reset on return.
 */
void process_image_discard(process_image_t *image);

/**
 * process_exit - Terminate the currently running process
 *
 * Marks the process terminated and immediately reschedules.  Stack/image
 * cleanup is deferred until the scheduler has detached the owning CPU and a
 * quiescent reaper reclaims the slot.  Never returns.  Cannot exit PID 1.
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
 * Marks the target terminated and reschedules if it is currently running.
 * Cleanup is immediate only for a target already detached from every CPU;
 * otherwise the quiescent reaper performs it later.  Cannot kill PID 1.
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
 * process_list_adam - Print TempleOS-style Adam task tree
 *
 * Root = Adam (idle, PID 1). Children grouped by domain
 * (kernel/hosted/external). Used by `adam` shell command.
*/
void process_list_adam(void);

/**
 * schedule - Select and switch to the next READY process
 *
 * Called from the timer IRQ0 handler every 10ms for preemptive
 * multitasking.  Also called explicitly by process_exit/process_yield.
 *
 * Uses round-robin scheduling over detached READY PCBs.  PID 1 is the BSP
 * fallback; APs capture their own PID-less scheduler context so a terminating
 * task can detach without sharing an idle stack between CPUs.
 * Calls made from generic interrupt context are deferred until the common
 * stub has completed the handler and acknowledged the interrupt.
*/
void schedule(void);

/**
 * process_reschedule_if_pending - Consume a CPU-local remote request
 *
 * Switches immediately when the scheduler is active and this CPU is outside
 * a BKL critical section.  Otherwise the request remains pending for the
 * outer BKL release or a later safe point.
 */
void process_reschedule_if_pending(void);

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
 * Does NOT allocate a new stack - uses the existing kernel stack.
 *
 * @name: human-readable name (e.g. "desktop")
 * Returns: the assigned PID, or 0 on failure.
*/
uint32_t process_register_current(const char *name);

/**
 * process_block - Suspend a READY process so the scheduler skips it
 *
 * Sets the process state to PROCESS_BLOCKED.  The process will not be
 * scheduled until process_unblock() is called.  Safe to call on a
 * process that is already blocked or terminated (no-op in those cases).
 *
 * @pid: PID of the process to block (must not be 0 or 1/idle)
*/
void process_block(uint32_t pid);

/**
 * process_unblock - Return a blocked process to the READY state
 *
 * Transitions the process from PROCESS_BLOCKED back to PROCESS_READY
 * so the scheduler will dispatch it again.  No-op if the process is
 * not currently blocked (e.g. already terminated or running).
 *
 * @pid: PID of the process to unblock
*/
void process_unblock(uint32_t pid);

const char *process_domain_name(process_domain_t domain);

#endif /* PROCESS_H */
