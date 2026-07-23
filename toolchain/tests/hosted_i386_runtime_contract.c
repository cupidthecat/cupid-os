#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CUPID_LINUX_SYS_BRK 45
#define CUPID_RUNTIME_UINT_MAX 4294967295u

int cupid_linux_syscall1(int number, unsigned int first);
int runtime_contract_run(int argc, char **argv);

static int allocator_contract(void) {
  unsigned char *first;
  unsigned char *middle;
  unsigned char *last;
  unsigned char *reused;
  unsigned char *expanded;
  unsigned char *zeroed;
  void *warmup;
  void *zero_size;
  void *reallocated;
  void *overflow;
  unsigned int baseline;
  unsigned int first_address;
  unsigned int grown;
  unsigned int released;
  size_t index;

  warmup = malloc(1u);
  if (warmup == (void *)0) {
    return 101;
  }
  free(warmup);
  baseline =
      (unsigned int)cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, 0u);

  warmup = malloc(4096u);
  if (warmup == (void *)0) {
    return 102;
  }
  grown = (unsigned int)cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, 0u);
  if (grown <= baseline) {
    free(warmup);
    return 103;
  }
  free(warmup);
  released =
      (unsigned int)cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, 0u);
  if (released != baseline) {
    return 104;
  }

  first = (unsigned char *)malloc(64u);
  middle = (unsigned char *)malloc(64u);
  last = (unsigned char *)malloc(64u);
  if (first == (unsigned char *)0 ||
      middle == (unsigned char *)0 ||
      last == (unsigned char *)0) {
    free(first);
    free(middle);
    free(last);
    return 105;
  }
  for (index = 0u; index < 64u; index++) {
    middle[index] = (unsigned char)(index + 1u);
  }
  first_address = (unsigned int)first;
  free(first);
  free(last);
  reused = (unsigned char *)malloc(32u);
  if ((unsigned int)reused != first_address) {
    free(reused);
    free(middle);
    return 106;
  }
  expanded = (unsigned char *)realloc(middle, 160u);
  if (expanded == (unsigned char *)0) {
    free(reused);
    free(middle);
    return 107;
  }
  for (index = 0u; index < 64u; index++) {
    if (expanded[index] != (unsigned char)(index + 1u)) {
      free(reused);
      free(expanded);
      return 108;
    }
  }
  free(reused);
  free(expanded);
  released =
      (unsigned int)cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, 0u);
  if (released != baseline) {
    return 109;
  }

  zeroed = (unsigned char *)calloc(8u, 4u);
  if (zeroed == (unsigned char *)0) {
    return 110;
  }
  for (index = 0u; index < 32u; index++) {
    if (zeroed[index] != 0u) {
      free(zeroed);
      return 111;
    }
  }
  free(zeroed);

  errno = 0;
  overflow = calloc(CUPID_RUNTIME_UINT_MAX, 2u);
  if (overflow != (void *)0 || errno != ENOMEM) {
    free(overflow);
    return 112;
  }
  zero_size = malloc(0u);
  if (zero_size == (void *)0) {
    return 113;
  }
  free(zero_size);
  reallocated = realloc((void *)0, 24u);
  if (reallocated == (void *)0) {
    return 114;
  }
  zero_size = realloc(reallocated, 0u);
  if (zero_size != (void *)0) {
    free(zero_size);
    return 115;
  }
  free((void *)0);
  return 0;
}

static int string_contract(void) {
  unsigned char source[12];
  unsigned char destination[12];
  size_t index;

  (void)memset(source, 0x5a, sizeof(source));
  source[11] = 0u;
  (void)memset(destination, 0, sizeof(destination));
  (void)memcpy(destination, source, sizeof(source));
  for (index = 0u; index < sizeof(source); index++) {
    if (destination[index] != source[index]) {
      return 201;
    }
  }
  if (strlen((const char *)destination) != 11u ||
      strcmp((const char *)destination, "ZZZZZZZZZZZ") != 0 ||
      strncmp((const char *)destination, "ZZZZ-other", 4u) != 0 ||
      strchr((const char *)destination, 'Z') !=
          (char *)destination ||
      strchr((const char *)destination, 'x') != (char *)0 ||
      strchr((const char *)destination, '\0') !=
          (char *)(destination + 11u)) {
    return 202;
  }
  return 0;
}

