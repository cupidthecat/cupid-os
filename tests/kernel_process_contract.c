#include "process.h"
#include "memory.h"
#include "percpu.h"
#include "bkl.h"
#include "shell.h"
#include "gui.h"
#include "kernel.h"
#include "serial.h"
#include "string.h"
#include "smp.h"

#define TEST_STACK_COUNT (MAX_PROCESSES + 2u)

per_cpu_t kernel_contract_cpu;
per_cpu_t kernel_contract_remote_cpu;
per_cpu_t *kernel_contract_this_cpu = &kernel_contract_cpu;
int smp_cpu_count_var = 1;

static uint8_t test_stacks[TEST_STACK_COUNT][DEFAULT_STACK_SIZE]
    __attribute__((aligned(16)));
static bool test_stack_used[TEST_STACK_COUNT];
static uint32_t test_stack_allocations;
static uint32_t test_stack_frees;
static uint32_t test_pmm_releases;
static uint32_t test_last_release_base;
static uint32_t test_last_release_size;
static uint32_t test_bkl_depth;
static uint32_t test_bkl_normal_unlocks;
static uint32_t test_bkl_handoff_releases;
static uint32_t test_bkl_unlock_underflows;
static uint32_t test_bkl_handoff_violations;
static uint32_t test_reschedule_calls;
static int test_last_reschedule_cpu = -1;
static uint32_t test_resume_esp;
static void *test_resume_eip;
static bool test_resume_armed;
static uint32_t test_resume_eflags;
static uint32_t test_outer_eflags = 0x00000202u;

void context_switch(process_t *old_proc, process_t *new_proc,
                    uint32_t resume_eflags)
    __attribute__((noreturn));
void context_switch_resume(void);

static bool descriptor_is_empty(const process_image_t *image) {
    return image->base == 0u && image->size == 0u &&
           image->ownership == PROCESS_IMAGE_NONE &&
           image->lease_generation == 0u;
}

static bool strings_equal(const char *left, const char *right) {
    uint32_t i = 0u;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return false;
        }
        i++;
    }
    return left[i] == right[i];
}

static void dummy_entry(void) {
}

void *memset(void *destination, int value, size_t count) {
    uint8_t *bytes = (uint8_t *)destination;
    for (size_t i = 0u; i < count; i++) {
        bytes[i] = (uint8_t)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0u; i < count; i++) {
        out[i] = in[i];
    }
    return destination;
}

void *kmalloc_debug(size_t size, const char *file, uint32_t line) {
    (void)file;
    (void)line;
    if (size > DEFAULT_STACK_SIZE) {
        return NULL;
    }
    for (uint32_t i = 0u; i < TEST_STACK_COUNT; i++) {
        if (!test_stack_used[i]) {
            test_stack_used[i] = true;
            test_stack_allocations++;
            return test_stacks[i];
        }
    }
    return NULL;
}

void kfree(void *pointer) {
    if (!pointer) {
        return;
    }
    for (uint32_t i = 0u; i < TEST_STACK_COUNT; i++) {
        if (pointer == test_stacks[i] && test_stack_used[i]) {
            test_stack_used[i] = false;
            test_stack_frees++;
            return;
        }
    }
}

void pmm_release_region(uint32_t start, uint32_t size) {
    test_pmm_releases++;
    test_last_release_base = start;
    test_last_release_size = size;
}

void bkl_lock(void) {
    if (this_cpu()->bkl_depth == 0u) {
        this_cpu()->bkl_eflags_saved = test_outer_eflags;
    }
    this_cpu()->bkl_depth++;
    test_bkl_depth++;
}

void bkl_unlock(void) {
    if (test_bkl_depth == 0u || this_cpu()->bkl_depth == 0u) {
        test_bkl_unlock_underflows++;
        return;
    }
    test_bkl_depth--;
    this_cpu()->bkl_depth--;
    if (this_cpu()->bkl_depth == 0u) {
        test_bkl_normal_unlocks++;
        process_reschedule_if_pending();
    }
}

bool bkl_held_by_this_cpu(void) {
    return test_bkl_depth != 0u;
}

bool bkl_is_initialized(void) {
    return true;
}

