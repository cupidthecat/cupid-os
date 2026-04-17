//help: P5 SMP smoke: count CPUs + atomic-increment binding verification
//help: Usage: feature20_smp

U0 Main() {
    I32 n = smp_cpu_count();
    I32 me = smp_current_cpu();
    serial_printf("[feature20] cpus=%d current=%d\n", n, me);

    U32 counter = 0;
    I32 i = 0;
    while (i < 100000) {
        smp_atomic_inc(&counter);
        i = i + 1;
    }

    if (counter == 100000) {
        serial_printf("[feature20] PASS counter=%u\n", counter);
    } else {
        serial_printf("[feature20] FAIL counter=%u\n", counter);
    }
}

Main();