static int file_contract(const char *output_path,
                         const char *missing_path) {
  static const char expected[] = "ok -12 0000002A\n";
  char contents[32];
  char extra;
  FILE *stream;
  size_t expected_size = strlen(expected);
  size_t index;

  stream = fopen(output_path, "wb");
  if (stream == (FILE *)0) {
    return 301;
  }
  if (fprintf(stream, "ok %d %08X\n", -12, 42u) !=
          (int)expected_size ||
      fflush(stream) != 0 ||
      ftell(stream) != (long)expected_size) {
    (void)fclose(stream);
    return 302;
  }
  if (fclose(stream) != 0) {
    return 302;
  }

  stream = fopen(output_path, "rb");
  if (stream == (FILE *)0) {
    return 303;
  }
  if (fseek(stream, 0L, SEEK_END) != 0 ||
      ftell(stream) != (long)expected_size ||
      fseek(stream, 0L, 0) != 0 ||
      fread(contents, 1u, expected_size, stream) != expected_size) {
    (void)fclose(stream);
    return 304;
  }
  for (index = 0u; index < expected_size; index++) {
    if (contents[index] != expected[index]) {
      (void)fclose(stream);
      return 304;
    }
  }
  if (fread(&extra, 1u, 1u, stream) != 0u ||
      ferror(stream) != 0 || fclose(stream) != 0) {
    return 304;
  }

  errno = 0;
  stream = fopen(output_path, "invalid");
  if (stream != (FILE *)0 || errno != EINVAL) {
    if (stream != (FILE *)0) {
      (void)fclose(stream);
    }
    return 305;
  }
  errno = 0;
  stream = fopen(missing_path, "rb");
  if (stream != (FILE *)0 || errno != ENOENT) {
    if (stream != (FILE *)0) {
      (void)fclose(stream);
    }
    return 306;
  }

  stream = fopen(output_path, "rb");
  if (stream == (FILE *)0) {
    return 307;
  }
  errno = 0;
  if (fread((void *)0, 1u, 1u, stream) != 0u ||
      errno != EINVAL || ferror(stream) == 0) {
    (void)fclose(stream);
    return 308;
  }
  if (fclose(stream) != 0) {
    return 308;
  }

  errno = 0;
  if (fprintf(stderr, "%q") != -1 || errno != EINVAL ||
      fflush((FILE *)0) != 0) {
    return 309;
  }
  return 0;
}

static int directory_contract(void) {
  char directory[512];
  char small[1];

  if (getcwd(directory, sizeof(directory)) != directory ||
      directory[0] == '\0') {
    return 401;
  }
  errno = 0;
  if (getcwd(small, sizeof(small)) != (char *)0 ||
      errno != ERANGE) {
    return 402;
  }
  errno = 0;
  if (getcwd((char *)0, sizeof(directory)) != (char *)0 ||
      errno != EINVAL) {
    return 403;
  }
  return 0;
}

int runtime_contract_run(int argc, char **argv) {
  int result;
  if (argc != 3 || argv == (char **)0 ||
      argv[0] == (char *)0 || argv[1] == (char *)0 ||
      argv[2] == (char *)0) {
    return 1;
  }
  result = allocator_contract();
  if (result == 0) {
    result = string_contract();
  }
  if (result == 0) {
    result = file_contract(argv[1], argv[2]);
  }
  if (result == 0) {
    result = directory_contract();
  }
  return result;
}

int main(int argc, char **argv) {
  int result = runtime_contract_run(argc, argv);
  if (result != 0) {
    (void)fprintf(stderr, "runtime-contract: %d\n", result);
    return result;
  }
  (void)fprintf(stdout, "runtime-ok\n");
  return 0;
}
