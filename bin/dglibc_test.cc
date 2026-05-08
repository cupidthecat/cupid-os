//help: dglibc smoke test - snprintf / malloc-free / setjmp round-trip
//help: Usage: dglibc_test

void main() {
    I32 rc;
    rc = dglibc_test_main();
    if (rc != 0) {
        serial_write_string("[FAIL] dglibc_test returned non-zero\n");
        return;
    }
    serial_write_string("[PASS] dglibc_test\n");
}

main();
