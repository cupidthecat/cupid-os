#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "isr.h"

// Process configuration
#define MAX_PROCESSES 32
#define PROCESS_STACK_PAGES 2
#define PROCESS_STACK_SIZE  (PROCESS_STACK_PAGES * PAGE_SIZE)  // 8KB per process
#define PROCESS_NAME_MAX 32

// Number of priority levels (0 = highest, 7 = lowest)
#define NUM_PRIORITIES 8
#define DEFAULT_PRIORITY 4

// Process states
typedef enum {
    PROCESS_STATE_FREE = 0,     // Slot available for use
    PROCESS_STATE_READY,        // Ready to run, in scheduler queue
    PROCESS_STATE_RUNNING,      // Currently executing
    PROCESS_STATE_BLOCKED,      // Waiting for I/O or event
    PROCESS_STATE_TERMINATED    // Finished, awaiting cleanup
} process_state_t;

// Process Control Block (PCB)
typedef struct {
    // Identity
    uint32_t pid;                   // Process ID (1-32, 0 = invalid/free)
    char name[PROCESS_NAME_MAX];    // Human-readable name

    // CPU State (saved during context switch)
    uint32_t eax, ebx, ecx, edx;    // General purpose registers
    uint32_t esi, edi, ebp;         // Index and base registers
    uint32_t esp;                   // Stack pointer
    uint32_t eip;                   // Instruction pointer
    uint32_t eflags;                // CPU flags

    // Scheduling
    uint8_t priority;               // 0-7 (0 = highest)
    uint8_t state;                  // process_state_t
    uint32_t quantum_remaining;     // Timer ticks left in current slice
    uint32_t quantum_total;         // Total quantum for this priority

    // Memory
    uint32_t stack_base;            // Bottom of allocated stack
    uint32_t stack_size;            // Size of stack (always PROCESS_STACK_SIZE)
    uint32_t stack_pages;           // Number of pages allocated for stack

    // Bookkeeping
    uint32_t ticks_used;            // Total CPU ticks consumed
    uint32_t parent_pid;            // PID of parent process (0 for kernel)
    int32_t exit_code;              // Exit code when terminated
} pcb_t;

// Quantum values per priority level (in timer ticks, assuming ~1ms per tick)
// Priority 0 gets 50ms, priority 7 gets 10ms
static const uint32_t priority_quantum[NUM_PRIORITIES] = {
    50, 45, 40, 30, 25, 20, 15, 10
};

// Global process state (defined in process.c)
extern pcb_t process_table[MAX_PROCESSES];
extern pcb_t* current_process;
extern uint32_t next_pid;

// Process management functions
void process_init(void);
int32_t process_create(const char* name, void (*entry_point)(void), uint8_t priority);
int32_t process_create_kernel(void);  // Special: makes current context PID 1
void process_exit(int exit_code);
int process_kill(uint32_t pid);

// Process control
void process_yield(void);
int process_set_priority(uint32_t pid, uint8_t priority);
process_state_t process_get_state(uint32_t pid);
pcb_t* process_get_current(void);
pcb_t* process_get_by_pid(uint32_t pid);

// Blocking
void process_block(void);
void process_unblock(uint32_t pid);

// Context switching (called from timer IRQ)
void process_switch_context(struct registers* regs);

// String helper for state names
const char* process_state_name(process_state_t state);

#endif
