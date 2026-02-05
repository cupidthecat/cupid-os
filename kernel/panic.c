#include "panic.h"
#include "kernel.h"
#include "string.h"
#include "math.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "memory.h"

/* ── helpers ──────────────────────────────────────────────────────── */

static void dual_print(const char *s) {
    print(s);
    serial_write_string(s);
}

static void print_separator(void) {
    dual_print(
        "================================================================================\n");
}

/* print_hex_word – print a 16-bit value like 0x001F */
static void vga_hex_word(uint16_t num) {
    const char hex[] = "0123456789ABCDEF";
    putchar('0'); putchar('x');
    putchar(hex[(num >> 12) & 0xF]);
    putchar(hex[(num >> 8)  & 0xF]);
    putchar(hex[(num >> 4)  & 0xF]);
    putchar(hex[ num        & 0xF]);
}

/* print_hex_byte – print one byte as 2 hex digits */
static void vga_hex_byte(uint8_t num) {
    const char hex[] = "0123456789ABCDEF";
    putchar(hex[(num >> 4) & 0xF]);
    putchar(hex[ num       & 0xF]);
}

/* ── Register dump ────────────────────────────────────────────────── */

static void print_registers(struct registers *regs) {
    dual_print("\nREGISTERS:\n");

    /* EAX–EDX */
    serial_printf("  EAX: 0x%x  EBX: 0x%x  ECX: 0x%x  EDX: 0x%x\n",
                  regs->eax, regs->ebx, regs->ecx, regs->edx);
    print("  EAX: "); print_hex(regs->eax);
    print("  EBX: "); print_hex(regs->ebx);
    print("  ECX: "); print_hex(regs->ecx);
    print("  EDX: "); print_hex(regs->edx);
    print("\n");

    /* ESI–ESP */
    serial_printf("  ESI: 0x%x  EDI: 0x%x  EBP: 0x%x  ESP: 0x%x\n",
                  regs->esi, regs->edi, regs->ebp, regs->esp);
    print("  ESI: "); print_hex(regs->esi);
    print("  EDI: "); print_hex(regs->edi);
    print("  EBP: "); print_hex(regs->ebp);
    print("  ESP: "); print_hex(regs->esp);
    print("\n");

    /* EIP + EFLAGS */
    serial_printf("  EIP: 0x%x  EFLAGS: 0x%x\n", regs->eip, regs->eflags);
    print("  EIP: "); print_hex(regs->eip);
    print("  EFLAGS: "); print_hex(regs->eflags);
    print("\n");

    /* Segments */
    serial_printf("  CS: 0x%x  DS: 0x%x  SS: 0x%x\n", regs->cs, regs->ds, regs->ss);
    print("  CS: "); vga_hex_word((uint16_t)regs->cs);
    print("  DS: "); vga_hex_word((uint16_t)regs->ds);
    print("  SS: "); vga_hex_word((uint16_t)regs->ss);
    print("\n");
}

/* ── Stack trace ──────────────────────────────────────────────────── */

void print_stack_trace(uint32_t ebp, uint32_t eip) {
    dual_print("\nSTACK TRACE:\n");

    serial_printf("  #0: 0x%x\n", eip);
    print("  #0: "); print_hex(eip); print("\n");

    for (int i = 1; i < 10 && ebp != 0; i++) {
        if (ebp < 0x1000 || ebp > 0x90000) {
            dual_print("  (invalid stack frame)\n");
            break;
        }

        uint32_t ret_addr = *((uint32_t *)(ebp + 4));

        serial_printf("  #%d: 0x%x\n", i, ret_addr);
        print("  #");
        print_int((uint32_t)i);
        print(": ");
        print_hex(ret_addr);
        print("\n");

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
        print("  "); print_hex(addr); print(": ");

        for (int j = 0; j < 16; j++) {
            uint8_t b = *((uint8_t *)(addr + (uint32_t)j));
            serial_printf("%c%c ",
                          "0123456789abcdef"[(b >> 4) & 0xF],
                          "0123456789abcdef"[b & 0xF]);
            vga_hex_byte(b);
            putchar(' ');
        }
        dual_print("\n");
    }
}

/* ── System state summary ─────────────────────────────────────────── */

static void print_system_state(void) {
    dual_print("\nSYSTEM STATE:\n");

    uint32_t ms = timer_get_uptime_ms();
    serial_printf("  Uptime: %u.%us\n", ms / 1000, ms % 1000);
    print("  Uptime: "); print_int(ms / 1000); putchar('.');
    print_int(ms % 1000); print("s\n");

    uint32_t free_pg  = pmm_free_pages();
    uint32_t total_pg = pmm_total_pages();
    serial_printf("  Memory: %u KB free / %u KB total\n",
                  free_pg * 4, total_pg * 4);
    print("  Memory: "); print_int(free_pg * 4); print(" KB free / ");
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
    print(fmt);
    print("\n");

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
    print(fmt);
    print("\n");
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
