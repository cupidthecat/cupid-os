    //help: CupidC Feature 5 demo — Print/PrintLine builtins
    //help: Usage: feature5_print_builtin
    //help: Verifies compiler-recognized Print and PrintLine with format args.

    void main() {
        I32 x = 42;
        U32 u = 42;
        U8 *name = "cupid";

        Print("x=%d u=%u hex=%x HEX=%X name=%s char=%c ptr=%p %%\n", x, u, u, u, name, 'A', name);
        PrintLine("feature5 builtins ok, x=%d", x);

        if (x == 42) {
            println("feature5_print_builtin: PASS");
        } else {
            println("feature5_print_builtin: FAIL");
        }
    }
