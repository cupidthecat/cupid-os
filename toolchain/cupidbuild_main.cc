#include "cupidbuild.h"

#include <stdio.h>
#include <string.h>

static void cupidbuild_usage(FILE *stream) {
  (void)fprintf(
      stream,
      "usage: cupidbuild assemble-cupidasm-object "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild assemble-bootloader "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild assemble-smp-trampoline "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n");
}

static int cupidbuild_take_value(int argc, char **argv, int *index,
                                 const char *option, const char **value_out) {
  if (strcmp(argv[*index], option) != 0) {
    return 0;
  }
  if (*index + 1 >= argc || *value_out != (const char *)0) {
    return -1;
  }
  *index = *index + 1;
  *value_out = argv[*index];
  return 1;
}

int main(int argc, char **argv) {
  cupidbuild_assembly_request_t request;
  int operation = 0;
  int index;
  if (argc == 2 &&
      (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    cupidbuild_usage(stdout);
    return 0;
  }
  (void)memset(&request, 0, sizeof(request));
  if (argc >= 2) {
    if (strcmp(argv[1], "assemble-cupidasm-object") == 0) {
      operation = 1;
    } else if (strcmp(argv[1], "assemble-bootloader") == 0) {
      operation = 2;
    } else if (strcmp(argv[1], "assemble-smp-trampoline") == 0) {
      operation = 3;
    }
  }
  if (operation != 0) {
    for (index = 2; index < argc; index++) {
      int taken = cupidbuild_take_value(
          argc, argv, &index, "--seed-manifest", &request.seed_manifest);
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--root",
                                      &request.repository_root);
      }
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--source",
                                      &request.source);
      }
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--output",
                                      &request.output);
      }
      if (taken <= 0) {
        cupidbuild_usage(stderr);
        return 2;
      }
    }
    if (request.seed_manifest == (const char *)0 ||
        request.repository_root == (const char *)0 ||
        request.source == (const char *)0 ||
        request.output == (const char *)0) {
      cupidbuild_usage(stderr);
      return 2;
    }
    if (operation == 1) {
      return cupidbuild_assemble_object(&request);
    }
    if (operation == 2) {
      return cupidbuild_assemble_bootloader(&request);
    }
    return cupidbuild_assemble_smp_trampoline(&request);
  }
  cupidbuild_usage(stderr);
  return 2;
}
