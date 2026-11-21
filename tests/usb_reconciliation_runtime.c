#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Use the host's exact-width and size types while exercising the real USB
 * core. Kernel headers see their normal declarations apart from types.h.
 */
#define TYPES_H
typedef struct {
    uint64_t start_tick;
    uint64_t duration_ms;
} timer_measure_t;

#include "../kernel/usb/usb.cc"

enum mock_failure {
    FAIL_NONE = 0,
    FAIL_FIRST_DESCRIPTOR,
    FAIL_SET_ADDRESS,
    FAIL_FULL_DESCRIPTOR,
    FAIL_SHORT_CONFIGURATION,
    FAIL_FULL_CONFIGURATION,
    FAIL_SET_CONFIGURATION
};

static usb_hc_t mock_hc;
static usb_setup_t mock_setup;
static enum mock_failure mock_failure;
static bool mock_connected;
static bool mock_fill_queue;
static int mock_reset_failures;
static bool mock_reset_handoff;
static bool mock_reset_must_reset;
static int mock_reset_calls;
static int mock_port_zero_status_calls;
static int mock_disconnect_failures;
static int mock_probe_retry_failures;
static int mock_probe_calls;
static usb_probe_result_t mock_probe_result;
static int fallback_probe_calls;
static int mock_raw_submit_failures;
static int mock_submit_calls;
static uint32_t mock_time_ms;
static uint8_t mock_hub_address;
static bool mock_hub_connected;
static bool mock_hub_change;
static bool mock_fail_hub_clear;
static int mock_hub_reset_requests;

static uint32_t queue_count(void) {
    return (
        workq_head + USB_WORKQ_SIZE - workq_tail
    ) % USB_WORKQ_SIZE;
}

static uint32_t reserved_address_count(void) {
    uint32_t count = 0;
    for (uint32_t address = 1; address <= 127u; address++) {
        if (usb_address_reserved[address]) count++;
    }
    return count;
}

static bool queue_contains(usb_hc_t *hc, int port) {
    for (
        uint32_t cursor = workq_tail;
        cursor != workq_head;
        cursor = (cursor + 1u) % USB_WORKQ_SIZE
    ) {
        if (workq[cursor].hc == hc && workq[cursor].port == port) {
            return true;
        }
    }
    return false;
}

static void run_next_retry(void) {
    if (workq_tail != workq_head) {
        mock_time_ms = workq[workq_tail].retry_after_ms;
    }
    usb_process_pending();
}

static bool request_fails(const usb_transfer_t *transfer) {
    uint8_t descriptor_type = (uint8_t)(mock_setup.wValue >> 8);
    if (
        mock_fail_hub_clear
        && transfer->device_addr == mock_hub_address
        && mock_setup.bRequest == USB_HUB_CLEAR_FEATURE
        && mock_setup.wValue == USB_HUB_C_PORT_CONNECTION
    ) {
        return true;
    }
    if (mock_failure == FAIL_FIRST_DESCRIPTOR) {
        return mock_setup.bRequest == 0x06u
            && descriptor_type == 0x01u
            && mock_setup.wLength == 8u
            && transfer->device_addr == 0u;
    }
    if (mock_failure == FAIL_SET_ADDRESS) {
        return mock_setup.bRequest == 0x05u
            || (
                mock_setup.bRequest == 0x06u
                && descriptor_type == 0x01u
                && mock_setup.wLength == 8u
                && transfer->device_addr != 0u
            );
    }
    if (mock_failure == FAIL_FULL_DESCRIPTOR) {
        return mock_setup.bRequest == 0x06u
            && descriptor_type == 0x01u
            && mock_setup.wLength == 18u;
    }
    if (mock_failure == FAIL_SHORT_CONFIGURATION) {
        return mock_setup.bRequest == 0x06u
            && descriptor_type == 0x02u
            && mock_setup.wLength == 9u;
    }
    if (mock_failure == FAIL_FULL_CONFIGURATION) {
        return mock_setup.bRequest == 0x06u
            && descriptor_type == 0x02u
            && mock_setup.wLength == 18u;
    }
    if (mock_failure == FAIL_SET_CONFIGURATION) {
        return mock_setup.bRequest == 0x09u;
    }
    return false;
}

