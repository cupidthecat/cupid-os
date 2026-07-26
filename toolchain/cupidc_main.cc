#include "ctool.h"
#include "ctool_host.h"
#include "cupidc_emit.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#define CUPIDC_HOST_SOURCE_BYTES 67108864u
#define CUPIDC_HOST_OUTPUT_BYTES 67108864u
#define CUPIDC_HOST_ARENA_BYTES 536870912u

typedef struct {
  const char *input;
  const char *output;
  const char *native_root;
  const char **include_arguments;
  ctool_u32 *include_forms;
  ctool_u32 include_count;
  ctool_c_pp_macro_action_t *macro_actions;
  ctool_u32 macro_action_count;
  ctool_bool hosted_environment;
  ctool_bool gnu_extensions;
} cupidc_cli_t;

typedef struct {
  const ctool_string_t *include_paths;
  const ctool_u32 *include_forms;
  ctool_c_pp_include_root_t *include_roots;
  ctool_u32 include_count;
  const ctool_c_pp_macro_action_t *macro_actions;
  ctool_u32 macro_action_count;
  ctool_bool hosted_environment;
  ctool_bool gnu_extensions;
} cupidc_invocation_context_t;

static void cupidc_usage(FILE *stream) {
  (void)fprintf(
      stream,
      "usage: cupidc -c INPUT -o OUTPUT [-I PATH] "
      "[--include-angle PATH] [-D NAME[=VALUE]] [-U NAME] [--gnu] "
      "[--freestanding] [--root NATIVE_ROOT]\n");
}

