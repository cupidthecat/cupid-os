#include "panic.h"
#include "kernel.h"
#include "string.h"
#include "math.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "memory.h"

/* Output function pointers (can be overridden for GUI mode) */
static void (*panic_print)(const char*) = print;
static void (*panic_putchar)(char) = putchar;

void panic_set_output(void (*print_fn)(const char*), void (*putchar_fn)(char)) {
    if (print_fn) panic_print = print_fn;
    if (putchar_fn) panic_putchar = putchar_fn;
}

/* ── helpers ──────────────────────────────────────────────────────── */

static void dual_print(const char *s) {
    panic_print(s);
    serial_write_string(s);
}

static void print_separator(void) {
    dual_print(
        "================================================================================\n");
}

/* print_hex_word – print a 16-bit value like 0x001F */
static void vga_hex_word(uint16_t num) {
    const char hex[] = "0123456789ABCDEF";
    panic_putchar('0'); panic_putchar('x');
    panic_putchar(hex[(num >> 12) & 0xF]);
    panic_putchar(hex[(num >> 8)  & 0xF]);
    panic_putchar(hex[(num >> 4)  & 0xF]);
    panic_putchar(hex[ num        & 0xF]);
}

/* print_hex_byte – print one byte as 2 hex digits */
static void vga_hex_byte(uint8_t num) {
    const char hex[] = "0123456789ABCDEF";
    panic_putchar(hex[(num >> 4) & 0xF]);
    panic_putchar(hex[ num       & 0xF]);
}

/* ── Register dump ────────────────────────────────────────────────── */

static void print_registers(struct registers *regs) {
    dual_print("\nREGISTERS:\n");

    /* EAX–EDX */
    serial_printf("  EAX: 0x%x  EBX: 0x%x  ECX: 0x%x  EDX: 0x%x\n",
                  regs->eax, regs->ebx, regs->ecx, regs->edx);
    panic_print("  EAX: "); print_hex(regs->eax);
    panic_print("  EBX: "); print_hex(regs->ebx);
    panic_print("  ECX: "); print_hex(regs->ecx);
    panic_print("  EDX: "); print_hex(regs->edx);
    panic_print("\n");

    /* ESI–ESP */
    serial_printf("  ESI: 0x%x  EDI: 0x%x  EBP: 0x%x  ESP: 0x%x\n",
                  regs->esi, regs->edi, regs->ebp, regs->esp);
    panic_print("  ESI: "); print_hex(regs->esi);
    panic_print("  EDI: "); print_hex(regs->edi);
    panic_print("  EBP: "); print_hex(regs->ebp);
    panic_print("  ESP: "); print_hex(regs->esp);
    panic_print("\n");

    /* EIP + EFLAGS */
    serial_printf("  EIP: 0x%x  EFLAGS: 0x%x\n", regs->eip, regs->eflags);
    panic_print("  EIP: "); print_hex(regs->eip);
    panic_print("  EFLAGS: "); print_hex(regs->eflags);
    panic_print("\n");

    /* Segments */
    serial_printf("  CS: 0x%x  DS: 0x%x  SS: 0x%x\n", regs->cs, regs->ds, regs->ss);
    panic_print("  CS: "); vga_hex_word((uint16_t)regs->cs);
    panic_print("  DS: "); vga_hex_word((uint16_t)regs->ds);
    panic_print("  SS: "); vga_hex_word((uint16_t)regs->ss);
    panic_print("\n");
}

/* ── Stack trace ──────────────────────────────────────────────────── */

void print_stack_trace(uint32_t ebp, uint32_t eip) {
    dual_print("\nSTACK TRACE:\n");

    serial_printf("  #0: 0x%x\n", eip);
    panic_print("  #0: "); print_hex(eip); print("\n");

    for (int i = 1; i < 10 && ebp != 0; i++) {
        if (ebp < 0x1000 || ebp > 0x190000) {
            dual_print("  (invalid stack frame)\n");
            break;
        }

        uint32_t ret_addr = *((uint32_t *)(ebp + 4));

        serial_printf("  #%d: 0x%x\n", i, ret_addr);
        panic_print("  #");
        print_int((uint32_t)i);
        panic_print(": ");
        print_hex(ret_addr);
        panic_print("\n");

        uint32_t prev_ebp = *((uint32_t *)ebp);
        if (prev_ebp <= ebp) break;
        ebp = prev_ebp;
    }
}

