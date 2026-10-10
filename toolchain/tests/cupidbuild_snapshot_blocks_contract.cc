#include "cupidbuild_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

static int decode(char *text) {
  size_t size = strlen(text);
  size_t index;
  if (size % 2u) return 0;
  for (index = 0u; index < size; index += 2u) {
    unsigned int high = (unsigned char)text[index];
    unsigned int low = (unsigned char)text[index + 1u];
    high = high >= '0' && high <= '9' ? high - '0' : high - 'a' + 10u;
    low = low >= '0' && low <= '9' ? low - '0' : low - 'a' + 10u;
    if (high > 15u || low > 15u || high * 16u + low == 0u) return 0;
    text[index / 2u] = (char)(high * 16u + low);
  }
  text[size / 2u] = 0;
  return 1;
}

static int extent(const char *text, size_t *result) {
  size_t value = 0u;
  if (*text == 0) return 0;
  while (*text != 0) {
    unsigned int digit = (unsigned char)*text++ - '0';
    if (digit > 9u || value > (67108865u - digit) / 10u) return 0;
    value = value * 10u + digit;
  }
  *result = value;
  return 1;
}

static int write_pattern(const char *path, size_t size) {
  unsigned char block[65536];
  size_t index;
#if defined(_WIN32)
  int standard = strcmp(path, "-") == 0;
  HANDLE file = standard ? GetStdHandle(STD_OUTPUT_HANDLE)
      : CreateFileA(path, GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE || file == NULL) return 0;
#else
  FILE *file = strcmp(path, "-") == 0 ? stdout : fopen(path, "wb");
  if (file == NULL) return 0;
#endif
  for (index = 0u; index < sizeof(block); index++)
    block[index] = (unsigned char)(index * 29u + 17u);
  while (size != 0u) {
    size_t chunk = size < sizeof(block) ? size : sizeof(block);
    int written;
#if defined(_WIN32)
    DWORD count = 0u;
    written = WriteFile(file, block, (DWORD)chunk, &count, NULL) && count == chunk;
#else
    written = fwrite(block, 1u, chunk, file) == chunk;
#endif
    if (!written) {
#if defined(_WIN32)
      if (!standard) (void)CloseHandle(file);
#else
      (void)fclose(file);
#endif
      return 0;
    }
    size -= chunk;
  }
#if defined(_WIN32)
  return standard || CloseHandle(file);
#else
  return fclose(file) == 0;
#endif
}

int main(int argc, char **argv) {
  cupidbuild_host_transaction_t *transaction = NULL;
  cupidbuild_host_snapshot_t snapshot;
  unsigned char *bytes = NULL;
  const char *path;
  const char *mode;
  const char *frozen = NULL;
  const char *arguments[4];
  char writer[8192];
  size_t size;
  unsigned int index;
  int private_file;
  int payload;
  int captured;
  int changed = -1;
  int ok;
  if (argc != 4) return 90;
  if (!extent(argv[3], &size)) return 91;
  if (strcmp(argv[1], "emit-file") == 0 || strcmp(argv[1], "emit-stdout") == 0)
    return write_pattern(argv[2], size) ? 0 : 93;
  if (!decode(argv[1])) return 90;
  mode = argv[2];
  private_file = strncmp(mode, "private", 7u) == 0;
  payload = strstr(mode, "bytes") != NULL;
  ok = cupidbuild_host_transaction_open(argv[1], "source.cc", "nested/file.o", &transaction);
  if (!ok) {
    fprintf(stderr, "open: %s\n", cupidbuild_host_error(transaction));
    (void)cupidbuild_host_transaction_close(transaction);
    return 2;
  }
  path = private_file ? cupidbuild_host_private_output(transaction)
                      : cupidbuild_host_candidate(transaction);
  if (strstr(mode, "limit") != NULL) {
    ok = write_pattern(path, size);
  } else {
    snprintf(writer, sizeof(writer), "%s/writer", argv[1]);
    arguments[0] = private_file ? "emit-stdout" : "emit-file";
    arguments[1] = private_file ? "-" : path;
    arguments[2] = argv[3];
    arguments[3] = NULL;
    ok = cupidbuild_host_freeze_input(transaction, writer, "writer.exe", &frozen, NULL) &&
         cupidbuild_host_make_input_executable(transaction, frozen);
    if (ok) {
      int status = private_file
          ? cupidbuild_host_run_to_private_output(transaction, frozen, arguments, 10000u)
          : cupidbuild_host_run(transaction, frozen, arguments, 10000u);
      if (status != 0) fprintf(stderr, "writer exit %d\n", status);
      ok = status == 0;
    }
  }
  captured = ok && (private_file
      ? cupidbuild_host_capture_private_output(transaction, &snapshot, payload ? &bytes : NULL)
      : cupidbuild_host_capture_candidate(transaction, &snapshot, payload ? &bytes : NULL));
  if (strstr(mode, "limit") != NULL) {
    printf("limit %d\n", captured);
    ok = ok && !captured;
  } else {
    ok = captured && snapshot.present && snapshot.size == size;
    if (ok && payload) {
      unsigned char digest[32];
      ok = bytes != NULL && bytes[size] == 0u;
      for (index = 0u; ok && index < size; index++)
        if (bytes[index] != (unsigned char)(index * 29u + 17u)) ok = 0;
      if (ok) {
        cupidbuild_host_sha256_bytes(bytes, size, digest);
        ok = memcmp(digest, snapshot.sha256, sizeof(digest)) == 0;
      }
    }
    free(bytes);
    bytes = NULL;
    if (ok) {
      printf("captured %u ", (unsigned int)snapshot.size);
      for (index = 0u; index < 32u; index++) printf("%02x", snapshot.sha256[index]);
      puts("");
    }
    if (ok && strstr(mode, "wrong") != NULL) {
      cupidbuild_host_snapshot_t wrong = snapshot;
      wrong.sha256[0] ^= 1u;
      ok = !(private_file
          ? cupidbuild_host_require_private_output(transaction, &wrong)
          : cupidbuild_host_require_candidate(transaction, &wrong));
      printf("wrong %d\n", !ok);
    }
    if (ok) ok = private_file
        ? cupidbuild_host_require_private_output(transaction, &snapshot)
        : cupidbuild_host_require_candidate(transaction, &snapshot);
    if (ok && strstr(mode, "pause") != NULL) {
      unsigned char resume;
      puts("ready");
      if (fflush(stdout) != 0 || fread(&resume, 1u, 1u, stdin) != 1u || resume != 'x')
        ok = 0;
      if (ok) {
        int boundary = cupidbuild_host_require_publication_boundary(transaction);
        int published = cupidbuild_host_publish_if_changed(transaction, &changed);
        printf("drift %d %d\n", boundary, published);
        ok = !boundary && !published;
      }
    } else if (ok && !private_file && strstr(mode, "wrong") == NULL) {
      ok = cupidbuild_host_publish_if_changed(transaction, &changed);
      printf("changed %d\n", changed);
    }
  }
  if (!ok) fprintf(stderr, "check: %s\n", cupidbuild_host_error(transaction));
  {
    int closed = cupidbuild_host_transaction_close(transaction);
    if (strstr(mode, "limit") != NULL) printf("closed %d\n", closed);
    else if (!closed) return 92;
  }
  return ok ? 0 : 3;
}
