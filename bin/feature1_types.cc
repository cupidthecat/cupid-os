//help: CupidC Feature 1 demo — sized type keywords + legacy aliases
//help: Usage: feature1_types
//help: Verifies U0/U8/U16/U32/I8/I16/I32/Bool parse and run, while int/char/void remain valid.

U0 PrintPair(U32 a, I32 b) {
    print("a=");
    print_int(a);
    print(", b=");
    if (b < 0) {
        print("-");
        print_int((U32)(0 - b));
    } else {
        print_int((U32)b);
    }
    print("\n");
}

U0 PrintSigned(I32 v) {
    if (v < 0) {
        print("-");
        print_int((U32)(0 - v));
    } else {
        print_int((U32)v);
    }
}

void main() {
    U8 u8v = 250;
    U16 u16v = 65000;
    U32 u32v = 123456789;

    I8 i8_neg = -5;
    I16 i16_neg = -1234;
    I32 i32_neg = -424242;

    I8 i8_pos = 5;
    I16 i16_pos = 1234;
    I32 i32_pos = 424242;

    Bool ok = 1;

    int old_int = 42;
    char old_char = 'Z';

    print("[feature1] sized types demo\n");
    print("u8="); print_int(u8v); print(" u16="); print_int(u16v); print(" u32="); print_int(u32v); print("\n");
    print("neg: i8="); PrintSigned(i8_neg); print(" i16="); PrintSigned(i16_neg); print(" i32="); PrintSigned(i32_neg); print("\n");
    print("pos: i8="); PrintSigned(i8_pos); print(" i16="); PrintSigned(i16_pos); print(" i32="); PrintSigned(i32_pos); print("\n");

    PrintPair(u32v, i32_neg);
    PrintPair(u32v, i32_pos);

    if (old_int == 42 && old_char == 'Z' && ok) {
        println("feature1_types: PASS");
    } else {
        println("feature1_types: FAIL");
    }
}
