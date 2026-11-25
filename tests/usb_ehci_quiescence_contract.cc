#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t command;
    uint32_t status;
    uint32_t elapsed_us;
    uint32_t halt_at_us;
    uint32_t halt_requests;
    uint32_t clear_writes_after_halt;
    bool stop_async_normally;
} ehci_ctrl_t;

static ehci_ctrl_t *controller;
static uint32_t ehci_op_read(ehci_ctrl_t *c, uint32_t reg);
static void ehci_op_write(ehci_ctrl_t *c, uint32_t reg, uint32_t value);
static void timer_delay_us(uint32_t delay);

#include "ehci_quiescence.inc"

static uint32_t ehci_op_read(ehci_ctrl_t *c, uint32_t reg) {
    return reg == EHCI_OP_USBCMD ? c->command : c->status;
}

static void ehci_op_write(ehci_ctrl_t *c, uint32_t reg, uint32_t value) {
    (void)reg;
    if ((c->command & EHCI_CMD_RUN) && !(value & EHCI_CMD_RUN)) {
        c->halt_requests++;
    }
    if ((c->status & EHCI_STS_HALTED) && !(value & EHCI_CMD_ASYNC_EN)) {
        c->clear_writes_after_halt++;
    }
    c->command = value;
}

static void timer_delay_us(uint32_t delay) {
    controller->elapsed_us += delay;
    if (controller->halt_requests && controller->elapsed_us >= controller->halt_at_us) {
        controller->status = EHCI_STS_HALTED;
    } else if (controller->stop_async_normally && !(controller->command & EHCI_CMD_ASYNC_EN)) {
        controller->status &= ~EHCI_STS_ASYNC;
    }
}

static ehci_ctrl_t request(uint32_t halt_at_us, bool stop_async_normally) {
    ehci_ctrl_t state = {
        .command = EHCI_CMD_RUN | EHCI_CMD_ASYNC_EN,
        .status = EHCI_STS_ASYNC,
        .halt_at_us = halt_at_us,
        .stop_async_normally = stop_async_normally,
    };
    return state;
}

int main(void) {
    ehci_ctrl_t fast = request(UINT32_MAX, true);
    controller = &fast;
    if (!ehci_quiesce_async(&fast) || fast.halt_requests != 0u || fast.elapsed_us > 50u) {
        (void)fprintf(stderr, "ordinary async stop did not complete directly\n");
        return 1;
    }

    ehci_ctrl_t stuck = request(UINT32_MAX, false);
    controller = &stuck;
    if (ehci_quiesce_async(&stuck) || stuck.halt_requests != 1u ||
        stuck.clear_writes_after_halt != 0u) {
        (void)fprintf(stderr, "unproved halt released DMA ownership\n");
        return 1;
    }

    ehci_ctrl_t delayed = request(EHCI_QUIESCE_TIMEOUT_US + 12000u, false);
    controller = &delayed;
    if (!ehci_quiesce_async(&delayed) || delayed.halt_requests != 1u ||
        delayed.clear_writes_after_halt != 1u || (delayed.status & EHCI_STS_ASYNC) ||
        !(delayed.status & EHCI_STS_HALTED)) {
        (void)fprintf(stderr, "delayed halt still unproved after %u microseconds\n",
                      delayed.elapsed_us);
        return 1;
    }
    return 0;
}
