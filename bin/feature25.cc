//help: Smoke test: continue reaches the nearest loop through a switch
//help: Usage: feature25

int condition_calls;

int keep_going(int value) {
    condition_calls = condition_calls + 1;
    return value < 2;
}

int test_do_switch_continue() {
    int body_runs = 0;
    int tail_runs = 0;
    int value = 0;
    condition_calls = 0;

    do {
        body_runs = body_runs + 1;
        value = value + 1;
        switch (value) {
            case 1:
                continue;
            default:
                break;
        }
        tail_runs = tail_runs + 1;
    } while (keep_going(value));

    return body_runs == 2 && condition_calls == 2 && tail_runs == 1;
}

int test_for_nested_switch_continue() {
    int i = 0;
    int outer_hits = 0;
    int tail_runs = 0;

    for (i = 0; i < 4; i = i + 1) {
        switch (i & 1) {
            case 0:
                switch (i) {
                    case 2:
                        continue;
                    default:
                        break;
                }
                outer_hits = outer_hits + 1;
                break;
            default:
                outer_hits = outer_hits + 10;
                break;
        }
        tail_runs = tail_runs + 1;
    }

    return outer_hits == 21 && tail_runs == 3;
}

int test_while_switch_continue() {
    int value = 0;
    int sum = 0;

    while (value < 4) {
        value = value + 1;
        switch (value) {
            case 2:
                continue;
            default:
                break;
        }
        sum = sum + value;
    }

    return sum == 8;
}

int test_switch_stack_cleanup() {
    int i = 0;

    // Each loop would leak more than the 4 MB process stack without cleanup.
    for (i = 0; i < 600000; i = i + 1) {
        switch (i) {
            default:
                switch (i) {
                    default:
                        continue;
                }
        }
    }

    for (i = 0; i < 600000; i = i + 1) {
        switch (i) {
            default:
                switch (i) {
                    default:
                        break;
                }
                break;
        }
    }

    return i == 600000;
}

int test_continue_without_loop_rejected() {
    return repl_eval("switch (1) { default: continue; }") == -1;
}

int test_nearest_loop_continue() {
    int outer = 0;
    int inner = 0;
    int sum = 0;
    int outer_tails = 0;

    for (outer = 0; outer < 2; outer = outer + 1) {
        switch (outer) {
            default:
                for (inner = 0; inner < 3; inner = inner + 1) {
                    switch (inner) {
                        case 1:
                            continue;
                        default:
                            sum = sum + outer * 10 + inner;
                            break;
                    }
                    sum = sum + 100;
                }
                sum = sum + 1000;
                break;
        }
        outer_tails = outer_tails + 1;
    }

    return sum == 2424 && outer_tails == 2;
}

void main() {
    int do_ok = test_do_switch_continue();
    int for_ok = test_for_nested_switch_continue();
    int while_ok = test_while_switch_continue();
    int stack_ok = test_switch_stack_cleanup();
    int reject_ok = test_continue_without_loop_rejected();
    int nearest_ok = test_nearest_loop_continue();

    if (do_ok && for_ok && while_ok && stack_ok && reject_ok && nearest_ok) {
        println("feature25: PASS");
        serial_printf("[feature25] PASS do=%d for=%d while=%d stack=%d reject=%d nearest=%d\n",
                      do_ok, for_ok, while_ok, stack_ok, reject_ok, nearest_ok);
    } else {
        println("feature25: FAIL");
        serial_printf("[feature25] FAIL do=%d for=%d while=%d stack=%d reject=%d nearest=%d\n",
                      do_ok, for_ok, while_ok, stack_ok, reject_ok, nearest_ok);
    }
}
