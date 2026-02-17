//help: CupidC Feature 4 demo - two-pass style function resolution
//help: Usage: feature4_forward_calls
//help: Verifies function calls work before definition, without prototypes.

void main() {
    I32 v = AddNoProto(40, 2);

    print("forward=");
    print_int(v);
    print("\n");

    if (v == 42) {
        println("feature4_forward_calls: PASS");
    } else {
        println("feature4_forward_calls: FAIL");
    }
}

I32 AddNoProto(I32 a, I32 b) {
    return a + b;
}
