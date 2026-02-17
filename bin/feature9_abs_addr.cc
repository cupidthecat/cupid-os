//help: CupidC Feature 9 demo - absolute addressing sugar
//help: Usage: feature9_abs_addr
//help: Verifies pointer assignment from integer literal without casts.

void main() {
    U32 *vga = 0xB8000;

    vga[0] = 0x0741;   // 'A' with attribute

    print("vga=");
    print_hex((U32)vga);
    print("\n");

    if ((U32)vga == 0xB8000) {
        println("feature9_abs_addr: PASS");
    } else {
        println("feature9_abs_addr: FAIL");
    }
}