uint32_t bkl_context_switch_eflags(void) {
    return this_cpu()->bkl_eflags_saved;
}

bool bkl_context_switch_release(void) {
    if (test_bkl_depth != 1u || this_cpu()->bkl_depth != 1u) {
        test_bkl_handoff_violations++;
        return false;
    }
    test_bkl_depth = 0u;
    this_cpu()->bkl_depth = 0u;
    test_bkl_handoff_releases++;
    return true;
}

void context_switch(process_t *old_proc, process_t *new_proc,
                    uint32_t resume_eflags) {
    (void)old_proc;
    (void)new_proc;
    if (!test_resume_armed) {
        for (;;) {
        }
    }
    uint32_t resume_esp = test_resume_esp;
    void *resume_eip = test_resume_eip;
    test_resume_armed = false;
    test_resume_eflags = resume_eflags;
    if (!bkl_context_switch_release()) {
        for (;;) {
        }
    }
    __asm__ volatile("movl %0, %%esp; jmp *%1"
                     : : "r"(resume_esp), "r"(resume_eip) : "memory");
    __builtin_unreachable();
}

void context_switch_resume(void) {
}

void kernel_check_reschedule(void) {
}

void kernel_clear_reschedule(void) {
}

void shell_jit_discard_by_owner(uint32_t pid) {
    (void)pid;
}

int gui_destroy_windows_by_owner(uint32_t owner_pid) {
    (void)owner_pid;
    return 0;
}

int shell_jit_suspended_count(void) {
    return 0;
}

const char *shell_jit_suspended_get_name(int index) {
    (void)index;
    return "";
}

void shell_jit_program_kill_at(int index) {
    (void)index;
}

void print(const char *text) {
    (void)text;
}

void print_int(uint32_t value) {
    (void)value;
}

void serial_printf(const char *format, ...) {
    (void)format;
}

void klog(log_level_t level, const char *format, ...) {
    (void)level;
    (void)format;
}

void smp_reschedule(int cpu_id) {
    test_reschedule_calls++;
    test_last_reschedule_cpu = cpu_id;
    if (cpu_id == 0) {
        percpu_request_reschedule(&kernel_contract_cpu);
    }
}

static void hosted_call_void(void (*operation)(void)) {
    __asm__ volatile(
        "pushl %%ebp\n\t"
        "pushl %%ebx\n\t"
        "pushl %%esi\n\t"
        "pushl %%edi\n\t"
        "movl %%esp, %0\n\t"
        "leal 1f, %%eax\n\t"
        "movl %%eax, %1\n\t"
        "movb $1, %2\n\t"
        "call *%3\n\t"
        "1:\n\t"
        "popl %%edi\n\t"
        "popl %%esi\n\t"
        "popl %%ebx\n\t"
        "popl %%ebp\n\t"
        : "=m"(test_resume_esp), "=m"(test_resume_eip),
          "=m"(test_resume_armed)
        : "m"(operation)
        : "eax", "ecx", "edx", "memory", "cc");
}

static void hosted_schedule_once(void) {
    hosted_call_void(schedule);
}

static void hosted_kill(uint32_t pid) {
    void (*operation)(uint32_t) = process_kill;
    __asm__ volatile(
        "pushl %%ebp\n\t"
        "pushl %%ebx\n\t"
        "pushl %%esi\n\t"
        "pushl %%edi\n\t"
        "movl %%esp, %0\n\t"
        "leal 1f, %%eax\n\t"
        "movl %%eax, %1\n\t"
        "movb $1, %2\n\t"
        "pushl %4\n\t"
        "call *%3\n\t"
        "addl $4, %%esp\n\t"
        "1:\n\t"
        "popl %%edi\n\t"
        "popl %%esi\n\t"
        "popl %%ebx\n\t"
        "popl %%ebp\n\t"
        : "=m"(test_resume_esp), "=m"(test_resume_eip),
          "=m"(test_resume_armed)
        : "m"(operation), "m"(pid)
        : "eax", "ecx", "edx", "memory", "cc");
}

