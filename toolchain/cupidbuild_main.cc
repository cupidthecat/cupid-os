#include "cupidbuild.h"
#include "cupidbuild_artifacts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CUPID_NATIVE_UTF8_ENABLE
#include "native_utf8.h"
#endif

static void cupidbuild_usage(FILE *stream) {
  (void)fprintf(
      stream,
      "usage: cupidbuild assemble-cupidasm-object "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild assemble-bootloader "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild assemble-smp-trampoline "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild assemble-iso-pattern "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild embed-jpeg "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild generate-ksyms "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild flatten-kernel "
      "--seed-manifest MANIFEST --root ROOT --input-manifest MANIFEST "
      "--output OUTPUT\n"
      "       cupidbuild compile-kernel "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild compile-doom "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild compile-production "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild compile-user "
      "--seed-manifest MANIFEST --root ROOT --source SOURCE --output OUTPUT\n"
      "       cupidbuild link-user "
      "--seed-manifest MANIFEST --root ROOT --source OBJECT --output OUTPUT\n"
      "       cupidbuild generate-profile-manifest "
      "--seed-manifest MANIFEST --root ROOT --output OUTPUT\n"
      "Seeded commands accept [--seed-release RELEASE].\n"
      "       cupidbuild verify-artifact-sizes --root ROOT --policy POLICY "
      "--seed-manifest LINUX_MANIFEST "
      "[--checked-manifest WINDOWS_MANIFEST --execution-manifest MANIFEST]\n"
      "usage: cupidbuild run --seed-manifest MANIFEST "
      "--root ROOT --tool {cupidc|cupidobj|cupidld} [--timeout SECONDS] "
      "[--seed-release RELEASE] -- "
      "TOOL_ARGS...\n");
}