static ctool_bool cupidc_string_equal_literal(ctool_string_t value,
                                               const char *literal) {
  size_t size = strlen(literal);
  return size <= 4294967295u && value.size == (ctool_u32)size &&
                 memcmp(value.data, literal, size) == 0
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static int cupidc_take_option_value(int argc, char **argv, int *index,
                                    const char *argument,
                                    const char *option,
                                    const char **value_out) {
  size_t option_size = strlen(option);
  if (strcmp(argument, option) == 0) {
    if (*index + 1 >= argc) {
      return -1;
    }
    *index = *index + 1;
    *value_out = argv[*index];
    return 1;
  }
  if (strncmp(argument, option, option_size) == 0 &&
      argument[option_size] != '\0') {
    *value_out = argument + option_size;
    return 1;
  }
  return 0;
}

static int cupidc_macro_action(const char *argument,
                               ctool_c_pp_macro_action_kind_t kind,
                               ctool_c_pp_macro_action_t *action) {
  const char *separator = strchr(argument, '=');
  size_t name_size =
      separator == (const char *)0 ? strlen(argument)
                                  : (size_t)(separator - argument);
  size_t replacement_size =
      separator == (const char *)0 ? 1u : strlen(separator + 1);
  if (name_size == 0u || name_size > 4294967295u ||
      replacement_size > 4294967295u ||
      (kind == CTOOL_C_PP_MACRO_UNDEF &&
       separator != (const char *)0)) {
    return 0;
  }
  action->kind = kind;
  action->name.data = argument;
  action->name.size = (ctool_u32)name_size;
  if (kind == CTOOL_C_PP_MACRO_UNDEF) {
    action->replacement = ctool_string("");
  } else if (separator == (const char *)0) {
    action->replacement = ctool_string("1");
  } else {
    action->replacement.data = separator + 1;
    action->replacement.size = (ctool_u32)replacement_size;
  }
  return cupidc_string_equal_literal(action->name, "__SIZEOF_POINTER__") ==
                 CTOOL_TRUE
             ? 0
             : 1;
}

static int cupidc_parse_cli(int argc, char **argv, cupidc_cli_t *cli) {
  int index;
  ctool_bool have_compile = CTOOL_FALSE;
  ctool_bool have_gnu = CTOOL_FALSE;
  ctool_bool have_freestanding = CTOOL_FALSE;
  size_t slot_count;
  if (argc < 0) {
    return 0;
  }
  (void)memset(cli, 0, sizeof(*cli));
  cli->hosted_environment = CTOOL_TRUE;
  slot_count = (size_t)argc + 1u;
  cli->include_arguments =
      (const char **)calloc(slot_count, sizeof(*cli->include_arguments));
  cli->include_forms =
      (ctool_u32 *)calloc(slot_count, sizeof(*cli->include_forms));
  cli->macro_actions = (ctool_c_pp_macro_action_t *)calloc(
      slot_count, sizeof(*cli->macro_actions));
  if (cli->include_arguments == (const char **)0 ||
      cli->include_forms == (ctool_u32 *)0 ||
      cli->macro_actions == (ctool_c_pp_macro_action_t *)0) {
    return 0;
  }
  cli->macro_actions[0].kind = CTOOL_C_PP_MACRO_DEFINE;
  cli->macro_actions[0].name = ctool_string("__SIZEOF_POINTER__");
  cli->macro_actions[0].replacement = ctool_string("4");
  cli->macro_action_count = 1u;
  for (index = 1; index < argc; index++) {
    const char *argument = argv[index];
    const char *value = (const char *)0;
    int taken;
    if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
      return -1;
    }
    if (strcmp(argument, "-c") == 0) {
      if (have_compile == CTOOL_TRUE) {
        return 0;
      }
      have_compile = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--gnu") == 0) {
      if (have_gnu == CTOOL_TRUE) {
        return 0;
      }
      cli->gnu_extensions = CTOOL_TRUE;
      have_gnu = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--freestanding") == 0) {
      if (have_freestanding == CTOOL_TRUE) {
        return 0;
      }
      cli->hosted_environment = CTOOL_FALSE;
      have_freestanding = CTOOL_TRUE;
      continue;
    }
    taken = cupidc_take_option_value(argc, argv, &index, argument, "-o",
                                     &value);
    if (taken != 0) {
      if (taken < 0 || cli->output != (const char *)0 ||
          value[0] == '\0') {
        return 0;
      }
      cli->output = value;
      continue;
    }
    taken = cupidc_take_option_value(argc, argv, &index, argument, "-I",
                                     &value);
    if (taken != 0) {
      if (taken < 0 || value[0] == '\0') {
        return 0;
      }
      cli->include_arguments[cli->include_count] = value;
      cli->include_forms[cli->include_count] =
          CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE;
      cli->include_count++;
      continue;
    }
    if (strcmp(argument, "--include-angle") == 0) {
      if (index + 1 >= argc) {
        return 0;
      }
      index++;
      value = argv[index];
      if (value[0] == '\0') {
        return 0;
      }
      cli->include_arguments[cli->include_count] = value;
      cli->include_forms[cli->include_count] = CTOOL_C_PP_INCLUDE_ANGLE;
      cli->include_count++;
      continue;
    }
    taken = cupidc_take_option_value(argc, argv, &index, argument, "-D",
                                     &value);
    if (taken != 0) {
      if (taken < 0 || value[0] == '\0' ||
          cupidc_macro_action(
              value, CTOOL_C_PP_MACRO_DEFINE,
              &cli->macro_actions[cli->macro_action_count]) == 0) {
        return 0;
      }
      cli->macro_action_count++;
      continue;
    }
    taken = cupidc_take_option_value(argc, argv, &index, argument, "-U",
                                     &value);
    if (taken != 0) {
      if (taken < 0 || value[0] == '\0' ||
          cupidc_macro_action(
              value, CTOOL_C_PP_MACRO_UNDEF,
              &cli->macro_actions[cli->macro_action_count]) == 0) {
        return 0;
      }
      cli->macro_action_count++;
      continue;
    }
    if (strcmp(argument, "--root") == 0) {
      if (cli->native_root != (const char *)0 || index + 1 >= argc) {
        return 0;
      }
      index++;
      if (argv[index][0] == '\0') {
        return 0;
      }
      cli->native_root = argv[index];
      continue;
    }
    if (argument[0] == '-' || cli->input != (const char *)0) {
      return 0;
    }
    cli->input = argument;
  }
  return have_compile == CTOOL_TRUE &&
                 cli->input != (const char *)0 &&
                 cli->output != (const char *)0
             ? 1
             : 0;
}

