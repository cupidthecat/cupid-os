int cupid_windows_write_stdout(const char *text, unsigned int size);

static const char cupid_windows_marker[] =
    "Cupid-built Windows runtime: ok\n";

int main(void) {
  unsigned int size = (unsigned int)sizeof(cupid_windows_marker) - 1u;
  return cupid_windows_write_stdout(cupid_windows_marker, size) == (int)size
             ? 37
             : 91;
}
