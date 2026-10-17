#include "cupidbuild_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int unhex(char *out, size_t capacity, const char *text) {
  size_t offset = 0u;
  while (*text) {
    unsigned int value = 0u;
    unsigned int digit;
    for (digit = 0u; digit < 2u; digit++) {
      unsigned int part = (unsigned char)*text++;
      if (part >= '0' && part <= '9') part -= '0';
      else if (part >= 'a' && part <= 'f') part = part - 'a' + 10u;
      else return 0;
      value = value * 16u + part;
    }
    if (value == 0u || offset + 1u >= capacity) return 0;
    out[offset++] = (char)value;
  }
  out[offset] = 0;
  return 1;
}

static int decimal(const char *text, unsigned long long *value) {
  unsigned long long result = 0u;
  if (!*text) return 0;
  while (*text) {
    unsigned int digit = (unsigned char)*text++ - '0';
    if (digit > 9u || result > (18446744073709551615ULL - digit) / 10u)
      return 0;
    result = result * 10u + digit;
  }
  *value = result;
  return 1;
}

static int check_file(const char *path, unsigned long long expected,
                       const char *export_path) {
  unsigned char bytes[65536];
  unsigned long long offset = 0u;
  FILE *file = fopen(path, "rb");
  FILE *copy = export_path != (const char *)0 ? fopen(export_path, "wb") : (FILE *)0;
  int valid = file != (FILE *)0;
  if (!valid || (export_path != (const char *)0 && copy == (FILE *)0)) {
    if (file != (FILE *)0) (void)fclose(file);
    if (copy != (FILE *)0) (void)fclose(copy);
    return 0;
  }
  while (valid && offset < expected) {
    size_t wanted = expected - offset > sizeof(bytes) ? sizeof(bytes) :
                    (size_t)(expected - offset);
    size_t size = fread(bytes, 1u, wanted, file);
    size_t index;
    if (size != wanted) { valid = 0; break; }
    for (index = 0u; index < size; index++) {
      if (bytes[index] != (unsigned char)(((unsigned int)offset + index) * 29u + 17u)) {
        valid = 0; break;
      }
    }
    if (copy != (FILE *)0 && fwrite(bytes, 1u, size, copy) != size) valid = 0;
    offset += size;
  }
  if (valid && (fread(bytes, 1u, 1u, file) != 0u || ferror(file))) valid = 0;
  if (fclose(file) != 0) valid = 0;
  if (copy != (FILE *)0 && fclose(copy) != 0) valid = 0;
  return valid;
}

static int change_first_byte(const char *path) {
  unsigned char changed = 42u;
  FILE *file = fopen(path, "r+b");
  int valid;
  if (file == (FILE *)0) return 0;
  valid = fwrite(&changed, 1u, 1u, file) == 1u;
  return fclose(file) == 0 && valid;
}

int main(int argc, char **argv) {
  char root[8192];
  char input[8192];
  unsigned long long size;
  unsigned long long capacity;
  const char *frozen = (const char *)0;
  cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
  cupidbuild_host_snapshot_t snapshot;
  int accepted;
  int closed;
  unsigned int index;
  if ((argc != 5 && argc != 6) || !unhex(root, sizeof(root), argv[1]) ||
      !decimal(argv[3], &size) || !decimal(argv[4], &capacity)) return 90;
  if (snprintf(input, sizeof(input), "%s/input.bin", root) < 0) return 91;
  if (!cupidbuild_host_transaction_open(root, "source.cc", "nested/result.bin", &transaction))
    return 92;
#if defined(CUPIDBUILD_STREAM_FROZEN_INPUT_TEST_NEW_API)
  frozen = (const char *)1;
  (void)memset(&snapshot, 0xa5, sizeof(snapshot));
  accepted = strcmp(argv[2], "ordinary") == 0 ?
      cupidbuild_host_freeze_input(transaction, input, "image.bin", &frozen, &snapshot) :
      cupidbuild_host_freeze_input_bounded(transaction, input, "image.bin", capacity, &frozen, &snapshot);
#else
  (void)capacity;
  accepted = cupidbuild_host_freeze_input(transaction, input, "image.bin", &frozen, &snapshot);
#endif
  if (!accepted) {
#if defined(CUPIDBUILD_STREAM_FROZEN_INPUT_TEST_NEW_API)
    if (strcmp(argv[2], "ordinary") != 0 &&
        (frozen != (const char *)0 || snapshot.present != 0 || snapshot.size != 0u))
      return 96;
#endif
    (void)printf("rejected %s\n", cupidbuild_host_error(transaction));
    closed = cupidbuild_host_transaction_close(transaction);
    (void)printf("close %d\n", closed);
    return closed ? 7 : 8;
  }
  if (snapshot.size != size || !check_file(frozen, size, argc == 6 ? argv[5] : (const char *)0)) return 93;
  (void)printf("captured %u ", (unsigned int)snapshot.size);
  for (index = 0u; index < 32u; index++) (void)printf("%02x", snapshot.sha256[index]);
  (void)printf("\n");
  if (!cupidbuild_host_input_matches_snapshot(transaction, input, &snapshot) ||
      !cupidbuild_host_require_inputs(transaction) ||
      !cupidbuild_host_require_frozen_inputs(transaction)) return 94;
  if (strcmp(argv[2], "live-drift") == 0) {
    if (!change_first_byte(input) || cupidbuild_host_require_inputs(transaction) ||
        cupidbuild_host_input_matches_snapshot(transaction, input, &snapshot) ||
        !cupidbuild_host_require_frozen_inputs(transaction)) return 97;
    (void)printf("live drift rejected\n");
  } else if (strcmp(argv[2], "frozen-drift") == 0) {
    int changed = change_first_byte(frozen);
    int unchanged = cupidbuild_host_require_frozen_inputs(transaction);
    if (changed) {
      if (unchanged) return 98;
      (void)printf("frozen drift rejected\n");
    } else {
      /* A rejected write can still change a sealed memfd's timestamps. The
       * metadata guard must retain its rejection; the complete bytes must
       * remain unchanged whenever the write itself was blocked. */
      if (!check_file(frozen, size, (const char *)0)) return 98;
      (void)printf("frozen drift %s\n",
                   unchanged ? "blocked" : "blocked, metadata rejected");
    }
  } else if (strcmp(argv[2], "payload") == 0) {
    size_t payload_size = 0u;
    unsigned char *payload = cupidbuild_host_read_frozen_input(transaction, frozen,
        (size_t)capacity, &payload_size);
    if (size > 67108864u ? payload != (unsigned char *)0 :
        payload == (unsigned char *)0 || payload_size != size) return 99;
    free(payload);
    if (!cupidbuild_host_require_inputs(transaction) ||
        !cupidbuild_host_require_frozen_inputs(transaction)) return 100;
    (void)printf("payload %s\n", size > 67108864u ? "bounded" : "accepted");
  }
  closed = cupidbuild_host_transaction_close(transaction);
  (void)printf("close %d\n", closed);
  return closed ? 0 : 95;
}
