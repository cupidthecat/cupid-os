//help: CupidC Feature 8 demo — reg / noreg storage hints
//help: Usage: feature8_reg_noreg
//help: Verifies reg/noreg are recognized and compile as storage hints.

void main() {
    reg I32 i;
    noreg I32 counter;

    i = 6;
    counter = 7;

    I32 sum = i * counter;

    print("sum=");
    print_int((U32)sum);
    print("\n");

    if (sum == 42) {
        println("feature8_reg_noreg: PASS");
    } else {
        println("feature8_reg_noreg: FAIL");
    }
}