static void fill_control_data(usb_transfer_t *transfer) {
    uint8_t descriptor_type = (uint8_t)(mock_setup.wValue >> 8);
    if (mock_setup.bRequest == 0x06u && descriptor_type == 0x01u) {
        uint8_t *data = transfer->buffer;
        for (uint32_t i = 0; i < transfer->length; i++) data[i] = 0u;
        if (transfer->length >= 8u) data[7] = 8u;
        if (transfer->length >= 12u) {
            data[8] = 0x34u;
            data[9] = 0x12u;
            data[10] = 0x78u;
            data[11] = 0x56u;
        }
    } else if (
        mock_setup.bRequest == 0x06u
        && descriptor_type == 0x02u
    ) {
        uint8_t *data = transfer->buffer;
        for (uint32_t i = 0; i < transfer->length; i++) data[i] = 0u;
        if (transfer->length >= 9u) {
            data[0] = 9u;
            data[1] = 2u;
            data[2] = 18u;
            data[4] = 1u;
            data[5] = 1u;
        }
        if (transfer->length >= 18u) {
            data[9] = 9u;
            data[10] = 4u;
            data[14] = 0xFFu;
        }
    } else if (mock_setup.bRequest == 0x08u && transfer->length > 0u) {
        transfer->buffer[0] = 0u;
    } else if (
        mock_setup.bRequest == USB_HUB_GET_STATUS
        && transfer->device_addr == mock_hub_address
        && transfer->length >= 4u
    ) {
        transfer->buffer[0] = mock_hub_connected ? 1u : 0u;
        transfer->buffer[1] = 0u;
        transfer->buffer[2] = (uint8_t)(
            (mock_hub_change ? 1u : 0u)
            | (mock_hub_connected ? 0x10u : 0u)
        );
        transfer->buffer[3] = 0u;
    }
}

static int mock_submit_sync(
    usb_hc_t *hc,
    usb_transfer_t *transfer,
    uint32_t timeout_ms
) {
    (void)hc;
    (void)timeout_ms;
    mock_submit_calls++;
    if (mock_raw_submit_failures > 0) {
        mock_raw_submit_failures--;
        return -1;
    }
    if (transfer->dir == USB_DIR_SETUP) {
        uint8_t *source = transfer->buffer;
        mock_setup.bmRequestType = source[0];
        mock_setup.bRequest = source[1];
        mock_setup.wValue = (uint16_t)(source[2] | (uint16_t)(source[3] << 8));
        mock_setup.wIndex = (uint16_t)(source[4] | (uint16_t)(source[5] << 8));
        mock_setup.wLength = (uint16_t)(source[6] | (uint16_t)(source[7] << 8));
        if (request_fails(transfer)) return -1;
        if (
            transfer->device_addr == mock_hub_address
            && mock_setup.bRequest == USB_HUB_SET_FEATURE
            && mock_setup.wValue == USB_HUB_PORT_RESET
        ) {
            mock_hub_reset_requests++;
        }
        if (
            transfer->device_addr == mock_hub_address
            && mock_setup.bRequest == USB_HUB_CLEAR_FEATURE
            && mock_setup.wValue == USB_HUB_C_PORT_CONNECTION
        ) {
            mock_hub_change = false;
        }
        return 0;
    }
    if (request_fails(transfer)) return -1;
    if (transfer->dir == USB_DIR_IN && transfer->length > 0u) {
        fill_control_data(transfer);
    }
    return 0;
}

static int mock_port_status(
    usb_hc_t *hc,
    int port,
    uint32_t *status
) {
    if (port == 0) mock_port_zero_status_calls++;
    if (mock_fill_queue && port == 0) {
        for (int queued_port = 1; queued_port < 32; queued_port++) {
            (void)usb_port_change(hc, queued_port);
        }
        return -1;
    }
    *status = port == 0 && mock_connected ? 1u : 0u;
    return 0;
}

