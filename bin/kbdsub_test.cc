//help: Task2 keyboard subscriber smoke — subscribe/unsubscribe + make/break
//help: Usage: kbdsub_test

void main() {
    I32 rc;
    I32 calls;
    I32 last_sc;
    I32 last_pressed;

    /* First subscribe should succeed (returns 0). */
    rc = keyboard_test_sub_start();
    if (rc != 0) {
        serial_write_string("[FAIL] kbdsub: subscribe returned non-zero\n");
        return;
    }

    /* Second subscribe attempt must be rejected (slot taken, returns -1). */
    rc = keyboard_test_sub_start();
    if (rc != -1) {
        serial_write_string("[FAIL] kbdsub: second subscribe should reject\n");
        keyboard_test_sub_stop();
        return;
    }

    /* Inject press 'A' (0x1E make) then release (0x9E break). */
    keyboard_inject_scancode(0x1E);
    keyboard_inject_scancode(0x9E);

    calls        = keyboard_test_sub_calls();
    last_sc      = keyboard_test_sub_last_sc();
    last_pressed = keyboard_test_sub_last_pressed();

    if (calls != 2) {
        serial_write_string("[FAIL] kbdsub: expected 2 callback calls\n");
        keyboard_test_sub_stop();
        return;
    }
    if (last_sc != 0x1E || last_pressed != 0) {
        serial_write_string("[FAIL] kbdsub: last event mismatch\n");
        keyboard_test_sub_stop();
        return;
    }

    keyboard_test_sub_stop();
    serial_write_string("[PASS] kbdsub: subscribe/unsubscribe round-trip\n");
}

main();
