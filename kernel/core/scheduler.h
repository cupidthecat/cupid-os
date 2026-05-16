#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"
#include "process.h"

// Simple circular queue for each priority level
typedef struct {
    uint32_t pids[MAX_PROCESSES];   // PIDs in this queue
    uint8_t head;                   // Front of queue
    uint8_t tail;                   // Back of queue
    uint8_t count;                  // Number of items
} process_queue_t;

// Scheduler state (defined in scheduler.c)
extern process_queue_t ready_queues[NUM_PRIORITIES];

// Scheduler functions
void scheduler_init(void);

// Add a process to its priority queue
void scheduler_add(pcb_t* process);

// Remove a process from its priority queue
void scheduler_remove(pcb_t* process);

// Get the next process to run (returns NULL if no ready processes)
pcb_t* scheduler_next(void);

// Called by timer IRQ on each tick
// Returns true if a context switch should occur
bool scheduler_tick(void);

// Enable/disable scheduling (for critical sections during init)
void scheduler_enable(void);
void scheduler_disable(void);
bool scheduler_is_enabled(void);

// Debug: print scheduler state
void scheduler_debug_print(void);

#endif
