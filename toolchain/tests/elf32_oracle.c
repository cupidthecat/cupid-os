int common_value;
extern int external_value;

__attribute__((weak)) int weak_function(void) { return 5; }

int use_values(void) {
  return common_value + external_value + weak_function();
}