static ctool_bool cupidc_native_separator(char character) {
  return character == '/' || character == '\\' ? CTOOL_TRUE : CTOOL_FALSE;
}

static char *cupidc_working_directory(void) {
  size_t capacity = 256u;
  while (capacity <= 1048576u) {
    char *directory = (char *)malloc(capacity);
    if (directory == (char *)0) {
      return (char *)0;
    }
#if defined(_WIN32)
    if (_getcwd(directory, (int)capacity) != (char *)0) {
      return directory;
    }
#else
    if (getcwd(directory, capacity) != (char *)0) {
      return directory;
    }
#endif
    if (errno != ERANGE) {
      free(directory);
      return (char *)0;
    }
    free(directory);
    capacity *= 2u;
  }
  return (char *)0;
}

static char *cupidc_logical_path(const char *path) {
  char *directory = (char *)0;
  char *native = (char *)0;
  char *logical;
  size_t path_size;
  size_t directory_size = 0u;
  size_t separator_size = 1u;
  size_t native_size;
  size_t logical_start;
  size_t index;
  ctool_bool absolute = CTOOL_FALSE;
  if (path == (const char *)0 || path[0] == '\0') {
    return (char *)0;
  }
  path_size = strlen(path);
#if defined(_WIN32)
  if (path_size >= 2u &&
      cupidc_native_separator(path[0]) == CTOOL_TRUE &&
      cupidc_native_separator(path[1]) == CTOOL_TRUE) {
    return (char *)0;
  }
  if (path_size >= 3u && path[1] == ':' &&
      cupidc_native_separator(path[2]) == CTOOL_TRUE) {
    absolute = CTOOL_TRUE;
  } else if (path_size >= 2u && path[1] == ':') {
    return (char *)0;
  }
#else
  if (path[0] == '/') {
    absolute = CTOOL_TRUE;
  }
#endif
  if (absolute == CTOOL_FALSE) {
    directory = cupidc_working_directory();
    if (directory == (char *)0) {
      return (char *)0;
    }
    directory_size = strlen(directory);
#if defined(_WIN32)
    if (cupidc_native_separator(path[0]) == CTOOL_TRUE) {
      if (directory_size < 2u || directory[1] != ':') {
        free(directory);
        return (char *)0;
      }
      directory_size = 2u;
      separator_size = 0u;
    }
#endif
  }
  if (path_size > (size_t)-1 - directory_size - 2u) {
    free(directory);
    return (char *)0;
  }
  native_size = absolute == CTOOL_TRUE
                    ? path_size
                    : directory_size + separator_size + path_size;
  native = (char *)malloc(native_size + 1u);
  if (native == (char *)0) {
    free(directory);
    return (char *)0;
  }
  if (absolute == CTOOL_TRUE) {
    (void)memcpy(native, path, path_size + 1u);
  } else {
    (void)memcpy(native, directory, directory_size);
    if (separator_size != 0u) {
      native[directory_size] = '/';
    }
    (void)memcpy(native + directory_size + separator_size, path,
                 path_size + 1u);
  }
  free(directory);
#if defined(_WIN32)
  logical_start = 1u;
#else
  logical_start = 0u;
#endif
  logical = (char *)malloc(native_size + logical_start + 1u);
  if (logical == (char *)0) {
    free(native);
    return (char *)0;
  }
  if (logical_start != 0u) {
    logical[0] = '/';
  }
  for (index = 0u; index < native_size; index++) {
    logical[logical_start + index] =
        cupidc_native_separator(native[index]) == CTOOL_TRUE
            ? '/'
            : native[index];
  }
  logical[logical_start + native_size] = '\0';
  free(native);
  return logical;
}

