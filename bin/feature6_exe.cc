//help: CupidC Feature 6 demo - #exe blocks
//help: Usage: feature6_exe
//help: Verifies #exe block support in JIT mode.

I32 g_value;

#exe {
    g_value = 40 + 2;
}

void main() {
    print("g_value=");
    print_int(g_value);
    print("\n");

    if (g_value == 42) {
        println("feature6_exe: PASS");
    } else {
        println("feature6_exe: FAIL");
    }
}