static usb_port_reset_result_t mock_port_reset(
    usb_hc_t *hc,
    int port,
    bool must_reset
) {
    (void)hc;
    (void)port;
    mock_reset_calls++;
    mock_reset_must_reset = must_reset;
    if (mock_reset_handoff) return USB_PORT_RESET_HANDOFF;
    if (mock_reset_failures > 0) {
        mock_reset_failures--;
        return USB_PORT_RESET_FAILED;
    }
    return USB_PORT_RESET_OK;
}

static usb_probe_result_t mock_probe(usb_device_t *dev) {
    (void)dev;
    mock_probe_calls++;
    if (mock_probe_retry_failures > 0) {
        mock_probe_retry_failures--;
        return USB_PROBE_RETRY;
    }
    return mock_probe_result;
}

static usb_probe_result_t fallback_probe(usb_device_t *dev) {
    (void)dev;
    fallback_probe_calls++;
    return USB_PROBE_BOUND;
}

static int mock_disconnect(usb_device_t *dev) {
    (void)dev;
    if (mock_disconnect_failures > 0) {
        mock_disconnect_failures--;
        return -1;
    }
    return 0;
}

static usb_driver_t mock_driver = {
    "reconciliation-test",
    mock_probe,
    mock_disconnect,
    NULL
};

static usb_driver_t fallback_driver = {
    "fallback-test",
    fallback_probe,
    mock_disconnect,
    NULL
};

static void reset_mock(void) {
    usb_init();
    mock_failure = FAIL_NONE;
    mock_connected = false;
    mock_fill_queue = false;
    mock_reset_failures = 0;
    mock_reset_handoff = false;
    mock_reset_must_reset = false;
    mock_reset_calls = 0;
    mock_port_zero_status_calls = 0;
    mock_disconnect_failures = 0;
    mock_probe_retry_failures = 0;
    mock_probe_calls = 0;
    mock_probe_result = USB_PROBE_BOUND;
    fallback_probe_calls = 0;
    mock_raw_submit_failures = 0;
    mock_submit_calls = 0;
    mock_time_ms = 0u;
    mock_hub_address = 0u;
    mock_hub_connected = false;
    mock_hub_change = false;
    mock_fail_hub_clear = false;
    mock_hub_reset_requests = 0;
    mock_hc.name = "mock";
    mock_hc.driver_data = NULL;
    mock_hc.root_speed = USB_SPEED_FULL;
    mock_hc.submit_sync = mock_submit_sync;
    mock_hc.submit_interrupt = NULL;
    mock_hc.cancel_interrupt = NULL;
    mock_hc.port_count = NULL;
    mock_hc.port_status = mock_port_status;
    mock_hc.port_reset = mock_port_reset;
    mock_hc.irq_handler = NULL;
    (void)usb_register_hc(&mock_hc);
}

static int test_reentrant_full_queue_retains_retry(void) {
    reset_mock();
    mock_fill_queue = true;
    if (!usb_port_change(&mock_hc, 0)) return 10;
    usb_process_pending();
    if (mock_port_zero_status_calls != 1) return 11;
    if (queue_count() != USB_WORKQ_SIZE - 1u) return 12;
    if (!queue_contains(&mock_hc, 0)) return 13;

    mock_fill_queue = false;
    usb_process_pending();
    if (queue_count() != 1u || mock_port_zero_status_calls != 1) return 14;
    run_next_retry();
    if (queue_count() != 0u || mock_port_zero_status_calls != 2) return 15;
    return 0;
}

static int test_reset_and_allocation_retry_once_per_poll(void) {
    reset_mock();
    mock_connected = true;
    mock_reset_failures = 2;
    if (!usb_port_change(&mock_hc, 0)) return 20;
    usb_process_pending();
    if (mock_reset_calls != 1 || queue_count() != 1u) return 21;
    if (workq[workq_tail].retry_delay_ms != USB_WORK_RETRY_MIN_MS) return 26;
    usb_process_pending();
    if (mock_reset_calls != 1 || queue_count() != 1u) return 27;
    run_next_retry();
    if (mock_reset_calls != 2 || queue_count() != 1u) return 28;
    if (workq[workq_tail].retry_delay_ms != 20u) return 29;
    usb_process_pending();
    if (mock_reset_calls != 2 || queue_count() != 1u) return 53;
    run_next_retry();
    if (
        mock_reset_calls != 3
        || usb_device_count() != 1
        || queue_count() != 0u
    ) {
        return 22;
    }

    reset_mock();
    mock_connected = true;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        devices[i].hc = NULL;
        devices[i].parent_hub = &devices[i];
        devices[i].in_use = true;
    }
    if (!usb_port_change(&mock_hc, 0)) return 23;
    usb_process_pending();
    if (queue_count() != 1u || reserved_address_count() != 0u) return 24;
    devices[0].in_use = false;
    run_next_retry();
    if (usb_device_count() != USB_MAX_DEVICES) return 25;
    return 0;
}

