#include "cupidbuild_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  FILE *output;
  const char *mode;
  uint64_t offset;
  unsigned int calls;
  size_t largest;
} stream_context_t;

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

static int decimal(const char *text, uint64_t *value) {
  uint64_t result = 0u;
  if (*text == 0) return 0;
  while (*text != 0) {
    unsigned int digit = (unsigned char)*text++ - '0';
    if (digit > 9u || result > (((uint64_t)-1) - digit) / 10u) return 0;
    result = result * 10u + digit;
  }
  *value = result;
  return 1;
}

static int resume(void) {
  unsigned char byte;
  return fread(&byte, 1u, 1u, stdin) == 1u && byte == 'x';
}

static int receive(void *opaque, uint64_t offset,
                    const unsigned char *bytes, size_t count) {
  stream_context_t *context = (stream_context_t *)opaque;
  if (offset != context->offset || count == 0u || count > 65536u) return 0;
  context->calls++;
  if (count > context->largest) context->largest = count;
  if ((strcmp(context->mode, "fail-first") == 0 && context->calls == 1u) ||
      (strcmp(context->mode, "fail-second") == 0 && context->calls == 2u)) return 0;
  if (fwrite(bytes, 1u, count, context->output) != count) return 0;
  context->offset += (uint64_t)count;
  if (strcmp(context->mode, "pause-stream") == 0 && context->calls == 1u) {
    puts("chunk");
    if (fflush(stdout) != 0 || !resume()) return 0;
  }
  return 1;
}

static int cleared(const cupidbuild_host_stream_observation_t *result) {
  unsigned char zero[sizeof(*result)];
  (void)memset(zero, 0, sizeof(zero));
  return memcmp(result, zero, sizeof(zero)) == 0;
}

int main(int argc, char **argv) {
  cupidbuild_host_observer_t *observer = NULL;
  cupidbuild_host_stream_observation_t result;
  stream_context_t context;
  uint64_t limit;
  unsigned int index;
  int ok;
  int current;
  int status;
  if (argc != 6 || !decode(argv[1]) || !decode(argv[2]) || !decode(argv[5]) ||
      !decimal(argv[4], &limit)) return 90;
  (void)memset(&context, 0, sizeof(context));
  context.mode = argv[3];
  context.output = fopen(argv[5], "wb");
  if (context.output == NULL || !cupidbuild_host_observer_open(argv[1], &observer)) return 91;
  (void)memset(&result, 0xa5, sizeof(result));
  ok = cupidbuild_host_observer_file_stream(
      strcmp(context.mode, "null-observer") == 0 ? NULL : observer,
      strcmp(context.mode, "null-path") == 0 ? NULL : argv[2], limit,
      strcmp(context.mode, "hash") == 0 ? NULL : receive, &context,
      strcmp(context.mode, "null-result") == 0 ? NULL : &result);
  if (fclose(context.output) != 0) return 92;
  if (!ok) {
    int zero = strcmp(context.mode, "null-result") == 0 || cleared(&result);
    int poisoned = strcmp(context.mode, "null-observer") == 0 ||
                   !cupidbuild_host_observer_require_unchanged(observer);
    (void)memset(&result, 0xa5, sizeof(result));
    if (strcmp(context.mode, "null-observer") != 0 &&
        (cupidbuild_host_observer_file_stream(observer, argv[2], limit,
            NULL, NULL, &result) || !cleared(&result))) return 93;
    printf("reject %d %d %u %u %u\n", zero, poisoned, context.calls,
           (unsigned int)(context.offset >> 32u), (unsigned int)context.offset);
    status = zero && poisoned ? 2 : 94;
  } else {
    if (strcmp(context.mode, "buffer") == 0) {
      unsigned char *bytes = NULL;
      unsigned char digest[32];
      uint64_t size = 0u;
      if (!cupidbuild_host_observer_file(observer, argv[2], (size_t)limit,
              &bytes, &size) || size != result.size) return 95;
      cupidbuild_host_sha256_bytes(bytes, (size_t)size, digest);
      free(bytes);
      if (memcmp(digest, result.sha256, 32u) != 0) return 96;
    }
    printf("ready %u %u %u %u ", (unsigned int)(result.size >> 32u),
           (unsigned int)result.size, context.calls, (unsigned int)context.largest);
    for (index = 0u; index < 32u; index++) printf("%02x", result.sha256[index]);
    puts("");
    if (fflush(stdout) != 0) return 97;
    if (strcmp(context.mode, "pause-after") == 0 && !resume()) return 98;
    current = cupidbuild_host_observer_require_unchanged(observer);
    printf("current %d\n", current);
    status = current ? 0 : 3;
  }
  if (!cupidbuild_host_observer_close(observer)) return 99;
  return status;
}
