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
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "usage: cupidbuild run --seed-manifest MANIFEST "
      "--root ROOT --tool cupidobj [--timeout SECONDS] -- TOOL_ARGS...\n");
}

static void cupidbuild_run_usage(FILE *stream) {
  (void)fprintf(stream,
                "usage: cupidbuild run --seed-manifest MANIFEST "
                "--root ROOT --tool cupidobj [--timeout SECONDS] "
                "-- TOOL_ARGS...\n");
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

static int cupidbuild_parse_timeout(const char *text,
                                    unsigned int *seconds_out) {
  unsigned int value = 0u;
  const char *cursor = text;
  if (text == (const char *)0 || text[0] == '\0') {
    return 0;
  }
  while (*cursor != '\0') {
    unsigned int digit;
    if (*cursor < '0' || *cursor > '9') {
      return 0;
    }
    digit = (unsigned int)(*cursor - '0');
    if (value > 8640u || (value == 8640u && digit > 0u)) {
      return 0;
    }
    value = value * 10u + digit;
    cursor++;
  }
  if (value == 0u) {
    return 0;
  }
  *seconds_out = value;
  return 1;
}

int main(int argc, char **argv) {
  cupidbuild_assembly_request_t request;
  cupidbuild_run_request_t run_request;
  int operation = 0;
  int index;
  if (argc == 2 &&
      (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    cupidbuild_usage(stdout);
    return 0;
  }
  (void)memset(&request, 0, sizeof(request));
  (void)memset(&run_request, 0, sizeof(run_request));
  run_request.timeout_seconds = 300u;
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
  if (argc >= 2 && strcmp(argv[1], "run") == 0) {
    int separator = 0;
    const char *timeout = (const char *)0;
    for (index = 2; index < argc; index++) {
      int taken;
      if (strcmp(argv[index], "--") == 0) {
        separator = 1;
        run_request.arguments = (const char *const *)&argv[index + 1];
        break;
      }
      if ((strcmp(argv[index], "--seed-manifest") == 0 &&
           run_request.seed_manifest != (const char *)0) ||
          (strcmp(argv[index], "--root") == 0 &&
           run_request.working_directory != (const char *)0) ||
          (strcmp(argv[index], "--tool") == 0 &&
           run_request.tool != (const char *)0) ||
          (strcmp(argv[index], "--timeout") == 0 &&
           timeout != (const char *)0)) {
        cupidbuild_run_usage(stderr);
        return 2;
      }
      taken = cupidbuild_take_value(argc, argv, &index, "--seed-manifest",
                                    &run_request.seed_manifest);
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--root",
                                      &run_request.working_directory);
      }
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--tool",
                                      &run_request.tool);
      }
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--timeout",
                                      &timeout);
      }
      if (taken <= 0) {
        cupidbuild_run_usage(stderr);
        return 2;
      }
    }
    if (separator == 0 || run_request.seed_manifest == (const char *)0 ||
        run_request.working_directory == (const char *)0 ||
        run_request.tool == (const char *)0 ||
        strcmp(run_request.tool, "cupidobj") != 0 ||
        (timeout != (const char *)0 &&
         !cupidbuild_parse_timeout(timeout, &run_request.timeout_seconds))) {
      cupidbuild_run_usage(stderr);
      return 2;
    }
    return cupidbuild_run_checked_tool(&run_request);
  }
  cupidbuild_usage(stderr);
  return 2;
}