static int run_claim_busy_discard(void) {
    process_image_t image = {0};
    process_image_t busy = {0};
    process_image_t next = {0};

    if (!process_external_image_claim(&image) ||
        image.base != EXTERNAL_EXEC_ARENA_START ||
        image.size != EXTERNAL_EXEC_ARENA_END - EXTERNAL_EXEC_ARENA_START ||
        image.ownership != PROCESS_IMAGE_EXTERNAL_LEASE ||
        image.lease_generation == 0u) {
        return 1;
    }
    uint32_t first_generation = image.lease_generation;
    if (process_external_image_claim(&busy) || !descriptor_is_empty(&busy)) {
        return 2;
    }
    process_image_discard(&image);
    if (!descriptor_is_empty(&image) || test_pmm_releases != 0u) {
        return 3;
    }
    if (!process_external_image_claim(&next) ||
        next.lease_generation == first_generation) {
        return 4;
    }
    process_image_discard(&next);
    return test_bkl_depth == 0u ? 0 : 5;
}

static int run_stale_discard(void) {
    process_image_t first = {0};
    process_image_t active = {0};
    process_image_t stale;
    process_image_t busy = {0};
    process_image_t final = {0};

    if (!process_external_image_claim(&first)) {
        return 1;
    }
    stale = first;
    process_image_discard(&first);
    if (!process_external_image_claim(&active) ||
        active.lease_generation == stale.lease_generation) {
        return 2;
    }
    process_image_discard(&stale);
    if (!descriptor_is_empty(&stale) || process_external_image_claim(&busy)) {
        return 3;
    }
    process_image_discard(&active);
    if (!process_external_image_claim(&final)) {
        return 4;
    }
    process_image_discard(&final);
    return 0;
}

static int create_idle_slot(void) {
    uint32_t pid = process_create(dummy_entry, "idle", DEFAULT_STACK_SIZE);
    return pid == 1u ? 0 : 1;
}

static int run_consume_and_kill(void) {
    process_image_t image = {0};
    process_image_t busy = {0};
    process_image_t next = {0};

    if (create_idle_slot() != 0 || !process_external_image_claim(&image)) {
        return 1;
    }
    uint32_t generation = image.lease_generation;
    uint32_t pid = process_create_with_arg_image_ex(
        dummy_entry, "external", DEFAULT_STACK_SIZE, 0u,
        PROCESS_DOMAIN_EXTERNAL, &image);
    if (pid != 2u || !descriptor_is_empty(&image) ||
        process_get_count() != 2u || process_external_image_claim(&busy)) {
        return 2;
    }
    process_kill(pid);
    if (process_get_count() != 1u || test_stack_frees != 1u ||
        test_pmm_releases != 0u) {
        return 3;
    }
    if (!process_external_image_claim(&next) ||
        next.lease_generation == generation) {
        return 4;
    }
    process_image_discard(&next);
    return 0;
}

static int run_failed_consume(void) {
    process_image_t image = {0};
    process_image_t busy = {0};

    if (create_idle_slot() != 0 || !process_external_image_claim(&image)) {
        return 1;
    }
    process_image_t original = image;
    uint32_t pid = process_create_with_arg_image_ex(
        dummy_entry, "wrong-domain", DEFAULT_STACK_SIZE, 0u,
        PROCESS_DOMAIN_KERNEL, &image);
    if (pid != 0u || image.base != original.base ||
        image.size != original.size || image.ownership != original.ownership ||
        image.lease_generation != original.lease_generation ||
        process_external_image_claim(&busy)) {
        return 2;
    }
    process_image_discard(&image);
    if (!descriptor_is_empty(&image)) {
        return 3;
    }
    return 0;
}

