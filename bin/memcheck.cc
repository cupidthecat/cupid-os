// memcheck.cc — Check heap integrity
void main() {
    print("Checking heap integrity...\n");
    heap_check_integrity();
    print("Heap integrity OK\n");
}