static int test_companion_handoff_completes_current_controller_work(void) {
    reset_mock();
    mock_connected = true;
    mock_reset_handoff = true;
    if (!usb_port_change(&mock_hc, 0)) return 54;
    usb_process_pending();
    if (mock_reset_calls != 1) return 55;
    if (queue_count() != 0u) return 56;
    if (usb_device_count() != 0) return 57;
    if (reserved_address_count() != 0u) return 58;
    if (mock_reset_must_reset) return 99;
    return 0;
}

static int test_quarantined_handoff_requires_a_proved_reset(void) {
    reset_mock();
    mock_connected = true;
    mock_reset_handoff = true;
    uint8_t quarantined = alloc_address();
    if (quarantined == 0u) return 100;
    if (!usb_port_change(&mock_hc, 0)) return 101;
    workq[workq_tail].quarantined_address = quarantined;
    usb_process_pending();
    if (mock_reset_calls != 1 || !mock_reset_must_reset) return 102;
    if (queue_count() != 0u || reserved_address_count() != 0u) return 103;
    return 0;
}

static int test_retryable_probe_keeps_port_work_until_binding_succeeds(void) {
    reset_mock();
    (void)usb_register_driver(&mock_driver);
    mock_connected = true;
    mock_probe_retry_failures = 1;

    if (!usb_port_change(&mock_hc, 0)) return 59;
    usb_process_pending();
    if (mock_probe_calls != 1) return 63;
    if (usb_device_count() != 0) return 64;
    if (queue_count() != 1u) return 65;
    if (reserved_address_count() != 1u) return 66;

    run_next_retry();
    if (mock_probe_calls != 2) return 67;
    if (usb_device_count() != 1) return 68;
    if (queue_count() != 0u) return 69;
    if (reserved_address_count() != 1u) return 79;
    return 0;
}

static int test_not_supported_probe_allows_the_next_driver(void) {
    reset_mock();
    (void)usb_register_driver(&fallback_driver);
    (void)usb_register_driver(&mock_driver);
    mock_connected = true;
    mock_probe_result = USB_PROBE_NOT_SUPPORTED;

    if (!usb_port_change(&mock_hc, 0)) return 80;
    usb_process_pending();
    if (mock_probe_calls != 1 || fallback_probe_calls != 1) return 81;
    usb_device_t *dev = usb_get_device(0);
    if (!dev || dev->driver != &fallback_driver) return 82;
    if (queue_count() != 0u) return 83;
    return 0;
}

static int test_rejected_probe_stops_before_a_fallback_driver(void) {
    reset_mock();
    (void)usb_register_driver(&fallback_driver);
    (void)usb_register_driver(&mock_driver);
    mock_connected = true;
    mock_probe_result = USB_PROBE_REJECTED;

    if (!usb_port_change(&mock_hc, 0)) return 84;
    usb_process_pending();
    if (mock_probe_calls != 1 || fallback_probe_calls != 0) return 85;
    usb_device_t *dev = usb_get_device(0);
    if (!dev || dev->driver != NULL) return 86;
    if (queue_count() != 0u) return 87;
    return 0;
}

static int test_invalid_probe_result_retries_without_publishing(void) {
    reset_mock();
    (void)usb_register_driver(&mock_driver);
    mock_connected = true;
    mock_probe_result = (usb_probe_result_t)99;

    if (!usb_port_change(&mock_hc, 0)) return 92;
    usb_process_pending();
    if (mock_probe_calls != 1) return 93;
    if (usb_device_count() != 0) return 94;
    if (queue_count() != 1u) return 95;
    if (reserved_address_count() != 1u) return 96;

    mock_probe_result = USB_PROBE_BOUND;
    run_next_retry();
    if (mock_probe_calls != 2) return 97;
    if (usb_device_count() != 1 || queue_count() != 0u) return 98;
    return 0;
}

