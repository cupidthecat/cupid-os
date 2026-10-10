#include "../artifact_size_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  unsigned char *bytes;
  size_t size;
} file_image_t;
static const char report_schema[] = "cupid.artifact-size-verification.v1";
static char contract_error[512];
static int set_error(const char *message) {
  (void)snprintf(contract_error, sizeof(contract_error), "%s", message);
  return 0;
}
static void file_image_release(file_image_t *image) {
  free(image->bytes);
  image->bytes = (unsigned char *)0;
  image->size = 0u;
}

static int read_request_file(const char *path, file_image_t *image) {
  FILE *stream;
  long length;
  size_t read_size;
  image->bytes = (unsigned char *)0;
  image->size = 0u;
  stream = fopen(path, "rb");
  if (stream == (FILE *)0) {
    return set_error("request file is unavailable");
  }
  if (fseek(stream, 0L, SEEK_END) != 0) {
    (void)fclose(stream);
    return set_error("cannot measure the request file");
  }
  length = ftell(stream);
  if (length < 0L || fseek(stream, 0L, 0) != 0) {
    (void)fclose(stream);
    return set_error("request file has an unsupported size");
  }
  image->size = (size_t)length;
  image->bytes = (unsigned char *)malloc(image->size == 0u ? 1u : image->size);
  if (image->bytes == (unsigned char *)0) {
    (void)fclose(stream);
    image->size = 0u;
    return set_error("cannot allocate the request file");
  }
  read_size = fread(image->bytes, 1u, image->size, stream);
  if (read_size != image->size || fclose(stream) != 0) {
    file_image_release(image);
    return set_error("cannot read the request file");
  }
  return 1;
}

static int run_check(const char *path) {
  file_image_t first;
  file_image_t second;
  artifact_size_policy_result_t result;
  int ok;
  contract_error[0] = '\0';
  if (!read_request_file(path, &first)) {
    (void)fprintf(stderr, "Cupid artifact-size contract failed: %s\n",
                  contract_error);
    return 1;
  }
  ok = artifact_size_policy_validate(first.bytes, first.size, &result,
                                    contract_error, sizeof(contract_error));
  if (ok) {
    ok = read_request_file(path, &second);
    if (ok && (second.size != first.size ||
               memcmp(second.bytes, first.bytes, first.size) != 0)) {
      ok = set_error("request changed while it was checked");
    }
    file_image_release(&second);
  }
  file_image_release(&first);
  if (!ok) {
    (void)fprintf(stderr, "Cupid artifact-size contract failed: %s\n",
                  contract_error);
    return 1;
  }
  (void)printf(
      "{\"artifact_count\":16,\"schema\":\"%s\","
      "\"total_exact_bytes\":%llu}\n",
      report_schema, (unsigned long long)result.total_exact_bytes);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "check") == 0) {
    return run_check(argv[2]);
  }
  (void)fprintf(stderr,
                "Cupid artifact-size contract failed: usage: "
                "artifact-size-policy-contract check REQUEST\n");
  return 2;
}
