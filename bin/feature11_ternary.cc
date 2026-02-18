// Feature 11 demo: ternary conditional operator (?:)
// Run with: cupidc /bin/feature11_ternary.cc

int pick(int cond, int a, int b) {
  return cond ? a : b;
}

void main() {
  int x = pick(1, 42, 7);
  int y = pick(0, 42, 7);
  int z = (x > y) ? x : y;
  int n = (0 ? 11 : (1 ? 22 : 33));

  Print("ternary x=%d y=%d z=%d n=%d\n", x, y, z, n);
}
