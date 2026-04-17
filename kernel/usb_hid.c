#include "usb.h"
#include "memory.h"
#include "../drivers/serial.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"

#define HID_CLASS 0x03
#define HID_SUBCLASS_BOOT 0x01
#define HID_PROTO_KBD   0x01
#define HID_PROTO_MOUSE 0x02

#define HID_SET_PROTOCOL 0x0B
#define HID_SET_IDLE     0x0A

static const uint8_t hid_to_ps2[256] = {
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E,
    [0x07] = 0x20, [0x08] = 0x12, [0x09] = 0x21,
    [0x0A] = 0x22, [0x0B] = 0x23, [0x0C] = 0x17,
    [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
    [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18,
    [0x13] = 0x19, [0x14] = 0x10, [0x15] = 0x13,
    [0x16] = 0x1F, [0x17] = 0x14, [0x18] = 0x16,
    [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C,
    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04,
    [0x21] = 0x05, [0x22] = 0x06, [0x23] = 0x07,
    [0x24] = 0x08, [0x25] = 0x09, [0x26] = 0x0A,
    [0x27] = 0x0B,
    [0x28] = 0x1C, [0x29] = 0x01,
    [0x2A] = 0x0E, [0x2B] = 0x0F,
    [0x2C] = 0x39, [0x2D] = 0x0C,
    [0x2E] = 0x0D, [0x2F] = 0x1A,
    [0x30] = 0x1B, [0x31] = 0x2B,
    [0x33] = 0x27, [0x34] = 0x28,
    [0x35] = 0x29, [0x36] = 0x33,
    [0x37] = 0x34, [0x38] = 0x35,
    [0x39] = 0x3A,
    [0x3A] = 0x3B, [0x3B] = 0x3C,
    [0x3C] = 0x3D, [0x3D] = 0x3E,
    [0x3E] = 0x3F, [0x3F] = 0x40,
    [0x40] = 0x41, [0x41] = 0x42,
    [0x42] = 0x43, [0x43] = 0x44,
    [0x4F] = 0x4D, [0x50] = 0x4B,
    [0x51] = 0x50, [0x52] = 0x48,
};

typedef struct {
    uint8_t  prev_report[8];
    uint8_t  cur_report[8];
    uint8_t  ep_in;
    uint8_t  max_packet;
    usb_device_t *dev;
} hid_kbd_state_t;

static void hid_kbd_cb(int status, usb_transfer_t *t) {
    if (status < 0) return;
    hid_kbd_state_t *st = (hid_kbd_state_t*)((uint8_t*)t->buffer - __builtin_offsetof(hid_kbd_state_t, cur_report));
    uint8_t *cur = st->cur_report;
    uint8_t *prev = st->prev_report;

    uint8_t mod_changed = (uint8_t)(cur[0] ^ prev[0]);
    /* Map both left and right HID modifier bits to the same PS/2 make code.
     * Right-side scancodes like 0x9D/0xB8 have bit 7 set and can't use the
     * simple sc|0x80 release encoding; and GUI keys don't have valid Set 1
     * make codes. Collapse to left counterparts. See wiki/USB.md known limits. */
    static const uint8_t mod_ps2[8] = {
        0x1D, 0x2A, 0x38, 0x00,   /* L: Ctrl, Shift, Alt, GUI(drop) */
        0x1D, 0x36, 0x38, 0x00    /* R: same as L for Ctrl/Alt; RShift=0x36 already; GUI drop */
    };
    for (int i = 0; i < 8; i++) {
        if (mod_changed & (1u << i)) {
            bool pressed = (cur[0] & (1u << i)) != 0;
            uint8_t sc = mod_ps2[i];
            if (sc == 0) continue;  /* GUI keys not mapped; drop silently */
            keyboard_inject_scancode((uint8_t)(sc | (pressed ? 0u : 0x80u)));
        }
    }

    for (int i = 2; i < 8; i++) {
        uint8_t k = prev[i];
        if (k == 0) continue;
        bool still = false;
        for (int j = 2; j < 8; j++) if (cur[j] == k) { still = true; break; }
        if (!still && hid_to_ps2[k]) keyboard_inject_scancode((uint8_t)(hid_to_ps2[k] | 0x80u));
    }
    for (int i = 2; i < 8; i++) {
        uint8_t k = cur[i];
        if (k == 0) continue;
        bool was = false;
        for (int j = 2; j < 8; j++) if (prev[j] == k) { was = true; break; }
        if (!was && hid_to_ps2[k]) keyboard_inject_scancode(hid_to_ps2[k]);
    }

    for (int i = 0; i < 8; i++) prev[i] = cur[i];
}

static int hid_kbd_probe(usb_device_t *dev) {
    if (dev->class_code != HID_CLASS || dev->subclass != HID_SUBCLASS_BOOT
        || dev->protocol != HID_PROTO_KBD) return -1;

    usb_control(dev, 0x21, HID_SET_PROTOCOL, 0, 0, NULL, 0);
    usb_control(dev, 0x21, HID_SET_IDLE, 0, 0, NULL, 0);

    hid_kbd_state_t *st = (hid_kbd_state_t*)kmalloc(sizeof(hid_kbd_state_t));
    if (!st) return -1;
    for (int i = 0; i < 8; i++) { st->prev_report[i] = 0; st->cur_report[i] = 0; }
    st->ep_in = 0x81;
    st->max_packet = 8;
    st->dev = dev;
    dev->driver_data = st;

    usb_transfer_t t;
    t.dir = USB_DIR_IN; t.endpoint = 1; t.device_addr = dev->address;
    t.max_packet = st->max_packet; t.speed = dev->speed; t.data_toggle = 0;
    t.buffer = st->cur_report; t.length = 8;
    t.tt_hub_addr = dev->tt_hub_addr; t.tt_port = dev->tt_port;
    dev->hc->submit_interrupt(dev->hc, &t, hid_kbd_cb);

    KINFO("usb_hid: keyboard attached addr=%u", dev->address);
    return 0;
}

static void hid_kbd_disconnect(usb_device_t *dev) {
    if (dev->driver_data) kfree(dev->driver_data);
    dev->driver_data = NULL;
    KINFO("usb_hid: keyboard detached");
}

static usb_driver_t hid_kbd_driver = {
    .name = "usb-hid-kbd", .probe = hid_kbd_probe, .disconnect = hid_kbd_disconnect,
    .next = NULL
};

typedef struct {
    uint8_t report[3];
    usb_device_t *dev;
} hid_mouse_state_t;

static void hid_mouse_cb(int status, usb_transfer_t *t) {
    if (status < 0) return;
    uint8_t *r = t->buffer;
    mouse_inject_event(r[0], (int8_t)r[1], (int8_t)r[2]);
}

static int hid_mouse_probe(usb_device_t *dev) {
    if (dev->class_code != HID_CLASS || dev->subclass != HID_SUBCLASS_BOOT
        || dev->protocol != HID_PROTO_MOUSE) return -1;

    usb_control(dev, 0x21, HID_SET_PROTOCOL, 0, 0, NULL, 0);
    usb_control(dev, 0x21, HID_SET_IDLE, 0, 0, NULL, 0);

    hid_mouse_state_t *st = (hid_mouse_state_t*)kmalloc(sizeof(hid_mouse_state_t));
    if (!st) return -1;
    for (int i = 0; i < 3; i++) st->report[i] = 0;
    st->dev = dev;
    dev->driver_data = st;

    usb_transfer_t t;
    t.dir = USB_DIR_IN; t.endpoint = 1; t.device_addr = dev->address;
    t.max_packet = 3; t.speed = dev->speed; t.data_toggle = 0;
    t.buffer = st->report; t.length = 3;
    t.tt_hub_addr = dev->tt_hub_addr; t.tt_port = dev->tt_port;
    dev->hc->submit_interrupt(dev->hc, &t, hid_mouse_cb);

    KINFO("usb_hid: mouse attached addr=%u", dev->address);
    return 0;
}

static void hid_mouse_disconnect(usb_device_t *dev) {
    if (dev->driver_data) kfree(dev->driver_data);
    dev->driver_data = NULL;
    KINFO("usb_hid: mouse detached");
}

static usb_driver_t hid_mouse_driver = {
    .name = "usb-hid-mouse", .probe = hid_mouse_probe, .disconnect = hid_mouse_disconnect,
    .next = NULL
};

void usb_hid_init(void);
void usb_hid_init(void) {
    usb_register_driver(&hid_kbd_driver);
    usb_register_driver(&hid_mouse_driver);
}