static uint32_t create_running_external(uint32_t *generation) {
    process_image_t image = {0};
    if (create_idle_slot() != 0 || !process_external_image_claim(&image)) {
        return 0u;
    }
    *generation = image.lease_generation;
    uint32_t pid = process_create_with_arg_image_ex(
        dummy_entry, "running", DEFAULT_STACK_SIZE, 0u,
        PROCESS_DOMAIN_EXTERNAL, &image);
    if (pid != 2u || !descriptor_is_empty(&image)) {
        return 0u;
    }
    kernel_contract_cpu.cpu_id = 0u;
    kernel_contract_cpu.current_pid = 0u;
    kernel_contract_this_cpu = &kernel_contract_cpu;
    process_start_scheduler();
    hosted_schedule_once();
    if (kernel_contract_cpu.current_pid != pid ||
        process_get_state(pid) != (int)PROCESS_RUNNING ||
        test_bkl_depth != 0u || test_resume_armed ||
        test_bkl_handoff_releases != 1u ||
        test_bkl_unlock_underflows != 0u ||
        test_resume_eflags != test_outer_eflags) {
        return 0u;
    }
    return pid;
}

static int run_schedule_handoff(void) {
    uint32_t generation = 0u;
    uint32_t pid = create_running_external(&generation);
    (void)generation;
    if (pid == 0u || test_bkl_handoff_violations != 0u ||
        test_bkl_normal_unlocks == 0u) {
        return 1;
    }

    hosted_schedule_once();
    if (kernel_contract_cpu.current_pid != 1u ||
        test_bkl_handoff_releases != 2u ||
        test_bkl_unlock_underflows != 0u) {
        return 2;
    }

    hosted_schedule_once();
    if (kernel_contract_cpu.current_pid != pid ||
        test_bkl_handoff_releases != 3u ||
        test_bkl_unlock_underflows != 0u) {
        return 3;
    }

    return test_bkl_handoff_violations == 0u ? 0 : 4;
}

static int run_no_switch_unlock(void) {
    if (create_idle_slot() != 0) return 1;
    kernel_contract_cpu.cpu_id = 0u;
    kernel_contract_cpu.current_pid = 0u;
    kernel_contract_this_cpu = &kernel_contract_cpu;
    process_start_scheduler();
    hosted_schedule_once();
    if (kernel_contract_cpu.current_pid != 1u ||
        test_bkl_handoff_releases != 1u) {
        return 2;
    }

    uint32_t unlocks = test_bkl_normal_unlocks;
    schedule();
    if (kernel_contract_cpu.current_pid != 1u ||
        test_bkl_handoff_releases != 1u ||
        test_bkl_normal_unlocks != unlocks + 1u ||
        test_bkl_depth != 0u || test_bkl_unlock_underflows != 0u) {
        return 3;
    }
    return 0;
}

static int run_remote_idle_owned(void) {
    if (create_idle_slot() != 0) return 1;
    kernel_contract_cpu.cpu_id = 0u;
    kernel_contract_cpu.current_pid = 0u;
    kernel_contract_this_cpu = &kernel_contract_cpu;
    process_start_scheduler();
    hosted_schedule_once();
    if (kernel_contract_cpu.current_pid != 1u ||
        test_bkl_handoff_releases != 1u || test_resume_armed) {
        return 2;
    }

    kernel_contract_remote_cpu.cpu_id = 1u;
    kernel_contract_remote_cpu.current_pid = 0u;
    kernel_contract_this_cpu = &kernel_contract_remote_cpu;
    uint32_t handoffs = test_bkl_handoff_releases;
    uint32_t unlocks = test_bkl_normal_unlocks;
    hosted_schedule_once();
    if (kernel_contract_remote_cpu.current_pid != 0u ||
        kernel_contract_cpu.current_pid != 1u ||
        process_get_state(1u) != (int)PROCESS_RUNNING ||
        test_bkl_handoff_releases != handoffs ||
        test_bkl_normal_unlocks != unlocks + 1u ||
        test_bkl_depth != 0u || this_cpu()->bkl_depth != 0u ||
        test_bkl_unlock_underflows != 0u || !test_resume_armed) {
        return 3;
    }
    test_resume_armed = false;
    return 0;
}