/* ── Stack hex dump ───────────────────────────────────────────────── */

static void print_stack_dump(uint32_t esp) {
    dual_print("\nSTACK DUMP:\n");

    for (int i = 0; i < 128; i += 16) {
        uint32_t addr = esp + (uint32_t)i;

        serial_printf("  0x%x: ", addr);
        panic_print("  "); print_hex(addr); print(": ");

        for (int j = 0; j < 16; j++) {
            uint8_t b = *((uint8_t *)(addr + (uint32_t)j));
            serial_printf("%c%c ",
                          "0123456789abcdef"[(b >> 4) & 0xF],
                          "0123456789abcdef"[b & 0xF]);
            vga_hex_byte(b);
            panic_putchar(' ');
        }
        dual_print("\n");
    }
}

/* ── System state summary ─────────────────────────────────────────── */

static void print_system_state(void) {
    dual_print("\nSYSTEM STATE:\n");

    uint32_t ms = timer_get_uptime_ms();
    serial_printf("  Uptime: %u.%us\n", ms / 1000, ms % 1000);
    panic_print("  Uptime: "); print_int(ms / 1000); panic_putchar('.');
    print_int(ms % 1000); print("s\n");

    uint32_t free_pg  = pmm_free_pages();
    uint32_t total_pg = pmm_total_pages();
    serial_printf("  Memory: %u KB free / %u KB total\n",
                  free_pg * 4, total_pg * 4);
    panic_print("  Memory: "); print_int(free_pg * 4); print(" KB free / ");
    print_int(total_pg * 4); print(" KB total\n");
}

/* ── kernel_panic (no register context) ───────────────────────────── */

void kernel_panic(const char *fmt, ...) {
    __asm__ volatile("cli");

    print_separator();
    dual_print("                          KERNEL PANIC\n");
    print_separator();

    /* Print message to both outputs */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    /* VGA – simplified (just print the raw format for reliability) */
    panic_print(fmt);
    panic_print("\n");

    __builtin_va_end(ap);

    /* Serial – formatted */
    __builtin_va_start(ap, fmt);
    serial_write_string("[PANIC] ");
    /* We re-do the format expansion via serial_printf
       but va_list forwarding across vfuncs is tricky in
       freestanding C.  Print the fmt string literally for
       reliability, then add a stack trace. */
    serial_write_string(fmt);
    serial_write_char('\n');
    __builtin_va_end(ap);

    /* Grab current EBP/EIP for a trace */
    uint32_t ebp, eip;
    __asm__ volatile("movl %%ebp, %0" : "=r"(ebp));
    __asm__ volatile("call 1f\n1: popl %0" : "=r"(eip));
    print_stack_trace(ebp, eip);

    print_system_state();

    dual_print("\nSystem halted. Press reset to reboot.\n");
    print_separator();

    while (1) { __asm__ volatile("hlt"); }
}

/* ── kernel_panic_regs (with register context from ISR) ───────────── */

void kernel_panic_regs(struct registers *regs, const char *fmt, ...) {
    __asm__ volatile("cli");

    print_separator();
    dual_print("                          KERNEL PANIC\n");
    print_separator();

    /* message */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    panic_print(fmt);
    panic_print("\n");
    serial_write_string("[PANIC] ");
    serial_write_string(fmt);
    serial_write_char('\n');
    __builtin_va_end(ap);

    if (regs) {
        print_registers(regs);
        print_stack_trace(regs->ebp, regs->eip);
        print_stack_dump(regs->esp);
    }

    print_system_state();

    dual_print("\nSystem halted. Press reset to reboot.\n");
    print_separator();

    while (1) { __asm__ volatile("hlt"); }
}
