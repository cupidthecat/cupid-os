#include "scheduler.h"
#include "process.h"

// External functions from kernel.c
extern void print(const char* str);
extern void print_int(uint32_t num);

// Ready queues for each priority level
process_queue_t ready_queues[NUM_PRIORITIES];

// Internal scheduler state
static bool scheduling_enabled = false;

void scheduler_init(void) {
    // Initialize all priority queues to empty
    for (int i = 0; i < NUM_PRIORITIES; i++) {
        ready_queues[i].head = 0;
        ready_queues[i].tail = 0;
        ready_queues[i].count = 0;
        for (int j = 0; j < MAX_PROCESSES; j++) {
            ready_queues[i].pids[j] = 0;
        }
    }
    scheduling_enabled = false;
}

// Add a process to the back of its priority queue
void scheduler_add(pcb_t* process) {
    if (!process || process->priority >= NUM_PRIORITIES) {
        return;
    }

    process_queue_t* queue = &ready_queues[process->priority];

    // Check if queue is full
    if (queue->count >= MAX_PROCESSES) {
        return;
    }

    // Add to tail
    queue->pids[queue->tail] = process->pid;
    queue->tail = (queue->tail + 1) % MAX_PROCESSES;
    queue->count++;
}

// Remove a process from its priority queue (if present)
void scheduler_remove(pcb_t* process) {
    if (!process || process->priority >= NUM_PRIORITIES) {
        return;
    }

    process_queue_t* queue = &ready_queues[process->priority];

    // Search for the process in the queue
    uint8_t idx = queue->head;
    for (uint8_t i = 0; i < queue->count; i++) {
        if (queue->pids[idx] == process->pid) {
            // Found it - shift remaining elements
            uint8_t shift_idx = idx;
            for (uint8_t j = i; j < queue->count - 1; j++) {
                uint8_t next_idx = (shift_idx + 1) % MAX_PROCESSES;
                queue->pids[shift_idx] = queue->pids[next_idx];
                shift_idx = next_idx;
            }
            queue->count--;
            queue->tail = (queue->tail - 1 + MAX_PROCESSES) % MAX_PROCESSES;
            return;
        }
        idx = (idx + 1) % MAX_PROCESSES;
    }
}

// Get the next process to run
// Returns NULL if no ready processes
pcb_t* scheduler_next(void) {
    // Check each priority level from highest (0) to lowest (7)
    for (int prio = 0; prio < NUM_PRIORITIES; prio++) {
        process_queue_t* queue = &ready_queues[prio];

        if (queue->count > 0) {
            // Dequeue from front
            uint32_t pid = queue->pids[queue->head];
            queue->head = (queue->head + 1) % MAX_PROCESSES;
            queue->count--;

            // Return the PCB
            return process_get_by_pid(pid);
        }
    }

    // No ready processes - return kernel process (PID 1) as idle
    return process_get_by_pid(1);
}

// Called by timer IRQ handler on each tick
// Returns true if context switch should occur
bool scheduler_tick(void) {
    if (!scheduling_enabled || !current_process) {
        return false;
    }

    // Don't preempt blocked or terminated processes
    if (current_process->state != PROCESS_STATE_RUNNING) {
        return false;
    }

    // Update CPU usage counter
    current_process->ticks_used++;

    // Decrement quantum
    if (current_process->quantum_remaining > 0) {
        current_process->quantum_remaining--;
    }

    // If quantum exhausted, signal context switch
    if (current_process->quantum_remaining == 0) {
        return true;
    }

    return false;
}

void scheduler_enable(void) {
    scheduling_enabled = true;
}

void scheduler_disable(void) {
    scheduling_enabled = false;
}

bool scheduler_is_enabled(void) {
    return scheduling_enabled;
}

// Debug function to print scheduler state
void scheduler_debug_print(void) {
    print("Scheduler queues:\n");
    for (int prio = 0; prio < NUM_PRIORITIES; prio++) {
        process_queue_t* queue = &ready_queues[prio];
        if (queue->count > 0) {
            print("  Priority ");
            print_int(prio);
            print(": ");

            uint8_t idx = queue->head;
            for (uint8_t i = 0; i < queue->count; i++) {
                print_int(queue->pids[idx]);
                print(" ");
                idx = (idx + 1) % MAX_PROCESSES;
            }
            print("\n");
        }
    }
}