static int run_terminated_ap_idle_context(void) {
    process_image_t image = {0};
    process_image_t next = {0};
    if (create_idle_slot() != 0) return 1;

    kernel_contract_cpu.cpu_id = 0u;
    kernel_contract_cpu.current_pid = 0u;
    kernel_contract_this_cpu = &kernel_contract_cpu;
    process_start_scheduler();
    hosted_schedule_once();
    if (kernel_contract_cpu.current_pid != 1u || test_resume_armed) return 2;

    if (!process_external_image_claim(&image)) return 3;
    uint32_t generation = image.lease_generation;
    uint32_t pid = process_create_with_arg_image_ex(
        dummy_entry, "ap-task", DEFAULT_STACK_SIZE, 0u,
        PROCESS_DOMAIN_EXTERNAL, &image);
    if (pid != 2u || !descriptor_is_empty(&image)) return 4;

    kernel_contract_remote_cpu.cpu_id = 1u;
    kernel_contract_remote_cpu.current_pid = 0u;
    kernel_contract_this_cpu = &kernel_contract_remote_cpu;
    hosted_schedule_once();
    if (kernel_contract_remote_cpu.current_pid != pid ||
        process_get_state(pid) != (int)PROCESS_RUNNING || test_resume_armed) {
        return 5;
    }

    uint32_t handoffs = test_bkl_handoff_releases;
    hosted_kill(pid);
    if (kernel_contract_remote_cpu.current_pid != 0u ||
        kernel_contract_cpu.current_pid != 1u ||
        process_get_state(pid) != (int)PROCESS_TERMINATED ||
        test_bkl_handoff_releases != handoffs + 1u || test_resume_armed ||
        test_bkl_depth != 0u || test_bkl_unlock_underflows != 0u) {
        return 6;
    }

    if (!process_external_image_claim(&next) || test_stack_frees != 1u ||
        next.lease_generation == generation || process_get_count() != 1u) {
        return 7;
    }
    process_image_discard(&next);
    return 0;
}

static int run_interrupt_defers_pending(void) {
    uint32_t generation = 0u;
    uint32_t pid = create_running_external(&generation);
    (void)generation;
    if (pid == 0u) return 1;

    kernel_contract_cpu.interrupt_depth = 1u;
    percpu_request_reschedule(&kernel_contract_cpu);
    hosted_call_void(process_reschedule_if_pending);
    if (!percpu_reschedule_is_pending(&kernel_contract_cpu) ||
        kernel_contract_cpu.current_pid != pid || !test_resume_armed) {
        return 2;
    }
    test_resume_armed = false;

    kernel_contract_cpu.interrupt_depth = 0u;
    hosted_call_void(process_reschedule_if_pending);
    if (percpu_reschedule_is_pending(&kernel_contract_cpu) ||
        kernel_contract_cpu.current_pid != 1u || test_resume_armed ||
        test_bkl_depth != 0u || test_bkl_unlock_underflows != 0u) {
        return 3;
    }
    return 0;
}

static int run_interrupt_direct_schedule_defers(void) {
    uint32_t generation = 0u;
    uint32_t pid = create_running_external(&generation);
    (void)generation;
    if (pid == 0u) return 1;

    kernel_contract_cpu.interrupt_depth = 1u;
    uint32_t handoffs = test_bkl_handoff_releases;
    uint32_t unlocks = test_bkl_normal_unlocks;
    hosted_schedule_once();
    if (!percpu_reschedule_is_pending(&kernel_contract_cpu) ||
        kernel_contract_cpu.current_pid != pid ||
        test_bkl_handoff_releases != handoffs ||
        test_bkl_normal_unlocks != unlocks || !test_resume_armed) {
        return 2;
    }
    test_resume_armed = false;

    kernel_contract_cpu.interrupt_depth = 0u;
    hosted_call_void(process_reschedule_if_pending);
    if (percpu_reschedule_is_pending(&kernel_contract_cpu) ||
        kernel_contract_cpu.current_pid != 1u || test_resume_armed ||
        test_bkl_depth != 0u || test_bkl_unlock_underflows != 0u) {
        return 3;
    }
    return 0;
}

static int run_if_clear_handoff(void) {
    uint32_t generation = 0u;
    test_outer_eflags = 0x00000002u;
    uint32_t pid = create_running_external(&generation);
    (void)generation;
    if (pid == 0u || test_resume_eflags != 0x00000002u ||
        test_bkl_handoff_releases != 1u ||
        test_bkl_unlock_underflows != 0u) {
        return 1;
    }
    return 0;
}

