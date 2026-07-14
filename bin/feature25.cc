//help: Smoke test: continue in a do loop reaches the condition
//help: Usage: feature25

int condition_calls;

int keep_going(int value) {
    condition_calls = condition_calls + 1;
    return value < 2;
}

void main() {
    int body_runs = 0;
    int value = 0;
    condition_calls = 0;

    do {
        body_runs = body_runs + 1;
        value = value + 1;
        if (value == 1) {
            continue;
        }
    } while (keep_going(value));

    if (body_runs == 2 && condition_calls == 2) {
        println("feature25: PASS");
        serial_printf("[feature25] PASS body=%d condition=%d\n",
                      body_runs, condition_calls);
    } else {
        print("feature25: FAIL body=");
        print_int(body_runs);
        print(" condition=");
        print_int(condition_calls);
        print("\n");
        serial_printf("[feature25] FAIL body=%d condition=%d\n",
                      body_runs, condition_calls);
    }
}