static int test_public_control_retry_has_a_bounded_retry_window(void) {
    reset_mock();
    usb_device_t dev = {0};
    dev.hc = &mock_hc;
    dev.speed = USB_SPEED_FULL;
    dev.max_packet_ep0 = 8u;

    mock_raw_submit_failures = 2;
    if (
        usb_control_retry(
            &dev,
            0x00u,
            0x01u,
            0u,
            0u,
            NULL,
            0u
        ) < 0
    ) {
        return 88;
    }
    if (mock_submit_calls != 4 || mock_time_ms != 20u) return 89;

    reset_mock();
    dev.hc = &mock_hc;
    dev.speed = USB_SPEED_FULL;
    dev.max_packet_ep0 = 8u;
    mock_raw_submit_failures = (int)USB_CONTROL_ATTEMPTS;
    if (
        usb_control_retry(
            &dev,
            0x00u,
            0x01u,
            0u,
            0u,
            NULL,
            0u
        ) >= 0
    ) {
        return 90;
    }
    if (
        mock_submit_calls != (int)USB_CONTROL_ATTEMPTS
        || mock_time_ms != 40u
    ) {
        return 91;
    }
    return 0;
}

static int test_control_failures_unwind_and_retry(void) {
    enum mock_failure failures[] = {
        FAIL_FIRST_DESCRIPTOR,
        FAIL_SET_ADDRESS,
        FAIL_FULL_DESCRIPTOR,
        FAIL_SHORT_CONFIGURATION,
        FAIL_FULL_CONFIGURATION,
        FAIL_SET_CONFIGURATION
    };
    for (
        uint32_t i = 0;
        i < (uint32_t)(sizeof(failures) / sizeof(failures[0]));
        i++
    ) {
        reset_mock();
        mock_connected = true;
        mock_failure = failures[i];
        if (!usb_port_change(&mock_hc, 0)) return 30;
        usb_process_pending();
        if (usb_device_count() != 0) return 31;
        uint32_t expected_reserved =
            failures[i] == FAIL_FIRST_DESCRIPTOR ? 0u : 1u;
        if (reserved_address_count() != expected_reserved) return 32;
        if (queue_count() != 1u) return 33;
        if (
            expected_reserved != 0u
            && (
                workq[workq_tail].quarantined_address == 0u
                || !usb_address_reserved[
                    workq[workq_tail].quarantined_address
                ]
            )
        ) {
            return 36;
        }
        if (expected_reserved != 0u) {
            uint8_t quarantined =
                workq[workq_tail].quarantined_address;
            uint8_t another = alloc_address();
            if (another == 0u || another == quarantined) return 37;
            release_address(another);
        }

        mock_failure = FAIL_NONE;
        run_next_retry();
        if (usb_device_count() != 1) return 34;
        if (reserved_address_count() != 1u) return 35;
    }
    return 0;
}

static int test_disconnect_retry_and_address_reuse(void) {
    reset_mock();
    (void)usb_register_driver(&mock_driver);
    mock_connected = true;
    if (!usb_port_change(&mock_hc, 0)) return 40;
    usb_process_pending();
    if (usb_device_count() != 1 || reserved_address_count() != 1u) return 41;

    mock_connected = false;
    mock_disconnect_failures = 1;
    if (!usb_port_change(&mock_hc, 0)) return 42;
    usb_process_pending();
    if (usb_device_count() != 1 || queue_count() != 1u) return 43;
    if (reserved_address_count() != 1u) return 44;
    run_next_retry();
    if (usb_device_count() != 0 || queue_count() != 0u) return 45;
    if (reserved_address_count() != 0u) return 46;

    reset_mock();
    uint8_t first_address = 0u;
    uint8_t reused_address = 0u;
    for (uint32_t cycle = 0; cycle < 128u; cycle++) {
        mock_connected = true;
        if (!usb_port_change(&mock_hc, 0)) return 47;
        usb_process_pending();
        usb_device_t *dev = usb_get_device(0);
        if (!dev) return 48;
        if (cycle == 0u) first_address = dev->address;
        if (cycle == 127u) reused_address = dev->address;

        mock_connected = false;
        if (!usb_port_change(&mock_hc, 0)) return 49;
        usb_process_pending();
        if (usb_device_count() != 0) return 50;
        if (reserved_address_count() != 0u) return 51;
    }
    return first_address == 1u && reused_address == first_address ? 0 : 52;
}