static ctool_status_t cupidc_compile_body(ctool_invocation_t *invocation,
                                          void *user_data) {
  cupidc_invocation_context_t *context =
      (cupidc_invocation_context_t *)user_data;
  ctool_c_pp_request_t pp_request;
  ctool_c_pp_result_t tape;
  ctool_c_parse_request_t parse_request;
  ctool_c_translation_unit_t unit;
  ctool_path_t root;
  const ctool_limits_t *limits;
  ctool_u32 index;
  ctool_status_t status;
  if (invocation == (ctool_invocation_t *)0 ||
      context == (cupidc_invocation_context_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  limits = ctool_job_limits(invocation->job);
  status = limits == (const ctool_limits_t *)0
               ? CTOOL_ERR_INVALID_ARGUMENT
               : ctool_path_root(ctool_job_arena(invocation->job), &root);
  for (index = 0u; status == CTOOL_OK && index < context->include_count;
       index++) {
    status = ctool_path_resolve(
        ctool_job_arena(invocation->job), &root,
        context->include_paths[index], limits->path_bytes,
        &context->include_roots[index].directory);
    context->include_roots[index].forms = context->include_forms[index];
  }
  (void)memset(&pp_request, 0, sizeof(pp_request));
  pp_request.mode = CTOOL_C_PP_MODE_C11;
  pp_request.hosted_environment = context->hosted_environment;
  pp_request.gnu_extensions = context->gnu_extensions;
  pp_request.include_roots = context->include_roots;
  pp_request.include_root_count = context->include_count;
  pp_request.macro_actions = context->macro_actions;
  pp_request.macro_action_count = context->macro_action_count;
  (void)memset(&tape, 0, sizeof(tape));
  if (status == CTOOL_OK) {
    status =
        ctool_c_preprocess(invocation->job, invocation->input, &pp_request,
                           &tape);
  }
  (void)memset(&parse_request, 0, sizeof(parse_request));
  parse_request.mode = CTOOL_C_PP_MODE_C11;
  parse_request.gnu_extensions = context->gnu_extensions;
  (void)memset(&unit, 0, sizeof(unit));
  if (status == CTOOL_OK) {
    status = ctool_c_parse(invocation->job, &tape, &parse_request, &unit);
  }
  if (status == CTOOL_OK) {
    status = ctool_c_emit_object(invocation->job, &unit, invocation->output);
  }
  return status;
}

int main(int argc, char **argv) {
  cupidc_cli_t cli;
  cupidc_invocation_context_t context;
  ctool_host_adapter_t adapter;
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_config_t config;
  ctool_invocation_request_t request;
  ctool_invocation_result_t result;
  ctool_string_t *include_paths = (ctool_string_t *)0;
  ctool_c_pp_include_root_t *include_roots =
      (ctool_c_pp_include_root_t *)0;
  char **owned_include_paths = (char **)0;
  char *owned_input = (char *)0;
  char *owned_output = (char *)0;
  const char *logical_input;
  const char *logical_output;
  const char *native_root;
  ctool_u32 index;
  ctool_status_t status;
  int parsed = cupidc_parse_cli(argc, argv, &cli);
  int exit_code = 1;
  if (parsed < 0) {
    cupidc_usage(stdout);
    exit_code = 0;
    goto done;
  }
  if (parsed == 0) {
    cupidc_usage(stderr);
    exit_code = 2;
    goto done;
  }
  if (cli.include_count != 0u) {
    include_paths = (ctool_string_t *)calloc(
        (size_t)cli.include_count, sizeof(*include_paths));
    include_roots = (ctool_c_pp_include_root_t *)calloc(
        (size_t)cli.include_count, sizeof(*include_roots));
    owned_include_paths =
        (char **)calloc((size_t)cli.include_count,
                        sizeof(*owned_include_paths));
    if (include_paths == (ctool_string_t *)0 ||
        include_roots == (ctool_c_pp_include_root_t *)0 ||
        owned_include_paths == (char **)0) {
      (void)fprintf(stderr, "cupidc: path allocation failed\n");
      goto done;
    }
  }
  if (cli.native_root != (const char *)0) {
    if (cli.input[0] != '/' || cli.output[0] != '/') {
      (void)fprintf(
          stderr,
          "cupidc: --root requires logical input and output paths\n");
      goto done;
    }
    logical_input = cli.input;
    logical_output = cli.output;
    native_root = cli.native_root;
    for (index = 0u; index < cli.include_count; index++) {
      if (cli.include_arguments[index][0] != '/') {
        (void)fprintf(
            stderr,
            "cupidc: --root requires logical include paths\n");
        goto done;
      }
      include_paths[index] = ctool_string(cli.include_arguments[index]);
    }
  } else {
    owned_input = cupidc_logical_path(cli.input);
    owned_output = cupidc_logical_path(cli.output);
    if (owned_input == (char *)0 || owned_output == (char *)0) {
      (void)fprintf(stderr, "cupidc: invalid input or output path\n");
      goto done;
    }
    logical_input = owned_input;
    logical_output = owned_output;
#if defined(_WIN32)
    native_root = "";
#else
    native_root = "/";
#endif
    for (index = 0u; index < cli.include_count; index++) {
      owned_include_paths[index] =
          cupidc_logical_path(cli.include_arguments[index]);
      if (owned_include_paths[index] == (char *)0) {
        (void)fprintf(stderr, "cupidc: invalid include path\n");
        goto done;
      }
      include_paths[index] = ctool_string(owned_include_paths[index]);
    }
  }
  status = ctool_host_adapter_init(&adapter, native_root);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupidc: job setup failed (%s)\n",
                  ctool_status_name(status));
    goto done;
  }
  limits.source_bytes = CUPIDC_HOST_SOURCE_BYTES;
  limits.output_bytes = CUPIDC_HOST_OUTPUT_BYTES;
  limits.arena_bytes = CUPIDC_HOST_ARENA_BYTES;
  config = ctool_host_job_config(&adapter, limits);
  (void)memset(&context, 0, sizeof(context));
  context.include_paths = include_paths;
  context.include_forms = cli.include_forms;
  context.include_roots = include_roots;
  context.include_count = cli.include_count;
  context.macro_actions = cli.macro_actions;
  context.macro_action_count = cli.macro_action_count;
  context.hosted_environment = cli.hosted_environment;
  context.gnu_extensions = cli.gnu_extensions;
  request.input_path = ctool_string(logical_input);
  request.output_path = ctool_string(logical_output);
  (void)memset(&result, 0, sizeof(result));
  status =
      ctool_invoke(&config, &request, cupidc_compile_body, &context, &result);
  if (status != CTOOL_OK) {
    if (result.diagnostic_count != 0u) {
      /* ctool_invoke has already rendered the ordered diagnostics. */
    } else if (result.body_status == CTOOL_ERR_NOT_FOUND) {
      (void)fprintf(stderr, "cupidc: cannot load %s (%s)\n", cli.input,
                    ctool_status_name(result.body_status));
    } else if (result.body_status == CTOOL_OK &&
               result.output_committed == CTOOL_FALSE) {
      (void)fprintf(stderr, "cupidc: cannot write %s (%s)\n", cli.output,
                    ctool_status_name(status));
    } else {
      (void)fprintf(stderr, "cupidc: compilation failed (%s)\n",
                    ctool_status_name(status));
    }
    goto done;
  }
  exit_code = 0;

done:
  if (owned_include_paths != (char **)0) {
    for (index = 0u; index < cli.include_count; index++) {
      free(owned_include_paths[index]);
    }
  }
  free(owned_output);
  free(owned_input);
  free(owned_include_paths);
  free(include_roots);
  free(include_paths);
  free(cli.macro_actions);
  free(cli.include_forms);
  free(cli.include_arguments);
  return exit_code;
}
