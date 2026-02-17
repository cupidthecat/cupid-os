//help: CupidC Feature 2 demo - top-level statement execution (no mandatory main)
//help: Usage: feature2_top_level
//help: Verifies top-level code runs automatically and main() still runs after top-level.

print("[feature2] top-level begin\n");

I32 top_answer;
top_answer = 40 + 2;
print("top_answer=");
print_int(top_answer);
print("\n");

U0 SayTop(U8 *name) {
    print("hello from top-level, ");
    print(name);
    print("\n");
}

SayTop("cupid-os");

void main() {
    print("[feature2] main after top-level, answer=");
    print_int(top_answer);
    print("\n");

    if (top_answer == 42) {
        println("feature2_top_level: PASS");
    } else {
        println("feature2_top_level: FAIL");
    }
}