static int run_nested_schedule_defers(void) {
    uint32_t generation = 0u;
    uint32_t pid = create_running_external(&generation);
    (void)generation;
    if (pid == 0u) return 1;

    bkl_lock();
    uint32_t handoffs = test_bkl_handoff_releases;
    schedule();
    if (kernel_contract_cpu.current_pid != pid ||
        test_bkl_handoff_releases != handoffs ||
        this_cpu()->bkl_depth != 1u || test_bkl_depth != 1u ||
        !percpu_reschedule_is_pending(&kernel_contract_cpu)) {
        return 2;
    }
    hosted_call_void(bkl_unlock);
    if (kernel_contract_cpu.current_pid != 1u ||
        percpu_reschedule_is_pending(&kernel_contract_cpu) ||
        test_bkl_handoff_releases != handoffs + 1u ||
        test_bkl_depth != 0u || test_bkl_unlock_underflows != 0u) {
        return 3;
    }
    return 0;
}

static int run_deferred_self_reap(void) {
    process_image_t next = {0};
    uint32_t generation = 0u;
    uint32_t pid = create_running_external(&generation);
    if (pid == 0u) return 1;

    hosted_kill(pid);
    if (kernel_contract_cpu.current_pid != 1u || test_stack_frees != 0u ||
        process_get_state(pid) != (int)PROCESS_TERMINATED ||
        test_bkl_depth != 0u || test_resume_armed) {
        return 2;
    }
    if (!process_external_image_claim(&next) || test_stack_frees != 1u ||
        next.lease_generation == generation || process_get_count() != 1u) {
        return 3;
    }
    process_image_discard(&next);
    return 0;
}

static int run_deferred_remote_reap(void) {
    process_image_t busy = {0};
    process_image_t next = {0};
    uint32_t generation = 0u;
    uint32_t pid = create_running_external(&generation);
    if (pid == 0u) return 1;

    kernel_contract_remote_cpu.cpu_id = 1u;
    kernel_contract_remote_cpu.current_pid = 0u;
    kernel_contract_this_cpu = &kernel_contract_remote_cpu;
    process_kill(pid);
    if (process_get_state(pid) != (int)PROCESS_TERMINATED ||
        test_stack_frees != 0u || test_reschedule_calls != 1u ||
        test_last_reschedule_cpu != 0 ||
        !percpu_reschedule_is_pending(&kernel_contract_cpu) ||
        process_external_image_claim(&busy)) {
        return 2;
    }

    kernel_contract_this_cpu = &kernel_contract_cpu;
    hosted_call_void(process_reschedule_if_pending);
    if (kernel_contract_cpu.current_pid != 1u || test_stack_frees != 0u ||
        percpu_reschedule_is_pending(&kernel_contract_cpu) ||
        test_bkl_depth != 0u || test_resume_armed ||
        test_bkl_unlock_underflows != 0u) {
        return 3;
    }
    if (!process_external_image_claim(&next) || test_stack_frees != 1u ||
        next.lease_generation == generation || process_get_count() != 1u) {
        return 4;
    }
    process_image_discard(&next);
    return 0;
}

static int run_pending_on_outer_unlock(void) {
    uint32_t generation = 0u;
    uint32_t pid = create_running_external(&generation);
    (void)generation;
    if (pid == 0u) return 1;

    bkl_lock();
    percpu_request_reschedule(&kernel_contract_cpu);
    process_reschedule_if_pending();
    if (!percpu_reschedule_is_pending(&kernel_contract_cpu) ||
        kernel_contract_cpu.current_pid != pid || this_cpu()->bkl_depth != 1u) {
        return 2;
    }
    hosted_call_void(bkl_unlock);
    if (percpu_reschedule_is_pending(&kernel_contract_cpu) ||
        kernel_contract_cpu.current_pid != 1u || test_bkl_depth != 0u ||
        test_bkl_unlock_underflows != 0u) {
        return 3;
    }
    return 0;
}

