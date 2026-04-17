#include "isr.h"
#include "irq.h"
#include "pic.h"
#include "math.h"
#include "lapic.h"
#include "ioapic.h"

#define IRQ_MAX_HANDLERS 4

/* Per-IRQ handler chain. Multiple handlers per line so devices on a
 * shared GSI (PCI INTx, legacy ISA overlap) all get dispatched. */
static irq_handler_t irq_handlers[16][IRQ_MAX_HANDLERS];

// Install a custom IRQ handler (appends to chain for shared IRQs)
void irq_install_handler(int irq, irq_handler_t handler) {
    int slot;
    if (irq < 0 || irq >= 16 || !handler) return;

    /* Append into first free slot. Silently refuse dup registrations
     * of the same fn pointer. */
    for (slot = 0; slot < IRQ_MAX_HANDLERS; slot++) {
        if (irq_handlers[irq][slot] == handler) return;
    }
    for (slot = 0; slot < IRQ_MAX_HANDLERS; slot++) {
        if (!irq_handlers[irq][slot]) {
            irq_handlers[irq][slot] = handler;
            break;
        }
    }
    if (slot == IRQ_MAX_HANDLERS) {
        print("IRQ chain full, handler dropped\n");
        return;
    }

    /* Unmask the corresponding GSI on the IOAPIC */
    {
        uint32_t gsi = ioapic_irq_to_gsi((uint8_t)irq);
        ioapic_unmask_gsi(gsi);
    }

    print("IRQ handler installed for IRQ ");
    /* Add code to print irq number */
    print("\n");
}

// Remove an IRQ handler (clears entire chain)
void irq_uninstall_handler(int irq) {
    int i;
    if (irq < 0 || irq >= 16) return;
    for (i = 0; i < IRQ_MAX_HANDLERS; i++) irq_handlers[irq][i] = 0;
}

static void default_irq_handler(struct registers* r) {
    print("Unhandled IRQ: ");
    char irq_str[3];
    itoa((int)(r->int_no - 32U), irq_str);
    print(irq_str);
    print("\n");
}

// IRQ handler dispatcher — walks full chain so shared lines work
void irq_handler(struct registers* r) {
    int irq = (int)(r->int_no - 32U);
    int i;
    int dispatched = 0;

    if (irq >= 0 && irq < 16) {
        for (i = 0; i < IRQ_MAX_HANDLERS; i++) {
            if (irq_handlers[irq][i]) {
                irq_handlers[irq][i](r);
                dispatched = 1;
            }
        }
    }
    if (!dispatched) default_irq_handler(r);

    /* Send EOI via Local APIC (8259 is fully masked) */
    lapic_eoi();
}

void irq_list_handlers(void) {
    int i, j;
    print("Installed IRQ Handlers:\n");
    for (i = 0; i < 16; i++) {
        for (j = 0; j < IRQ_MAX_HANDLERS; j++) {
            if (irq_handlers[i][j]) {
                print("IRQ");
                char num[3];
                itoa(i, num);
                print(num);
                print(": 0x");
                print_hex((uint32_t)irq_handlers[i][j]);
                print("\n");
            }
        }
    }
}

void irq_init(void) {
    int i, j;
    for (i = 0; i < 16; i++)
        for (j = 0; j < IRQ_MAX_HANDLERS; j++) irq_handlers[i][j] = 0;
    print("IRQ subsystem initialized\n");
}