static void cupidbuild_run_usage(FILE *stream) {
  (void)fprintf(stream,
                "usage: cupidbuild run --seed-manifest MANIFEST "
                "--root ROOT --tool {cupidc|cupidobj|cupidld} "
                "[--timeout SECONDS] [--seed-release RELEASE] -- TOOL_ARGS...\n");
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

static int cupidbuild_artifact_command(int argc, char **argv) {
  cupidbuild_artifact_request_t request = {NULL, NULL, NULL};
  const char *checked_manifest = NULL;
  const char *execution_manifest = NULL;
  artifact_size_policy_result_t result;
  char *error;
  int index;
  int ok;
  for (index = 2; index < argc; index++) {
    int found = cupidbuild_take_value(argc, argv, &index, "--root",
                                      &request.repository_root);
    if (found == 0) found = cupidbuild_take_value(argc, argv, &index, "--policy",
                                                 &request.policy_path);
    if (found == 0) found = cupidbuild_take_value(argc, argv, &index,
        "--seed-manifest", &request.linux_manifest_path);
    if (found == 0) found = cupidbuild_take_value(argc, argv, &index,
        "--checked-manifest", &checked_manifest);
    if (found == 0) found = cupidbuild_take_value(argc, argv, &index,
        "--execution-manifest", &execution_manifest);
    if (found != 1) { cupidbuild_usage(stderr); return 2; }
  }
  if (request.repository_root == NULL || request.policy_path == NULL ||
      request.linux_manifest_path == NULL || request.repository_root[0] == 0 ||
      request.policy_path[0] == 0 || request.linux_manifest_path[0] == 0 ||
      (checked_manifest == NULL) != (execution_manifest == NULL)) {
    cupidbuild_usage(stderr); return 2;
  }
  error = (char *)malloc(CUPIDBUILD_ARTIFACT_ERROR_BYTES);
  if (error == NULL) {
    (void)fprintf(stderr, "artifact size verification failed: cannot allocate diagnostic\n");
    return 1;
  }
  ok = checked_manifest != NULL ?
      cupidbuild_verify_artifact_sizes_selected(&request, checked_manifest,
          execution_manifest, &result, error, CUPIDBUILD_ARTIFACT_ERROR_BYTES) :
      cupidbuild_verify_artifact_sizes(&request, &result, error,
          CUPIDBUILD_ARTIFACT_ERROR_BYTES);
  if (!ok) (void)fprintf(stderr, "artifact size verification failed: %s\n", error);
  free(error);
  if (!ok) return 1;
  (void)printf("Cupid artifact sizes: ok (%u exact artifacts)\n",
               (unsigned int)result.artifact_count);
  return 0;
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
  const char *seed_release = (const char *)0;
  int operation = 0;
  int index;
  if (argc == 2 &&
      (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    cupidbuild_usage(stdout);
    return 0;
  }
  if (argc >= 2 && strcmp(argv[1], "verify-artifact-sizes") == 0)
    return cupidbuild_artifact_command(argc, argv);
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
    } else if (strcmp(argv[1], "assemble-iso-pattern") == 0) {
      operation = 8;
    } else if (strcmp(argv[1], "compile-kernel") == 0) {
      operation = 9;
    } else if (strcmp(argv[1], "compile-doom") == 0) {
      operation = 10;
    } else if (strcmp(argv[1], "compile-production") == 0) {
      operation = 11;
    } else if (strcmp(argv[1], "compile-user") == 0) {
      operation = 12;
    } else if (strcmp(argv[1], "link-user") == 0) {
      operation = 13;
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
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--seed-release",
                                      &seed_release);
      }
      if (taken <= 0) {
        cupidbuild_usage(stderr);
        return 2;
      }
    }
    if (*seed_manifest == (const char *)0 ||
        *repository_root == (const char *)0 ||
        (input != (const char **)0 && *input == (const char *)0) ||
        *output == (const char *)0 ||
        (seed_release != (const char *)0 && seed_release[0] == '\0')) {
      cupidbuild_usage(stderr);
      return 2;
    }
    if (operation == 13) {
      if (request.seed_manifest[0] == '\0' ||
          request.repository_root[0] == '\0' ||
          request.source[0] == '\0' || request.output[0] == '\0') {
        cupidbuild_usage(stderr);
        return 2;
      }
      return cupidbuild_link_user_with_release(&request, seed_release);
    }
    if (operation == 1) {
      return cupidbuild_assemble_object_with_release(&request, seed_release);
    }
    if (operation == 2) {
      return cupidbuild_assemble_bootloader_with_release(&request, seed_release);
    }
    if (operation == 3) {
      return cupidbuild_assemble_smp_trampoline_with_release(&request, seed_release);
    }
    if (operation == 4) {
      return cupidbuild_embed_jpeg_with_release(&request, seed_release);
    }
    if (operation == 5) {
      return cupidbuild_generate_ksyms_with_release(&request, seed_release);
    }
    if (operation == 6) {
      return cupidbuild_flatten_kernel_with_release(&kernel_request, seed_release);
    }
    if (operation == 8) {
      return cupidbuild_assemble_iso_pattern_with_release(&request, seed_release);
    }
    if (operation == 9) {
      return cupidbuild_compile_kernel_with_release(&request, seed_release);
    }
    if (operation == 10) {
      return cupidbuild_compile_doom_with_release(&request, seed_release);
    }
    if (operation == 11) {
      return cupidbuild_compile_production_with_release(&request, seed_release);
    }
    if (operation == 12) {
      return cupidbuild_compile_user_with_release(&request, seed_release);
    }
    return cupidbuild_generate_profile_manifest_with_release(&profile_request, seed_release);
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
           timeout != (const char *)0) ||
          (strcmp(argv[index], "--seed-release") == 0 &&
           seed_release != (const char *)0)) {
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
      if (taken == 0) {
        taken = cupidbuild_take_value(argc, argv, &index, "--seed-release",
                                      &seed_release);
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
        (seed_release != (const char *)0 && seed_release[0] == '\0') ||
        (timeout != (const char *)0 &&
         !cupidbuild_parse_timeout(timeout, &run_request.timeout_seconds))) {
      cupidbuild_run_usage(stderr);
      return 2;
    }
    if (seed_release != (const char *)0) {
      return cupidbuild_run_checked_tool_with_release(&run_request, seed_release);
    }
    return cupidbuild_run_checked_tool(&run_request);
  }
  cupidbuild_usage(stderr);
  return 2;
}
