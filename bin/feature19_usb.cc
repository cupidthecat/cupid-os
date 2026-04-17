//help: P4 USB stack smoke: HC registered + at least one USB device + HID present
//help: Usage: feature19_usb

void main() {
    int n = usb_device_count();
    int i = 0;
    int found_hid = 0;

    serial_printf("[feature19] usb_device_count=%d\n", n);

    while (i < n) {
        int cls = usb_device_class(i);
        serial_printf("[feature19] dev[%d] class=%d\n", i, cls);
        if ((cls & 255) == 3) {
            found_hid = 1;
        }
        i = i + 1;
    }

    if (n >= 1 && found_hid == 1) {
        serial_printf("[feature19] PASS\n");
    } else {
        serial_printf("[feature19] FAIL n=%d found_hid=%d\n", n, found_hid);
    }
}

main();