static int run_stack_canary_reap(void) {
    process_image_t next = {0};
    uint32_t generation = 0u;
    uint32_t pid = create_running_external(&generation);
    if (pid == 0u || !test_stack_used[1]) return 1;

    *(uint32_t *)test_stacks[1] = 0u;
    hosted_schedule_once();
    if (kernel_contract_cpu.current_pid != 1u ||
        process_get_state(pid) != (int)PROCESS_TERMINATED ||
        test_stack_frees != 0u || test_bkl_unlock_underflows != 0u) {
        return 2;
    }
    if (!process_external_image_claim(&next) || test_stack_frees != 1u ||
        next.lease_generation == generation || process_get_count() != 1u) {
        return 3;
    }
    process_image_discard(&next);
    return 0;
}

static bool descriptor_equal(const process_image_t *left,
                             const process_image_t *right) {
    return left->base == right->base && left->size == right->size &&
           left->ownership == right->ownership &&
           left->lease_generation == right->lease_generation;
}

static int run_permanent_consume(void) {
    process_image_t cupidc = {
        CUPIDC_EXEC_ARENA_START, PAGE_SIZE, PROCESS_IMAGE_PERMANENT, 0u};
    process_image_t cupidasm = {
        CUPIDASM_EXEC_ARENA_START, PAGE_SIZE, PROCESS_IMAGE_PERMANENT, 0u};

    if (create_idle_slot() != 0) return 1;
    uint32_t pid = process_create_with_arg_image_ex(
        dummy_entry, "CupidC", DEFAULT_STACK_SIZE, 0u,
        PROCESS_DOMAIN_EXTERNAL, &cupidc);
    if (pid != 2u || !descriptor_is_empty(&cupidc) ||
        test_stack_allocations != 2u) {
        return 2;
    }
    process_kill(pid);
    if (process_get_count() != 1u || test_stack_frees != 1u) return 3;

    pid = process_create_with_arg_image_ex(
        dummy_entry, "CupidASM", DEFAULT_STACK_SIZE, 0u,
        PROCESS_DOMAIN_EXTERNAL, &cupidasm);
    if (pid != 2u || !descriptor_is_empty(&cupidasm) ||
        test_stack_allocations != 3u) {
        return 4;
    }
    process_kill(pid);
    if (process_get_count() != 1u || test_stack_frees != 2u ||
        test_pmm_releases != 0u) {
        return 5;
    }
    return 0;
}

static int run_permanent_wrong_domain(void) {
    process_image_t image = {
        CUPIDC_EXEC_ARENA_START, PAGE_SIZE, PROCESS_IMAGE_PERMANENT, 0u};
    process_image_t original = image;
    if (create_idle_slot() != 0) return 1;
    uint32_t pid = process_create_with_arg_image_ex(
        dummy_entry, "wrong-domain", DEFAULT_STACK_SIZE, 0u,
        PROCESS_DOMAIN_KERNEL, &image);
    if (pid != 0u || !descriptor_equal(&image, &original) ||
        test_stack_allocations != 1u || process_get_count() != 1u) {
        return 2;
    }
    process_image_discard(&image);
    return descriptor_is_empty(&image) ? 0 : 3;
}

static int run_invalid_permanent_ranges(void) {
    process_image_t invalid[] = {
        {EXTERNAL_EXEC_ARENA_START, PAGE_SIZE, PROCESS_IMAGE_PERMANENT, 0u},
        {STACK_BOTTOM, PAGE_SIZE, PROCESS_IMAGE_PERMANENT, 0u},
        {0x02000000u, PAGE_SIZE, PROCESS_IMAGE_PERMANENT, 0u},
        {CUPIDC_EXEC_ARENA_START + 1u, PAGE_SIZE,
         PROCESS_IMAGE_PERMANENT, 0u},
        {CUPIDC_EXEC_ARENA_START, PAGE_SIZE - 1u,
         PROCESS_IMAGE_PERMANENT, 0u},
        {CUPIDC_EXEC_ARENA_END - PAGE_SIZE, PAGE_SIZE * 2u,
         PROCESS_IMAGE_PERMANENT, 0u},
        {CUPIDASM_EXEC_ARENA_START, PAGE_SIZE,
         PROCESS_IMAGE_PERMANENT, 1u}
    };
    if (create_idle_slot() != 0) return 1;
    uint32_t count = (uint32_t)(sizeof(invalid) / sizeof(invalid[0]));
    for (uint32_t i = 0u; i < count; i++) {
        process_image_t original = invalid[i];
        uint32_t pid = process_create_with_arg_image_ex(
            dummy_entry, "invalid-permanent", DEFAULT_STACK_SIZE, 0u,
            PROCESS_DOMAIN_EXTERNAL, &invalid[i]);
        if (pid != 0u || !descriptor_equal(&invalid[i], &original) ||
            test_stack_allocations != 1u || process_get_count() != 1u) {
            return (int)(i + 2u);
        }
    }
    return 0;
}

