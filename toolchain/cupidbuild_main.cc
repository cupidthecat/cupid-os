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
      "       cupidbuild embed-jpeg "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild generate-ksyms "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild flatten-kernel "
      "--seed-manifest MANIFEST --root ROOT --input-manifest MANIFEST "
      "--output OUTPUT\n"
      "       cupidbuild generate-profile-manifest "
      "--seed-manifest MANIFEST --root ROOT --output OUTPUT\n"
      "usage: cupidbuild run --seed-manifest MANIFEST "
      "--root ROOT --tool {cupidc|cupidobj|cupidld} [--timeout SECONDS] -- "
      "TOOL_ARGS...\n");
}

static void cupidbuild_run_usage(FILE *stream) {
  (void)fprintf(stream,
                "usage: cupidbuild run --seed-manifest MANIFEST "
                "--root ROOT --tool {cupidc|cupidobj|cupidld} "
                "[--timeout SECONDS] -- TOOL_ARGS...\n");
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
  cupidbuild_kernel_request_t kernel_request;
  cupidbuild_profile_request_t profile_request;
  cupidbuild_run_request_t run_request;
  int operation = 0;
  int index;
  if (argc == 2 &&
      (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    cupidbuild_usage(stdout);
    return 0;
  }
  (void)memset(&request, 0, sizeof(request));
  (void)memset(&kernel_request, 0, sizeof(kernel_request));
  (void)memset(&profile_request, 0, sizeof(profile_request));
  (void)memset(&run_request, 0, sizeof(run_request));
  run_request.timeout_seconds = 300u;
  if (argc >= 2) {
    if (strcmp(argv[1], "assemble-cupidasm-object") == 0) {
      operation = 1;
    } else if (strcmp(argv[1], "assemble-bootloader") == 0) {
      operation = 2;
    } else if (strcmp(argv[1], "assemble-smp-trampoline") == 0) {
      operation = 3;
    } else if (strcmp(argv[1], "embed-jpeg") == 0) {
      operation = 4;
    } else if (strcmp(argv[1], "generate-ksyms") == 0) {
      operation = 5;
    } else if (strcmp(argv[1], "flatten-kernel") == 0) {
      operation = 6;
    } else if (strcmp(argv[1], "generate-profile-manifest") == 0) {
      operation = 7;
    }
  }
  if (operation != 0) {
    const char **seed_manifest = &request.seed_manifest;
    const char **repository_root = &request.repository_root;
    const char **input = &request.source;
    const char **output = &request.output;
    if (operation == 6) {
      seed_manifest = &kernel_request.seed_manifest;
      repository_root = &kernel_request.repository_root;
      input = &kernel_request.input_manifest;
      output = &kernel_request.output;
    } else if (operation == 7) {
      seed_manifest = &profile_request.seed_manifest;
      repository_root = &profile_request.repository_root;
      input = (const char **)0;
      output = &profile_request.output;
    }
    for (index = 2; index < argc; index++) {
      int taken = cupidbuild_take_value(
          argc, argv, &index, "--seed-manifest", seed_manifest);
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--root",
                                      repository_root);
      }
      if (taken == 0 && input != (const char **)0) {
        taken = cupidbuild_take_value(
            argc, argv, &index,
            operation == 6 ? "--input-manifest" : "--source",
            input);
      }
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--output",
                                      output);
      }
      if (taken <= 0) {
        cupidbuild_usage(stderr);
        return 2;
      }
    }
    if (*seed_manifest == (const char *)0 ||
        *repository_root == (const char *)0 ||
        (input != (const char **)0 && *input == (const char *)0) ||
        *output == (const char *)0) {
      cupidbuild_usage(stderr);
      return 2;
    }
    if (operation == 1) {
      return cupidbuild_assemble_object(&request);
    }
    if (operation == 2) {
      return cupidbuild_assemble_bootloader(&request);
    }
    if (operation == 3) {
      return cupidbuild_assemble_smp_trampoline(&request);
    }
    if (operation == 4) {
      return cupidbuild_embed_jpeg(&request);
    }
    if (operation == 5) {
      return cupidbuild_generate_ksyms(&request);
    }
    if (operation == 6) {
      return cupidbuild_flatten_kernel(&kernel_request);
    }
    return cupidbuild_generate_profile_manifest(&profile_request);
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
        (strcmp(run_request.tool, "cupidc") != 0 &&
         strcmp(run_request.tool, "cupidobj") != 0 &&
         strcmp(run_request.tool, "cupidld") != 0) ||
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