static int test_backoff_resets_on_observed_state_change(void) {
    usb_work_t work = {0};
    observe_port_state(&work, true);
    defer_work(&work);
    defer_work(&work);
    if (work.retry_delay_ms != 20u) return 60;
    observe_port_state(&work, false);
    if (work.retry_delay_ms != 0u || work.retry_after_ms != 0u) return 61;

    for (int i = 0; i < 16; i++) defer_work(&work);
    return work.retry_delay_ms == USB_WORK_RETRY_MAX_MS ? 0 : 62;
}

static int test_hub_ack_retry_keeps_the_reconciled_child(void) {
    reset_mock();
    usb_device_t *hub = alloc_device_slot();
    if (!hub) return 70;
    mock_hub_address = alloc_address();
    if (mock_hub_address == 0u) return 71;
    hub->address = mock_hub_address;
    hub->hc = &mock_hc;
    hub->speed = USB_SPEED_HIGH;
    hub->max_packet_ep0 = 8u;
    hub->class_code = 0x09u;
    hub->hub_depth = 0u;

    mock_hub_connected = true;
    mock_hub_change = true;
    mock_fail_hub_clear = true;
    if (!usb_hub_port_change(hub, 1)) return 72;
    usb_process_pending();
    if (queue_count() != 1u || usb_device_count() != 2) return 73;
    if (!workq[workq_tail].reconciled) return 74;
    if (mock_hub_reset_requests != 1) return 75;

    usb_process_pending();
    if (usb_device_count() != 2 || mock_hub_reset_requests != 1) return 76;

    mock_fail_hub_clear = false;
    run_next_retry();
    if (queue_count() != 0u || usb_device_count() != 2) return 77;
    if (mock_hub_reset_requests != 1 || mock_hub_change) return 78;
    return 0;
}

void klog(log_level_t level, const char *format, ...) {
    (void)level;
    (void)format;
}

void timer_delay_us(uint32_t us) {
    mock_time_ms += (us + 999u) / 1000u;
}

uint32_t timer_get_uptime_ms(void) {
    return mock_time_ms;
}

void irq_install_handler(int irq, irq_handler_t handler) {
    (void)irq;
    (void)handler;
}

void ehci_poll_ports(void) {
}

void ehci_poll_interrupts(void) {
}

void uhci_poll_ports(void) {
}

void uhci_poll_interrupts(void) {
}

int main(void) {
    int result = test_reentrant_full_queue_retains_retry();
    if (result != 0) return result;
    result = test_reset_and_allocation_retry_once_per_poll();
    if (result != 0) return result;
    result = test_companion_handoff_completes_current_controller_work();
    if (result != 0) return result;
    result = test_quarantined_handoff_requires_a_proved_reset();
    if (result != 0) return result;
    result = test_retryable_probe_keeps_port_work_until_binding_succeeds();
    if (result != 0) return result;
    result = test_not_supported_probe_allows_the_next_driver();
    if (result != 0) return result;
    result = test_rejected_probe_stops_before_a_fallback_driver();
    if (result != 0) return result;
    result = test_invalid_probe_result_retries_without_publishing();
    if (result != 0) return result;
    result = test_public_control_retry_has_a_bounded_retry_window();
    if (result != 0) return result;
    result = test_control_failures_unwind_and_retry();
    if (result != 0) return result;
    result = test_disconnect_retry_and_address_reuse();
    if (result != 0) return result;
    result = test_backoff_resets_on_observed_state_change();
    if (result != 0) return result;
    return test_hub_ack_retry_keeps_the_reconciled_child();
}