static int run_descriptor_cleanup(void) {
    process_image_t permanent = {
        CUPIDC_EXEC_ARENA_START, 0x00001000u, PROCESS_IMAGE_PERMANENT, 0u};

    process_image_discard(&permanent);
    process_image_discard(&permanent);
    if (test_pmm_releases != 0u || test_last_release_base != 0u ||
        test_last_release_size != 0u || !descriptor_is_empty(&permanent)) {
        return 1;
    }
    return 0;
}

static int __attribute__((used)) contract_main(int argc, char **argv) {
    if (argc != 2) {
        return 64;
    }
    if (strings_equal(argv[1], "claim-busy-discard")) {
        return run_claim_busy_discard();
    }
    if (strings_equal(argv[1], "stale-discard")) {
        return run_stale_discard();
    }
    if (strings_equal(argv[1], "consume-kill")) {
        return run_consume_and_kill();
    }
    if (strings_equal(argv[1], "failed-consume")) {
        return run_failed_consume();
    }
    if (strings_equal(argv[1], "deferred-self-reap")) {
        return run_deferred_self_reap();
    }
    if (strings_equal(argv[1], "deferred-remote-reap")) {
        return run_deferred_remote_reap();
    }
    if (strings_equal(argv[1], "schedule-handoff")) {
        return run_schedule_handoff();
    }
    if (strings_equal(argv[1], "no-switch-unlock")) {
        return run_no_switch_unlock();
    }
    if (strings_equal(argv[1], "remote-idle-owned")) {
        return run_remote_idle_owned();
    }
    if (strings_equal(argv[1], "terminated-ap-idle-context")) {
        return run_terminated_ap_idle_context();
    }
    if (strings_equal(argv[1], "interrupt-defers-pending")) {
        return run_interrupt_defers_pending();
    }
    if (strings_equal(argv[1], "interrupt-direct-schedule-defers")) {
        return run_interrupt_direct_schedule_defers();
    }
    if (strings_equal(argv[1], "if-clear-handoff")) {
        return run_if_clear_handoff();
    }
    if (strings_equal(argv[1], "nested-schedule-defers")) {
        return run_nested_schedule_defers();
    }
    if (strings_equal(argv[1], "pending-on-outer-unlock")) {
        return run_pending_on_outer_unlock();
    }
    if (strings_equal(argv[1], "stack-canary-reap")) {
        return run_stack_canary_reap();
    }
    if (strings_equal(argv[1], "permanent-consume")) {
        return run_permanent_consume();
    }
    if (strings_equal(argv[1], "permanent-wrong-domain")) {
        return run_permanent_wrong_domain();
    }
    if (strings_equal(argv[1], "invalid-permanent-ranges")) {
        return run_invalid_permanent_ranges();
    }
    if (strings_equal(argv[1], "descriptor-cleanup")) {
        return run_descriptor_cleanup();
    }
    return 65;
}

#if defined(_WIN32)
int main(int argc, char **argv);

int main(int argc, char **argv) {
    return contract_main(argc, argv);
}
#else
void _start(void) __attribute__((naked, noreturn));

void _start(void) {
    __asm__("movl (%esp), %eax\n\t"
            "leal 4(%esp), %edx\n\t"
            "pushl %edx\n\t"
            "pushl %eax\n\t"
            "call contract_main\n\t"
            "movl %eax, %ebx\n\t"
            "movl $1, %eax\n\t"
            "int $0x80\n\t");
}
#endif
